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

    operation=preview/import: path or paths, recursive=False, optional parser options
    and decimal ("." or ","). Preview returns sources with stable source_id (absolute
    UTF-8 path), root, preview and typed per-file error. Import commits all readable
    sources atomically; if any source fails, supply source_ids to explicitly select
    the files to commit. Filename metadata remains a suggestion. cancel=True or
    operation_id uses the same Core cancellation path as analysis.
    Workspace operations: new, activate (project_id), close (optional application),
    close_decision (decision=save/discard/cancel, explicit path for an unsaved
    project), workspace and save_as (path). Pass the previous response's workspace
    in request["workspace"] to retain open projects, active_project_id, paths,
    revisions, dirty state and recent_projects between calls. File save/open still
    use Core jobs. A failed close-save returns the unchanged open workspace.
    Undo history remains in the live C++ workspace, not in the JSON transport.

    Analysis results report one standard error from residual covariance:
    CV V_b_standard_error_V, N_standard_error_m3, x_d_fit.standard_error_m;
    raw DLCP group N_DL_standard_error_m3 and x_e_standard_error_m;
    Arrhenius E_a_standard_error_eV and prefactor_standard_error (prefactor_unit).
    Missing covariance is null; sigma_cm2 is the capture cross-section.
    Walter quantity is apparent DOS; historical_limits describes W1-W3 coverage.

    operation=walter: settings select measurement_id or measurement_ids, optional
    cv_fit_id / arrhenius_fit_id, model (default Walter1996-linear-field),
    zeta_s_1_K_2 or explicit compat B_s_1/emission_source, V_b_V/E_F_eV,
    parameter_source for manual inputs, x_d source/policy and ranges with units.
    operation=integrate_saved_walter: settings={sample_id, optional
    integration_range_eV}; replays the frozen historical slice, not the GUI.
    Both commit through the same Core analysis command/job/Undo as the GUI.

    operation: edit_circuit (edit: create/replace/delete), simulate or fit
    (settings: measurement_id, circuit_id, optional confirmed temperature_K/bias_V).
    Model-only simulate omits measurement_id and supplies temperature_K, bias_V,
    and frequency_Hz (list) or frequency_sweep {start_Hz, stop_Hz, points_per_decade}.
    Same-sample measurements overlay on their original grids; no residuals are invented.
    edit_circuit/create can supply sample_name instead of sample_id for a new sample.
    fit settings add parameters [{element, field, unit, free, min, max}],
    Z_floor_Ohm, optional max_iterations, score_tolerance, frequency_min_Hz,
    frequency_max_Hz and sigma_Ohm per original measurement row.
    fit returns draft={base_project, project}; accepted project stays unchanged.
    accept_fit with that draft commits via Core Undo; stale drafts are rejected.
    An optional caller-supplied operation_id permits another thread to execute
    {operation: "cancel", operation_id: id} while execute() runs (GIL released).
    cancel=True requests cancellation before queued work and preserves the project.
    Known measurement T/bias take precedence over confirmations.
    """
    import json
    import sst_caplab
    return json.loads(sst_caplab.execute(json.dumps(project, allow_nan=False),
                                         json.dumps(request, allow_nan=False)))
