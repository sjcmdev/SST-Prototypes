"""Worksheet objects backed by stable native IDs and Core commands.

Arrays returned by NumPy are read-only snapshots. Assign through a column to
record edits and preserve undo. Row slices retain row IDs through sorting.
"""
from __future__ import annotations

import difflib
from contextvars import ContextVar
import keyword
import re
import numpy as np
from numpy.lib.mixins import NDArrayOperatorsMixin

current_project = ContextVar("plotlab_project", default=None)


class RunResult(dict):
    """JSON-serializable result with attribute access for interactive use."""
    __getattr__ = dict.__getitem__


def run_result(result):
    return RunResult(ok=result.ok, output=result.output, error=result.error,
                     line=result.line, row=result.row, error_code=result.error_code)


class LookupError(KeyError):
    code = "lookup"


def identifier(name):
    # Hyphens in measurement names join letters: I-V 300 K -> IV_300_K.
    name = re.sub(r"(?<=\w)-(?=\w)", "", name)
    name = re.sub(r"\W+", "_", name).strip("_")
    if name[:1].isdigit():
        name = "_" + name
    return name + "_" if keyword.iskeyword(name) else name


class Project:
    """A live native document. Use run(code) for one atomic, undoable script."""
    def __init__(self, backend, active=0, bindings=None, reads=None, references=None):
        self._backend = backend
        self._identity = backend.project_id
        self._active = active
        self._bindings = {} if bindings is None else bindings
        self._reads = {} if reads is None else reads
        self._references = {} if references is None else references

    @property
    def _native(self):
        if self._backend is None or self._backend.project_id != self._identity:
            raise RuntimeError("This worksheet reference belongs to a closed or replaced project")
        return self._backend

    @_native.setter
    def _native(self, backend):
        self._backend = backend

    @property
    def books(self):
        return Collection(self, 1)

    @property
    def sheets(self):
        return Collection(self, 2)

    @property
    def scripts(self):
        return Collection(self, 7)

    @property
    def sheet(self):
        active = self._active or self._native.active_sheet
        if not active:
            ids = self.sheets._ids()
            if not ids:
                raise LookupError("No worksheet; use sheets.new(name)")
            active = ids[0]
        return Sheet(self, active)

    def run(self, source, *, timeout_s=5, parameters=None):
        """Run Python off the owner thread; one undo step, rollback on error."""
        return run_result(self._native.run_python(source, self.sheet.id if len(self.sheets) else 0, timeout_s, parameters or {}))

    def run_script(self, script, *, parameters=None, timeout_s=5):
        """Run a saved script by stable ID or name, with parameter overrides."""
        if isinstance(script, int):
            script = self.by_id(script)
        elif isinstance(script, str):
            script = self.scripts[script]
        return self.run(script._info["source"], parameters={**script._info["parameters"], **(parameters or {})}, timeout_s=timeout_s)

    def undo(self):
        self._native.undo()

    def redo(self):
        self._native.redo()

    def by_id(self, id):
        """Resolve a stable ID (also used by History as Python)."""
        return self._wrap(id)

    def _wrap(self, id):
        kind = self._native.info(id)["type"]
        return {1: Book, 2: Sheet, 3: Column, 7: Script}.get(kind, Object)(self, id)


class Object:
    def __init__(self, project, id):
        self._project, self.id = project, id

    @property
    def _info(self):
        return self._project._native.info(self.id)

    @property
    def name(self):
        return self._info["name"]

    @name.setter
    def name(self, name):
        self._project._native.rename(self.id, str(name))

    @property
    def long_name(self):
        return self._info.get("long_name", "")

    @long_name.setter
    def long_name(self, value):
        self._project._native.metadata(self.id, "long_name", str(value))

    @property
    def short_name(self):
        return self.name

    @short_name.setter
    def short_name(self, name):
        self.name = name

    def __repr__(self):
        return f"{type(self).__name__}({self.name!r}, id={self.id})"


