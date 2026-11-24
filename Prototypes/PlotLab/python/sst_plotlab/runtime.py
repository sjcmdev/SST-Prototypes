"""Python execution/binding edge. Native callers receive typed values, never JSON."""
from __future__ import annotations

import ast
import builtins
import hashlib
import inspect
import io
import json
import keyword
import os
from pathlib import Path
import re
import sys
import time
from types import SimpleNamespace

import numpy as np
from .worksheet import Project, Column, identifier, namespace, current_project

_consoles = {}


class PythonError(RuntimeError):
    code = "python"

    def __init__(self, message, *, row=0, line=0):
        super().__init__(message)
        self.row, self.line = row, line


class Budget:
    # shortcut: native extension calls finish first; isolate them if hard deadlines are required.
    def __init__(self, timeout_s, cancelled):
        self.deadline = time.monotonic() + timeout_s
        self.cancelled = cancelled

    def check(self):
        if self.cancelled():
            raise PythonError("Cancelled")
        if time.monotonic() >= self.deadline:
            raise PythonError("Python time limit exceeded")

    def trace(self, frame, event, arg):
        frame.f_trace_opcodes = True
        self.check()
        return self.trace

    def __enter__(self):
        self.previous = sys.gettrace()
        sys.settrace(self.trace)
        return self

    def __exit__(self, *args):
        sys.settrace(self.previous)


def parameters(source):
    """Static parameter declarations: numeric default or inclusive (min,max)."""
    tree = ast.parse(source or "pass")
    result = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "params":
            if node.args:
                raise ValueError("Use params(name=value)")
            for arg in node.keywords:
                value = ast.literal_eval(arg.value)
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    default, lo, hi, slider = float(value), 0.0, 0.0, False
                elif isinstance(value, tuple) and len(value) == 2 and all(isinstance(v, (int, float)) for v in value):
                    lo, hi = map(float, value)
                    if not lo < hi:
                        raise ValueError(f"Invalid parameter range: {arg.arg}")
                    default, slider = (lo + hi) / 2, True
                else:
                    raise ValueError(f"Parameter {arg.arg}: use a number or (minimum, maximum)")
                if arg.arg is None or not np.isfinite([default, lo, hi]).all():
                    raise ValueError("Parameters must have names and finite values")
                if any(p[0] == arg.arg for p in result):
                    raise ValueError(f"Duplicate parameter: {arg.arg}")
                result.append((arg.arg, default, lo, hi, slider))
    return result


def execute(backend, source, active_sheet=0, timeout_s=5, cancelled=lambda: False, values=None, console=0):
    if console and console in _consoles:
        project, env = _consoles[console]
        project._native, project._active = backend, active_sheet
    else:
        project = Project(backend, active_sheet)
        env = namespace(project)
        if console:
            _consoles[console] = project, env
    env.update(namespace(project))
    output = io.StringIO()
    def print_local(*args, **kwargs):
        kwargs.setdefault("file", output)
        builtins.print(*args, **kwargs)
    env["print"] = print_local
    declarations = parameters(source)
    param_values = dict(values or {})
    env.update(param_values)
    for name, default, lo, hi, slider in declarations:
        value = float(param_values.get(name, default))
        if not np.isfinite(value) or (slider and not lo <= value <= hi):
            raise ValueError(f"Parameter {name} is outside its range")
        env[name] = value
    def params(**unused):
        return SimpleNamespace(**{name: env[name] for name, *_ in declarations})
    env["params"] = params
    token = current_project.set(project)
    try:
        with Budget(timeout_s, cancelled):
            exec(compile(source, "<PlotLab script>", "exec"), env)
    except Exception as error:
        error.plotlab_output = output.getvalue()
        raise
    finally:
        current_project.reset(token)
        if console:
            project._native = None
    return output.getvalue()


