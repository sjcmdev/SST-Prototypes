"""Laboratory AS import; no document or UI mutation."""
import json
from .importers import import_preview, parse_measurement, provenance as _source_provenance


def parse_lab_as(text, source="", decimal=".", cancelled=lambda: False, progress=lambda value: None):
    """Compatibility shape for the M1 bridge; parsing is shared with M2."""
    result = parse_measurement(text, source, 'lab_as_cp_rp', decimal, cancelled=cancelled, progress=progress)
    if result['quality']:
        raise ValueError('Nonfinite data or nonpositive f/Rp')
    return result


def preview_json(text, source, decimal, cancelled=lambda: False, progress=lambda value: None, options_json="", provenance_only=False):
    if cancelled():
        raise RuntimeError('Import cancelled')
    if provenance_only:
        result = _source_provenance(text, source, '')
    elif options_json:
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
    preview returns groups by target_sample_id, T/f/bias/type with source/block indices.
    target_sample_name creates one new target for the selected batch, with one Undo
    entry; use target_sample_id for an existing target or omit both for per-source samples.
    format_profile selects a named profile. profile(action=save/load/delete, name,
    options, decimal) stores PlotLab-Engine ImportOptions in the project; the app
    also persists named formats in its configuration directory.
    options.metadata_rules maps sample_name/treatment/temperature_K/frequency_Hz/
    bias_V to patterns such as {stem_0}, {folder_1} or custom variable names.
    custom_variables and variable_delimiter use JFM's shared token/slice rules.
    These are candidates; headers win. Explicit options confirm a chosen value;
    confirm_metadata_conflicts=True acknowledges conflicting candidates.
    reimport(measurement_id, optional path) and relink(measurement_id, path) return
    source_draft={base_project, project, diff, provenance_only}. accept_source(draft)
    is the explicit commit, using the same Undo command as GUI. Re-import retains
    measurement/column IDs and invalidates dependent results. A changed grid
    disables approved defect points pending reassignment. Relink requires the
    original SHA-256 and changes provenance only, preserving numerical results.
    Workspace operations: new, activate (project_id), close (optional application),
    close_decision (decision=save/discard/cancel, explicit path for an unsaved
    project), workspace and save_as (path). Pass the previous response's workspace
    in request["workspace"] to retain open projects, active_project_id, paths,
    revisions, dirty state and recent_projects between calls. File save/open still
    use Core jobs. A failed close-save returns the unchanged open workspace.
    Undo history remains in the live C++ workspace, not in the JSON transport.

    operation=plot: measurement_ids plus plot_type (C-V, Mott-Schottky, N_CV(W),
    N_DLCP(W), C-f, G/omega-f, Nyquist, Bode magnitude, Bode phase), or graph_ids
    from existing analyses (including Arrhenius defects and Walter spectra).
    analysis_ids with plot_index (default 0) select the same result plot across
    several analyses. graph_ids allow individual plot choices.
    Optional name; returns graph_id. Uses stored SI columns/results without a fit.
    source_colors maps measurement/analysis graph ID strings to RGBA arrays in
    [0, 1]; stored curve styles retain these colours through save and export.
    AS C(f,T) / AS G/omega(f,T) take a temperature series from one sample/bias,
    view_kind=lines/surface/heatmap, grid_policy=measured (default) or explicit
    interpolate_log_f with increasing frequency_Hz. Unequal measured grids use
    per-temperature 3D lines; no automatic interpolation or extrapolation.
    Axes must describe the same quantities and scales. Missing/duplicate IDs,
    missing area for Mott-Schottky or incompatible axes return plot_failed.
    action=view with graph_id accepts the shared view JSON (ranges in the axis
    SI units, visibility keyed by stable plot ID strings, x_ticks/y_ticks) and/or
    x_scale/y_scale (linear/log10). View changes use the same job and Undo path.
    Spatial views also retain camera; colormap and color_range_F (or null for
    automatic limits) set the shared surface/heatmap colour scale.
    z_range_F (two increasing finite limits, or null for automatic) controls the
    3D Z range; set both ranges to [0, maximum_F] to include zero explicitly.
    operation=select, selection={kind: graph, id: graph_id} selects a saved graph.
    Export accepts graphs=[{graph_id: ...}, ...] or graph_id for one figure;
    saved views supply ranges/visibility/ticks unless explicitly overridden.

    Analysis results report one standard error from residual covariance:
    CV V_b_standard_error_V, N_standard_error_m3, x_d_fit.standard_error_m;
    raw DLCP group N_DL_standard_error_m3 and x_e_standard_error_m;
    Arrhenius E_a_standard_error_eV and prefactor_standard_error (prefactor_unit).
    Missing covariance is null; sigma_cm2 is the capture cross-section.
    Walter quantity is apparent DOS; historical_limits describes W1-W3 coverage.

    analyze settings may include preview=True to return an uncommitted draft;
    accept_preview(draft) uses the same acceptance command as the GUI.

    operation=tree: edit={action, ...}. Actions: create_sample(name),
    rename_sample(sample_id, name), duplicate_sample(sample_id, name),
    delete_sample(sample_id), sample_metadata(sample_id, optional area_m2/material/notes),
    move_measurements(measurement_ids, target_sample_id), merge_samples(sample_id,
    target_sample_id), delete_analysis(analysis_id), delete_circuit(circuit_id).
    merge_samples moves all measurements, defects and circuits; source metadata
    and historical/accepted results retain their identity. Every edit uses Core Undo.
    import accepts target_sample_id (0 creates samples); historical bundles retain
    their own samples. select accepts selection={kind, id}, where kind is sample,
    measurement, defect, analysis, circuit or clear (id=0). The response includes
    selection with stable IDs. No selection falls back to another object after deletion.

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