class Collection:
    def __init__(self, project, kind, parent=0):
        self._project, self._kind, self._parent = project, kind, parent

    def _ids(self):
        return self._project._native.ids(self._kind, self._parent)

    def __len__(self):
        return len(self._ids())

    def __iter__(self):
        return (self._project._wrap(id) for id in self._ids())

    def _lookup(self, key, attribute=False):
        ids = self._ids()
        indexed = isinstance(key, (int, np.integer))
        token = f"{self._kind}:{self._parent}:{'index' if indexed else 'attr' if attribute else 'name'}:{key}"
        bound = self._project._bindings.get(token)
        if bound is not None:
            try:
                kind = self._project._native.info(bound)["type"]
            except (KeyError, IndexError):
                kind = -1
            if kind != self._kind:
                raise LookupError(f"Missing referenced object ID {bound}: {key}")
            self._project._references[token] = bound
            return bound
        if indexed:
            id = ids[key]
            self._project._references[token] = id
            return id
        matches, aliases, candidates = [], [], []
        for id in ids:
            info = self._project._native.info(id)
            names = [info["name"], info.get("long_name", "")]
            if self._kind == 3:
                ordinal = ids.index(id) + 1
                letters = ""
                while ordinal:
                    ordinal, remainder = divmod(ordinal - 1, 26)
                    letters = chr(65 + remainder) + letters
                if key == letters:
                    aliases.append(id)
            candidates.extend(n for n in names if n)
            if key in ([identifier(n) for n in names if n] if attribute else names):
                matches.append(id)
        if not matches:
            matches = aliases
        if len(matches) > 1:
            labels = []
            for id in matches:
                info = self._project._native.info(id)
                parent = self._project._native.info(info["parent"])["name"]
                labels.append(f"{parent}/{info['name']} (id={id})")
            raise LookupError(f"Ambiguous {key!r}: " + ", ".join(labels))
        if not matches:
            close = difflib.get_close_matches(str(key), candidates, n=3)
            hint = f"; did you mean {', '.join(repr(n) for n in close)}?" if close else ""
            raise LookupError(f"Unknown name {key!r}{hint}")
        self._project._references[token] = matches[0]
        return matches[0]

    def __getitem__(self, key):
        if isinstance(key, slice):
            return [self._project._wrap(id) for id in self._ids()[key]]
        return self._project._wrap(self._lookup(key))

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        try:
            return self._project._wrap(self._lookup(name, True))
        except LookupError as e:
            raise AttributeError(str(e)) from e

    def __setitem__(self, key, values):
        if self._kind != 3:
            raise TypeError("Assign values through a worksheet column")
        self[key][:] = values

    def __dir__(self):
        return sorted(set(super().__dir__()) | {
            identifier(name) for obj in self for name in (obj.name, obj._info.get("long_name", "")) if name
        })

    def new(self, name, df=None):
        """Create a workbook or worksheet, optionally populated from pandas."""
        if self._kind == 7:
            return self._project._wrap(self._project._native.save_script(str(name), str(df or ""), 0))
        if self._kind not in (1, 2):
            raise TypeError("Use sheet.add_column(name, values)")
        native = self._project._native
        native.begin("New worksheet" if self._kind == 2 else "New workbook")
        try:
            parent = self._parent
            if not parent:
                if self._kind == 2:
                    books = self._project.books._ids()
                    parent = books[0] if books else native.create(0, 1, "Book1")
            obj = self._project._wrap(native.create(parent, self._kind, str(name)))
            if df is not None:
                for label in df.columns:
                    array = df[label].to_numpy()
                    obj.add_column(str(label), array, role="None" if array.dtype.kind in "OUS" else "Y")
            native.end(True)
            return obj
        except BaseException:
            native.end(False)
            raise


