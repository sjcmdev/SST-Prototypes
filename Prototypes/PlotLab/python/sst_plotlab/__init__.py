"""Headless PlotLab Core commands, worksheet operations and publication export."""
def __getattr__(name):
    if name == "plot":
        from .worksheet import plot
        return plot
    if name in {"project", "books", "sheets", "sheet"}:
        import _sst_plotlab
        from .worksheet import Project, current_project
        project = current_project.get() or Project(_sst_plotlab.active_document)
        return project if name == "project" else getattr(project, name)
    if name == "export_graph":
        from .exporter import export_graph
        return export_graph
    raise AttributeError(name)


__all__ = ["export_graph", "project", "books", "sheets", "sheet", "plot"]


class WorksheetSession:
    """Embedded worksheet commands with JSON-compatible dict inputs and outputs.

    Stable project IDs; data-operation ranges are zero-based and half-open.
    Physical values use explicitly named *_column_units fields.
    Masked analysis samples are JSON null; date_time uses Unix seconds in UTC.
    Failures return {ok: False, error: {type, message}}.
    clear_cells takes worksheet_id, column_ids and optional stable row_ids;
    omitted/empty row_ids clears all visible rows in one Core undo group.
    """

    def __init__(self, active=False):
        import _sst_plotlab
        from .worksheet import current_project
        current = current_project.get()
        self._session = (current._native if active and current is not None else _sst_plotlab.active_document if active
                         else _sst_plotlab.WorksheetSession())

    @property
    def project(self):
        from .worksheet import Project
        return Project(self._session)

    @property
    def books(self):
        return self.project.books

    @property
    def sheets(self):
        return self.project.sheets

    @property
    def sheet(self):
        return self.project.sheet

    def run(self, source, *, timeout_s=5, parameters=None):
        return self.project.run(source, timeout_s=timeout_s, parameters=parameters)

    def console(self, source):
        from .worksheet import run_result
        return run_result(self._session.console(source, self.sheet.id if len(self.sheets) else 0))

    def complete(self, source, cursor=None, *, console=False):
        entries = self._session.complete(source if cursor is None else source[:cursor], self.sheet.id if len(self.sheets) else 0, console)
        return [dict(insert=insert, signature=signature, documentation=doc) for insert, signature, doc in entries]

    def command(self, request):
        import json
        return json.loads(self._session.command_json(json.dumps(request, allow_nan=False)))

    def import_preview(self, paths, recipe, *, wait=True):
        """Plan every full input without publication; names/roles/units are explicit.

        mapping: [{source, name, role (0..7), units, required, numeric}].
        hooks: stage -> {source, reference, version, numbers, strings}; see
        importing.STAGES. Hooks run only in preview, after explicit trust.
        Import commits that immutable preview after checking its source hash.
        """
        return self._session.import_preview([str(p) for p in paths], recipe, wait)

    def import_commit(self, *, wait=True):
        return self._session.import_commit(wait)

    def import_status(self):
        return self._session.import_status()

    def import_cancel(self):
        self._session.import_cancel()

    def trust_import_recipe(self, recipe):
        """Explicitly grant this exact recipe the worksheet Python permissions."""
        self._session.trust_import_recipe(recipe)

    def save_import_presets(self, path, presets):
        self._session.save_import_presets(str(path), presets)

    def load_import_presets(self, path):
        return self._session.load_import_presets(str(path))

    @property
    def import_recipes(self):
        """Immutable accepted recipe versions, keyed by stable fingerprint."""
        return self._session.import_recipes

    def plot_columns(self, worksheet_id, column_ids, kind=2, graph_id=0,
                     x_column_id=0, y_error_column_id=0):
        """Plot stable column IDs; kinds: 0 line, 1 scatter, 2 line+symbol, 8 column.

        graph_id=0 creates a graph; otherwise adds to its first layer.
        X defaults to the nearest designated X column or one-based row numbers.
        Returns {ok, graph_id, error}; uses the GUI's Core command/undo path.
        """
        return self._session.plot_columns(worksheet_id, column_ids, kind, graph_id,
                                          x_column_id, y_error_column_id)


__all__.append("WorksheetSession")