class FormulaGlobals(dict):
    def __init__(self, project, rows):
        super().__init__(namespace(project))
        self.project = project
        self["i"] = np.arange(1, rows + 1, dtype=float)
        self["__builtins__"] = builtins.__dict__

    def __missing__(self, name):
        token = "variable:" + name
        id = self.project._bindings.get(token)
        if id is None:
            try:
                if name in ("x", "y"):
                    role = name.upper()
                    columns = [c for c in self.project.sheet.columns if c.role == role]
                    if len(columns) != 1:
                        raise KeyError(f"Role {role} requires exactly one column")
                    column = columns[0]
                else:
                    column = getattr(self.project.sheet, name)
            except (KeyError, AttributeError):
                if name in builtins.__dict__:
                    return builtins.__dict__[name]
                raise
            id = column.id
        self.project._references[token] = id
        return Column(self.project, id)


def formula(backend, source, worksheet, rows, bindings, cancelled=lambda: False, timeout_s=2, limit=0):
    """Evaluate once on a native immutable view; record only columns read."""
    reads, references = {}, {}
    project = Project(backend, worksheet, bindings, reads, references)
    env = FormulaGlobals(project, rows)
    token = current_project.set(project)
    try:
        tree = ast.parse(source)
        # Bind resolvable names in inactive branches without reading their data.
        # Dependencies below still contain only arrays actually consumed.
        for node in ast.walk(tree):
            if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load) and node.id not in env:
                try:
                    env.__missing__(node.id)
                except (KeyError, AttributeError):
                    pass
            elif isinstance(node, (ast.Subscript, ast.Attribute)):
                root = node
                while isinstance(root, (ast.Subscript, ast.Attribute)):
                    root = root.value
                if isinstance(root, ast.Name) and root.id in ("sheets", "books", "sheet"):
                    try:
                        _safe_object(ast.unparse(node), env)
                    except (KeyError, AttributeError, ValueError, TypeError):
                        pass
        with Budget(timeout_s, cancelled):
            with np.errstate(all="raise"):
                try:
                    code = compile(source, "<PlotLab formula>", "eval")
                except SyntaxError:
                    wrapped = "def __formula__():\n" + "\n".join("    " + line for line in source.splitlines()) + "\n    return result\n"
                    exec(compile(wrapped, "<PlotLab formula>", "exec"), env)
                    result = env["__formula__"]()
                else:
                    result = eval(code, env)
                values = np.asarray(result, dtype=float)
                if values.ndim == 0:
                    values = np.full(rows, values.item())
                if values.ndim != 1 or len(values) != rows:
                    raise ValueError(f"Expected {rows} results, got shape {values.shape}")
        bad = np.flatnonzero(np.isinf(values))
        if len(bad):
            raise PythonError(f"Nonfinite result at row {int(bad[0]) + 1}", row=int(bad[0]) + 1)
        return (values[:limit] if limit else values).tolist(), reads, {**bindings, **references}
    except FloatingPointError as e:
        # NumPy reports a vector domain error without a sample. Re-evaluate
        # with warnings disabled solely to locate the first invalid row.
        try:
            with Budget(timeout_s, cancelled), np.errstate(all="ignore"):
                value = env["__formula__"]() if "__formula__" in env else eval(code, env)
                invalid = ~np.isfinite(np.asarray(value, dtype=float))
                if invalid.ndim == 1 and len(invalid) == rows:
                    missing = np.zeros(rows, dtype=bool)
                    for id in reads.values():
                        values = np.asarray(backend.values(id))
                        if backend.info(id)["parent"] == worksheet and len(values) == rows:
                            missing |= np.isnan(values)
                    invalid &= ~missing
                bad = np.flatnonzero(invalid)
            row = int(bad[0]) + 1 if len(bad) else 1
        except Exception:
            row = 1
        raise PythonError(f"{e} at row {row}", row=row) from e
    except Exception as e:
        if "row " in str(e):
            raise
        line = getattr(e, "lineno", None)
        tb = e.__traceback__
        while tb:
            if tb.tb_frame.f_code.co_filename == "<PlotLab formula>":
                line = tb.tb_lineno
            tb = tb.tb_next
        raise PythonError(f"{e}" + (f" at line {line}" if line else " at row 1"), line=line or 0, row=0 if line else 1) from e
    finally:
        current_project.reset(token)


