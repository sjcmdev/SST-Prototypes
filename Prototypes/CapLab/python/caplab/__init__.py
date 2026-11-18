"""Laboratory AS import; no document or UI mutation."""
import json
from .importers import import_preview, parse_measurement


def parse_lab_as(text, source="", decimal=".", cancelled=lambda: False, progress=lambda value: None):
    """Compatibility shape for the M1 bridge; parsing is shared with M2."""
    result = parse_measurement(text, source, 'lab_as_cp_rp', decimal, cancelled=cancelled, progress=progress)
    if result['quality']:
        raise ValueError('Nonfinite data or nonpositive f/Rp')
    return result


def preview_json(text, source, decimal, cancelled=lambda: False, progress=lambda value: None, options_json=""):
    if options_json:
        result = import_preview(text, source, decimal, json.loads(options_json), cancelled, progress)
    else:
        result = parse_lab_as(text, source, decimal, cancelled, progress)
    return json.dumps(result, allow_nan=False)


def project_from_preview(preview, temperature=None):
    """Exercise the actual embedded C++ model and validation."""
    import sst_caplab
    return json.loads(sst_caplab.project_from_preview(json.dumps(preview), temperature))


def execute(project, request):
    """Headless Core command/job operation; JSON values in/out, typed error envelope.

    operation: edit_circuit (edit: create/replace/delete) or simulate
    (settings: measurement_id, circuit_id, optional confirmed temperature_K/bias_V).
    Known measurement T/bias take precedence over confirmations.
    """
    import json
    import sst_caplab
    return json.loads(sst_caplab.execute(json.dumps(project, allow_nan=False),
                                         json.dumps(request, allow_nan=False)))