class Script(Object):
    @property
    def source(self):
        return self._info["source"]

    @source.setter
    def source(self, source):
        self._project._native.save_script(self.name, str(source), self.id)

    @property
    def parameters(self):
        return self._info["parameters"]

    @parameters.setter
    def parameters(self, values):
        self._project._native.set_script_parameters(self.id, values)

    def run(self, *, parameters=None, timeout_s=5):
        return self._project.run_script(self.id, parameters=parameters, timeout_s=timeout_s)


class Book(Object):
    @property
    def sheets(self):
        return Collection(self._project, 2, self.id)

    def __getitem__(self, key):
        return self.sheets[key]

    def __getattr__(self, name):
        return getattr(self.sheets, name)


class Sheet(Object):
    def __dir__(self):
        return sorted(set(super().__dir__()) | {
            identifier(name) for col in self.columns for name in (col.name, col.long_name) if name
        })

    @property
    def columns(self):
        return Collection(self._project, 3, self.id)

    @property
    def rows(self):
        return Rows(self)

    def __getitem__(self, key):
        return self.columns[key]

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        return getattr(self.columns, name)

    def __setitem__(self, key, values):
        try:
            column = self.columns[key]
        except LookupError as e:
            if isinstance(key, str) and str(e).find("Unknown name") >= 0:
                self.add_column(key, values)
                return
            raise
        column[:] = values

    def __delitem__(self, key):
        self._project._native.delete(self[key].id)

    def add_column(self, name, values=(), *, units="", role="Y"):
        """Add a named column; extend worksheet rows without truncating data."""
        native = self._project._native
        native.begin("Add column")
        try:
            array = np.asarray(values)
            if array.ndim != 1:
                raise ValueError("Column values must be one-dimensional")
            column = Column(self._project, native.add_column(self.id, str(name), array.dtype.kind in "OUS"))
            column[:] = array
            column.units = units
            column.role = role
            native.end(True)
            return column
        except BaseException:
            native.end(False)
            raise

    def sort(self, column, *, descending=False):
        """Sort whole rows by a column, keeping masks and row identities."""
        id = column.id if isinstance(column, Column) else self[column].id
        self._project._native.sort(self.id, id, bool(descending))

    def to_pandas(self):
        import pandas as pd
        return pd.DataFrame({c.long_name or c.name: np.asarray(c) for c in self.columns})


class Column(Object, NDArrayOperatorsMixin):
    __array_priority__ = 1000

    def __array__(self, dtype=None, copy=None):
        self._project._reads[f"@{self.id}"] = self.id
        array = np.asarray(self._project._native.values(self.id), dtype=dtype)
        return array.copy() if copy else array

    def __array_ufunc__(self, ufunc, method, *inputs, **kwargs):
        if kwargs.get("out") is not None:
            raise TypeError("Assign the result to a column to preserve undo")
        return getattr(ufunc, method)(*(np.asarray(v) if isinstance(v, Column) else v for v in inputs), **kwargs)

    def __len__(self):
        return len(np.asarray(self))

    def __getitem__(self, key):
        return np.asarray(self)[key]

    def __setitem__(self, key, values):
        native = self._project._native
        if isinstance(key, slice) and key == slice(None):
            array = np.asarray(values)
            if array.ndim != 1:
                raise ValueError("Column values must be one-dimensional")
            native.set_values(self.id, array.tolist())
        elif isinstance(key, (int, np.integer)):
            index = int(key)
            if index < 0:
                index += len(self)
            if index < 0 or index >= len(self):
                raise IndexError("Column row index out of range")
            native.set_cells(self.id, index, [values])
        else:
            array = np.asarray(self).copy()
            array[key] = values
            native.set_values(self.id, array.tolist())

    @property
    def mask(self):
        return Mask(self)

    @property
    def valid(self):
        values = np.asarray(self)
        valid = ~np.asarray(self.mask)
        if values.dtype.kind in "fciu":
            valid &= np.isfinite(values)
        return values[valid]

    def max(self, *args, **kwargs):
        return self.valid.max(*args, **kwargs)

    def min(self, *args, **kwargs):
        return self.valid.min(*args, **kwargs)

    def mean(self, *args, **kwargs):
        return self.valid.mean(*args, **kwargs)

    def set_values(self, expression, *, mode="python", recalculate="Auto"):
        """Set a Python or legacy col(A) formula; stable reads drive recalc."""
        return run_result(self._project._native.set_formula(self.id, expression, mode, recalculate))


