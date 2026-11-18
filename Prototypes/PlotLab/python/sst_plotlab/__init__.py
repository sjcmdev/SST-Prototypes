"""Headless publication export from an owning PlotLab graph snapshot."""
def __getattr__(name):
    if name == "export_graph":
        from .exporter import export_graph
        return export_graph
    raise AttributeError(name)


__all__ = ["export_graph"]


class WorksheetSession:
    """Embedded worksheet commands with JSON-compatible dict inputs and outputs.

    Stable project IDs; zero-based row_index; date_time uses Unix seconds in UTC.
    Failures return {ok: False, error: {type, message}}.
    """

    def __init__(self, active=False):
        import _sst_plotlab
        self._session = (_sst_plotlab.active_document if active
                         else _sst_plotlab.WorksheetSession())

    def command(self, request):
        import json
        return json.loads(self._session.command_json(json.dumps(request, allow_nan=False)))


__all__.append("WorksheetSession")