def _safe_object(expression, env):
    node = ast.parse(expression, mode="eval").body
    def resolve(n):
        if isinstance(n, ast.Name):
            return env[n.id]
        if isinstance(n, ast.Attribute):
            if n.attr.startswith("_"):
                raise ValueError("Private completion")
            return getattr(resolve(n.value), n.attr)
        if isinstance(n, ast.Subscript):
            return resolve(n.value)[ast.literal_eval(n.slice)]
        raise ValueError("Completion never executes calls")
    return resolve(node)


def complete(backend, source, active_sheet=0, console=0):
    """Safe API metadata completion: (insert, signature, short doc) tuples."""
    project = Project(backend, active_sheet)
    env = namespace(project)
    if console in _consoles:
        from .worksheet import Object, Collection
        for name, obj in _consoles[console][1].items():
            if isinstance(obj, Object):
                try:
                    env[name] = project._wrap(obj.id)
                except (KeyError, IndexError):
                    pass
            elif isinstance(obj, Collection):
                env[name] = Collection(project, obj._kind, obj._parent)
            elif isinstance(obj, (int, float, str, np.ndarray)):
                env[name] = obj
    env.update({word: getattr(builtins, word) for word in ("range", "len", "abs", "min", "max", "sum", "print")})
    if len(project.sheets):
        for col in project.sheet.columns:
            for name in (col.name, col.long_name):
                if name:
                    env[identifier(name)] = col
    # Resolve only simple previous assignments; never evaluate user code.
    for match in re.finditer(r"(?m)^\s*(\w+)\s*=\s*([^\n]+)", source):
        try:
            env[match[1]] = _safe_object(match[2], env)
        except Exception:
            pass
    tail = source.splitlines()[-1] if source else ""
    match = re.search(r"([\w.\[\]'\" \-]+)\.([\w]*)$", tail)
    bracket = re.search(r"(\w+(?:\[[^\]]+\])?)\[(?:['\"]([^'\"]*))?$", tail)
    try:
        if match:
            obj = _safe_object(match[1].strip(), env)
            prefix = match[2]
            names = {name: None for name in dir(obj) if not name.startswith("_")}
            for name in names:
                try:
                    names[name] = getattr(obj, name)
                except Exception:
                    pass
        elif bracket:
            obj = _safe_object(bracket[1], env)
            prefix = bracket[2] or ""
            names = {o.name: o for o in obj}
        else:
            prefix_match = re.search(r"\w*$", tail)
            prefix = prefix_match[0]
            names = dict(env)
            names.update({word: None for word in keyword.kwlist})
    except Exception:
        return []
    result = []
    for name, obj in sorted(names.items()):
        if not name.startswith(prefix) or name.startswith("_"):
            continue
        signature, doc = "", ""
        if callable(obj):
            try:
                signature = str(inspect.signature(obj))
            except (ValueError, TypeError):
                if isinstance(obj, np.ufunc):
                    signature = "(" + ", ".join(f"x{n+1}" for n in range(obj.nin)) + ", /, out=None, **kwargs)"
        documentation = inspect.getdoc(obj) if obj is not None else "Python keyword"
        if documentation:
            doc = documentation.splitlines()[0][:200]
        insert = name if not bracket else repr(name) + "]" if bracket[2] is None else name + tail[tail.rfind("[") + 1:][:1] + "]"
        result.append((insert, signature, doc))
        if len(result) >= 100:
            break
    return result


def _trust_key(path):
    path = Path(path).resolve()
    return os.path.normcase(str(path)), hashlib.sha256(path.read_bytes()).hexdigest()


def _trust_path():
    override = os.environ.get("SST_PLOTLAB_TRUST_FILE")
    return Path(override) if override else Path(os.environ["LOCALAPPDATA"]) / "SemiconductorStudio/PlotLab/trusted-projects.json"


def is_trusted(path):
    try:
        name, digest = _trust_key(path)
        return json.loads(_trust_path().read_text(encoding="utf-8")).get(name) == digest
    except (OSError, ValueError, KeyError):
        return False


def remember_trust(path):
    name, digest = _trust_key(path)
    destination = _trust_path()
    try:
        settings = json.loads(destination.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        settings = {}
    settings[name] = digest
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".tmp")
    temporary.write_text(json.dumps(settings, indent=2), encoding="utf-8")
    temporary.replace(destination)