def _metadata(name):
    def get(self):
        return self._info[name]
    def set(self, value):
        self._project._native.metadata(self.id, name, str(value))
    return property(get, set)


for _name in ("units", "long_name", "comments", "role"):
    setattr(Column, _name, _metadata(_name))


class Mask:
    def __init__(self, column):
        self.column = column

    def __array__(self, dtype=None, copy=None):
        self.column._project._reads[f"@{self.column.id}"] = self.column.id
        return np.array(self.column._project._native.masks(self.column.id), dtype=dtype or bool)

    def __getitem__(self, key):
        return np.asarray(self)[key]

    def __setitem__(self, key, value):
        array = np.asarray(self)
        array[key] = value
        self.column._project._native.set_masks(self.column.id, array.tolist())


class Rows:
    def __init__(self, sheet, ids=None):
        self.sheet, self._row_ids = sheet, ids

    def _ids(self):
        return self.sheet._project._native.rows(self.sheet.id) if self._row_ids is None else self._row_ids

    def __len__(self):
        return len(self._ids())

    def __iter__(self):
        return (Row(self.sheet, id) for id in self._ids())

    def __getitem__(self, key):
        ids = self._ids()
        return Rows(self.sheet, ids[key]) if isinstance(key, slice) else Row(self.sheet, ids[key])

    def append(self, **values):
        """Append one row; names are resolved before any changes are made."""
        native = self.sheet._project._native
        columns = {self.sheet.columns._lookup(k, True): v for k, v in values.items()}
        native.begin("Append row")
        try:
            native.grow(self.sheet.id, len(self.sheet.rows) + 1)
            row = self.sheet.rows[-1]
            for id, value in columns.items():
                row._set(Column(self.sheet._project, id), value)
            native.end(True)
        except BaseException:
            native.end(False)
            raise


class Row:
    def __init__(self, sheet, id):
        object.__setattr__(self, "sheet", sheet)
        object.__setattr__(self, "id", id)

    def _index(self):
        return self.sheet._project._native.rows(self.sheet.id).index(self.id)

    def __getitem__(self, key):
        return self.sheet[key][self._index()]

    def __getattr__(self, name):
        return getattr(self.sheet, name)[self._index()]

    def _set(self, column, value):
        column[self._index()] = value

    def __setitem__(self, key, value):
        self._set(self.sheet[key], value)

    def __setattr__(self, name, value):
        self._set(getattr(self.sheet, name), value)


def plot(x, y, *, kind="line+symbol"):
    """Plot worksheet columns through the shared Core command and undo path."""
    if not isinstance(x, Column) or not isinstance(y, Column) or x._project._native is not y._project._native:
        raise ValueError("plot requires columns from the same project")
    kinds = {"line": 0, "scatter": 1, "line+symbol": 2, "column": 8, "bar": 9,
             "stacked-column": 10, "percent-column": 11, "stacked-bar": 12, "percent-bar": 13,
             "histogram": 14, "box": 15, "area": 16, "waterfall": 17, "error-band": 18}
    result = x._project._native.plot_columns(y._info["parent"], [y.id], kinds[kind], 0, x.id, 0)
    if not result["ok"]:
        raise ValueError(result["error"]["message"])
    return Object(x._project, result["graph_id"])


def namespace(project):
    return dict(project=project, books=project.books, sheets=project.sheets,
                sheet=project.sheet if len(project.sheets) else None, np=np, plot=plot)
