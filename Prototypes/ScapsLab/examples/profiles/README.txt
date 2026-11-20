ScapsLab profile examples

All densities and file values use the SCAPS/Core SI convention.
Doping and defects: .grd position is relative x/d, values are m^-3.
Generation: .gen position is um, values are m^-3 s^-1.
Absorption: nm, m^-1. Spectrum: nm, W/m^2 per bin.
Filter: nm, percent. Optical capture: nm, m^2.
Editor previews convert position to nm and bulk density to cm^-3.
CSV exports use x_m and the parameter ID; .grd exports use absolute um and SI values.

Python Session.execute operations (same Core commands/jobs as the GUI):
load_profile: parameter_id, path; material/doping/composition also require layer_id.
Nt_m3 requires layer_id and defect_id. generation_m3_s uses the full device.
set_profile: same IDs and profile (Core law/fields); generation uses generation_m_m3s pairs.
preview_profile: same IDs, returns operation_result with x_m, value_SI, value_unit.
export_profile: same IDs and destination path (.csv or .grd).
load_material/save_material: layer_id, path, selection (A, B, both).
Read stable IDs from execute({"operation":"inspect"})["document"].

Loaded tables are embedded in .scapslab projects, retaining source names.
Profile changes use per-device Undo/Redo. Invalid input preserves the current document.
Core limitations still apply: shared ranges only for composition/NA/ND, no shared Nt.
