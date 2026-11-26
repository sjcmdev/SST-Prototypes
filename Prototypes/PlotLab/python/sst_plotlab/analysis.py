"""Scientific analysis edge. The native backend owns Core commands/jobs and all numerics.

C registers analysis_submit(recipe, model_callable, preview), analysis_status(job_id),
analysis_wait(job_id), analysis_cancel(job_id), analysis_accept(job_id, folder_id).
Responses: {ok: bool, result: JSON-compatible value} or {ok: False, error: {type, message}}.
User callables are converted at the nanobind edge after the document's trust check.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import tempfile


class AnalysisError(RuntimeError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def evaluate_model(callback, x, parameters):
    """Binding edge: the existing runtime budget also covers explicit user models."""
    from .runtime import Budget
    with Budget(5, lambda: False):
        return callback(x, parameters)


def _load_model_file(path):
    import runpy
    from .runtime import Budget
    with Budget(5, lambda: False):
        namespace = runpy.run_path(str(path))
        model = namespace["model"]
        return model.descriptor(), model.evaluate, namespace["parameters"]


def _result(response):
    if not response.get("ok", False):
        error = response.get("error", {})
        raise AnalysisError(error.get("type", "analysis"), error.get("message", "Analysis failed"))
    return response["result"]


@dataclass(frozen=True)
class NamedModel:
    """Explicit metadata and named callable; never executes project source on load."""
    id: str
    version: int
    x_units: str
    y_units: str
    parameter_names: tuple[str, ...]
    function: object

    def descriptor(self):
        if not self.id or type(self.version) is not int or self.version != 1 or not callable(self.function):
            raise ValueError("Expected model ID, version 1 and an explicit callable")
        if not self.parameter_names or len(set(self.parameter_names)) != len(self.parameter_names):
            raise ValueError("User parameters must have unique names")
        if any(not name.isidentifier() for name in self.parameter_names):
            raise ValueError("User parameters must be named identifiers")
        return dict(id=self.id, version=self.version, x_units=self.x_units,
                    y_units=self.y_units, parameter_names=list(self.parameter_names))

    def evaluate(self, x, parameters):
        """Only the native binding invokes this, on its trusted runtime path."""
        import numpy as np
        if len(parameters) != len(self.parameter_names):
            raise ValueError("User parameter count does not match")
        values = np.asarray(self.function(np.asarray(x), **dict(zip(self.parameter_names, parameters))), dtype=float)
        if values.shape != (len(x),) or not np.isfinite(values).all():
            raise ValueError("User model must return one finite Y value per physical X")
        return values


class AnalysisSession:
    def __init__(self, backend):
        self._native = backend

    def load_model(self, path):
        """Explicit trusted file load; returns a JSON-compatible recipe with model settings."""
        return _result(self._native.analysis_load_model(str(path)))

    def run(self, recipe, *, model=None, wait=True, accept=False, preview=False):
        """Recipe uses stable *_id fields; ranges/anchors/parameters declare their units.

        Settings map directly to AnalysisSettings in C++; no Python fit fallback exists.
        Preview results cannot be accepted. One accept creates one undoable operation.
        """
        request = dict(recipe)
        callback = None
        if model is not None:
            request["user_model"] = model.descriptor()
            callback = model.evaluate
        job_id = _result(self._native.analysis_submit(request, callback, preview))["job_id"]
        if not wait:
            return {"job_id": job_id}
        result = _result(self._native.analysis_wait(job_id))
        if accept:
            self.accept(job_id)
        return result

    def batch(self, items, *, model=None, wait=True):
        """Items are {id, recipe}; mixed scientific failures stay per item."""
        request = {"items": list(items)}
        callback = None
        if model is not None:
            request["user_model"] = model.descriptor()
            callback = model.evaluate
        job_id = _result(self._native.analysis_submit(request, callback, False))["job_id"]
        return _result(self._native.analysis_wait(job_id)) if wait else {"job_id": job_id}

    def global_fit(self, recipe, datasets, *, model=None, wait=True, accept=False):
        """One simultaneous fit including the recipe's primary dataset.

        Additional datasets contain id, worksheet_id, x_column_id, y_column_id,
        optional sigma_column_id and parameters. Parameter shared=True means one
        common value; False means a local value per dataset. Local starts/bounds
        may differ; shared definitions must agree. All inputs use identical units.
        """
        return self.run(dict(recipe, global_inputs=list(datasets)), model=model,
                        wait=wait, accept=accept)

    def status(self, job_id, *, include_arrays=False):
        """Poll scalar status; request curve/covariance/peak arrays explicitly."""
        return _result(self._native.analysis_status(job_id, True) if include_arrays else self._native.analysis_status(job_id))

    def cancel(self, job_id):
        return _result(self._native.analysis_cancel(job_id))

    def accept(self, job_id, *, folder_id=0):
        return _result(self._native.analysis_accept(job_id, folder_id))


def _validate_recipe(value):
    if not isinstance(value, dict) or not isinstance(value.get("id"), str) or not value["id"] or type(value.get("version")) is not int or value["version"] != 1:
        raise ValueError("Expected a stable recipe ID and version 1")
    for key in ("worksheet_id", "x_column_id", "y_column_id"):
        if type(value.get(key)) is not int or value[key] <= 0:
            raise ValueError(f"Expected positive integer {key}")
    # This is the actual file edge. Reject accidental callable/nonfinite settings before replacing a file.
    json.dumps(value, allow_nan=False)
    return value


def save_recipe(path, recipe):
    """Atomic UTF-8 file edge, including model/version, settings, range and view state."""
    _validate_recipe(recipe)
    path = Path(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(recipe, stream, ensure_ascii=False, allow_nan=False, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def load_recipe(path):
    with Path(path).open(encoding="utf-8") as stream:
        return _validate_recipe(json.load(stream))
