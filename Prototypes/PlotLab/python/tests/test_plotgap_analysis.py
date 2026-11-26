"""Analysis edge contract. Native numerical/parity oracles live in AnalysisJobTests."""
import json
import numpy as np
import pytest
from sst_plotlab.analysis import AnalysisError, AnalysisSession, NamedModel, load_recipe, save_recipe


def recipe():
    return dict(id="analysis-17", version=1, worksheet_id=12, x_column_id=13, y_column_id=14,
                settings=dict(operation="exponential", model_version=1, weights="absolute_sigma",
                              x_from_x_units=0.1, x_to_x_units=8,
                              parameters=[dict(name="tau", units="s", start_parameter_units=1.7,
                                               lower_parameter_units=.01, upper_parameter_units=10, fixed=False)]),
                view=dict(show_residuals=True, auto_preview=True))


class Backend:
    """Protocol spy, deliberately contains no numerical fitter."""
    def __init__(self):
        self.calls = []

    def analysis_submit(self, request, callback, preview):
        self.calls.append(("submit", request, callback, preview))
        return dict(ok=True, result=dict(job_id=99))

    def analysis_wait(self, job_id):
        self.calls.append(("wait", job_id))
        return dict(ok=True, result=dict(job_id=job_id, items=[dict(id="analysis-17", model_id="exponential", parameters=[.3, 4, 1.7])]))

    def analysis_accept(self, job_id, folder_id):
        self.calls.append(("accept", job_id, folder_id))
        return dict(ok=True, result=dict(worksheet_ids=[101]))

    def analysis_status(self, job_id):
        return dict(ok=False, error=dict(type="stale", message="Source changed"))

    def analysis_cancel(self, job_id):
        return dict(ok=True, result=dict(job_id=job_id, state="cancelled"))


def test_recipe_save_reopen_preserves_ids_parameters_range_and_view(tmp_path):
    path = tmp_path / "recipe.json"
    original = recipe()
    save_recipe(path, original)
    assert load_recipe(path) == original
    assert json.loads(path.read_text(encoding="utf-8")) == original
    assert not list(tmp_path.glob("tmp*"))
    original["settings"]["tolerance"] = float("nan")
    with pytest.raises(ValueError):
        save_recipe(path, original)
    assert load_recipe(path) == recipe()


def test_python_passes_recipe_to_native_command_job_only():
    backend = Backend()
    session = AnalysisSession(backend)
    result = session.run(recipe(), accept=True)
    assert result["items"][0]["parameters"] == [.3, 4, 1.7]
    assert backend.calls == [("submit", recipe(), None, False), ("wait", 99), ("accept", 99, 0)]
    with pytest.raises(AnalysisError, match="Source changed") as error:
        session.status(99)
    assert error.value.code == "stale"
    assert session.cancel(99)["state"] == "cancelled"


def test_async_preview_and_batch_are_native_requests():
    backend = Backend()
    session = AnalysisSession(backend)
    assert session.run(recipe(), wait=False, preview=True) == {"job_id": 99}
    assert backend.calls[-1][3] is True
    items = [dict(id="sample-a", recipe=recipe()), dict(id="sample-b", recipe=recipe())]
    session.batch(items)
    assert backend.calls[-2][1] == {"items": items}


def test_user_callable_is_not_executed_by_submit_or_recipe_load(tmp_path):
    calls = []
    def line(x, *, offset, gain):
        calls.append(True)
        return offset + gain * x
    model = NamedModel("owner.line", 1, "s", "V", ("offset", "gain"), line)
    backend = Backend()
    AnalysisSession(backend).run(recipe(), model=model, wait=False)
    assert not calls
    request, callback = backend.calls[0][1:3]
    assert request["user_model"]["id"] == "owner.line"
    np.testing.assert_allclose(callback([0, 1, 3], [2, 4]), [2, 6, 14])
    assert calls == [True]
    path = tmp_path / "recipe.json"
    request["settings"]["source"] = "raise RuntimeError('must not execute')"
    save_recipe(path, request)
    load_recipe(path)
    assert calls == [True]


def test_callable_shape_domain_and_parameter_errors():
    model = NamedModel("bad", 1, "s", "V", ("a",), lambda x, a: [float("nan")] * len(x))
    with pytest.raises(ValueError, match="finite"):
        model.evaluate([1, 2], [1])
    with pytest.raises(ValueError, match="count"):
        model.evaluate([1], [])
    with pytest.raises(ValueError, match="unique"):
        NamedModel("bad", 1, "s", "V", ("a", "a"), lambda: None).descriptor()


@pytest.mark.parametrize("field,value", [("id", ""), ("version", 2), ("version", True), ("worksheet_id", True), ("x_column_id", -1)])
def test_invalid_recipe_file_edge(tmp_path, field, value):
    value_recipe = recipe()
    value_recipe[field] = value
    with pytest.raises(ValueError):
        save_recipe(tmp_path / "recipe.json", value_recipe)
