"""Restricted before-formula assignments in the Core embedded interpreter.

Numeric scalars only; col(stable_id) exposes read-only current-project data.
No Python objects, imports, attributes, I/O, loops or user function calls enter
this interpreter. Work is bounded by the script length and AST node budget.
"""
import ast
import json
import math
import operator

_BINARY = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
           ast.Div: operator.truediv, ast.Mod: operator.mod}
_FUNCTIONS = {name: getattr(math, name) for name in
              ("sqrt", "exp", "log", "log10", "sin", "cos", "tan", "atan2")}
_FUNCTIONS.update(abs=abs, min=min, max=max, sum=sum)

def _tree(script):
    if len(script) > 16384:
        raise ValueError("Before formula script too long")
    tree = ast.parse(script)
    if len(list(ast.walk(tree))) > 1024:
        raise ValueError("Before formula script too complex")
    allowed = (ast.Module, ast.Assign, ast.Name, ast.Load, ast.Store, ast.Constant,
               ast.BinOp, ast.UnaryOp, ast.UAdd, ast.USub, ast.Call,
               *_BINARY.keys())
    for node in ast.walk(tree):
        if not isinstance(node, allowed):
            raise ValueError("Before formula disallows " + type(node).__name__)
        if isinstance(node, ast.Assign):
            if len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
                raise ValueError("Use scalar assignments: scale = mean(col(5))")
            if node.targets[0].id.startswith("_") or node.targets[0].id in (*_FUNCTIONS, "mean", "col", "pi", "e", "i", "nrows"):
                raise ValueError("Reserved script variable")
        if isinstance(node, ast.Call) and (not isinstance(node.func, ast.Name) or
                node.func.id not in (*_FUNCTIONS, "mean", "col") or node.keywords):
            raise ValueError("Only numeric library calls are allowed")
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "col":
            if len(node.args) != 1 or not isinstance(node.args[0], ast.Constant) or type(node.args[0].value) is not int or not 0 < node.args[0].value <= 2**64 - 1:
                raise ValueError("col requires a positive literal stable column ID")
        if isinstance(node, ast.Constant) and (type(node.value) not in (int, float) or
                not math.isfinite(node.value) or abs(node.value) > 1e308):
            raise ValueError("Only finite numeric constants are allowed")
    return tree

def declared(script):
    return [node.targets[0].id for node in _tree(script).body]

def inputs(script):
    return sorted({node.args[0].value for node in ast.walk(_tree(script))
                   if isinstance(node, ast.Call) and node.func.id == "col"})

def declared_json(script):
    return json.dumps(declared(script))

def inputs_json(script):
    return json.dumps(inputs(script))

def run(script, data_json):
    data = json.loads(data_json)
    variables = dict(pi=math.pi, e=math.e)
    def column(id):
        if type(id) not in (int, float) or int(id) != id or str(int(id)) not in data:
            raise ValueError("Missing project column ID")
        return tuple(x for x in data[str(int(id))] if x is not None and math.isfinite(x))
    def mean(values):
        return sum(values) / len(values) if values else math.nan
    functions = dict(_FUNCTIONS, col=column, mean=mean)
    def evaluate(node):
        if isinstance(node, ast.Constant):
            return float(node.value)
        if isinstance(node, ast.Name):
            return variables[node.id]
        if isinstance(node, ast.BinOp):
            a, b = evaluate(node.left), evaluate(node.right)
            if not isinstance(a, (float, int)) or not isinstance(b, (float, int)):
                raise ValueError("Arithmetic requires numeric scalars")
            return _BINARY[type(node.op)](a, b)
        if isinstance(node, ast.UnaryOp):
            value = evaluate(node.operand)
            if not isinstance(value, (float, int)):
                raise ValueError("Unary arithmetic requires a scalar")
            return -value if isinstance(node.op, ast.USub) else value
        if isinstance(node, ast.Call):
            if node.func.id == "col":
                return column(node.args[0].value)
            return functions[node.func.id](*(evaluate(arg) for arg in node.args))
        raise ValueError("Unsupported script expression")
    for statement in _tree(script).body:
        value = evaluate(statement.value)
        if type(value) not in (int, float) or not math.isfinite(value):
            raise ValueError("Before formula assignments must be finite numeric scalars")
        variables[statement.targets[0].id] = value
    return json.dumps({name: variables[name] for name in json.loads(declared_json(script))}, allow_nan=False)
