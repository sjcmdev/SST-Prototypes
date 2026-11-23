"""Headless PlotLab Core commands, worksheet operations and publication export."""
def __getattr__(name):
    if name == "export_graph":
        from .exporter import export_graph
        return export_graph
    raise AttributeError(name)


__all__ = ["export_graph"]


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
        self._session = (_sst_plotlab.active_document if active
                         else _sst_plotlab.WorksheetSession())

    def command(self, request):
        import json
        return json.loads(self._session.command_json(json.dumps(request, allow_nan=False)))

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
