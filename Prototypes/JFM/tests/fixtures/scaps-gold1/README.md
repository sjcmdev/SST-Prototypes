# SCAPS golden fixtures (gold1)

Source: `SST-Experiments-gui-bridge/scaps-agent/datasets/CATALOG.md`, SCAPS 3.3.12.
`curves.npz` is a lossless copy of selected CSV columns, compressed with NumPy;
JFM phase-9 TSV files are byte-for-byte copies. No decimation or synthetic noise.
`reference.json` copies independent SCAPS extraction results; SHA256.json records
original source hashes and fixture hashes. No INVALID, quarantined or exploratory set.

JFM: phase-4 finite exported IV (post-export process crash is documented by catalog),
18 curves; phase-9 32 IV(T) TSVs, example cells without mesh convergence certification.
Phase-4 params have no area: tests explicitly declare 1 cm2 normalization. Phase-9
area 1e-4 m2 comes from jfm_normalization_area_m2. Convert specific resistance by
DIVIDING by area, and current density by MULTIPLYING by area.

CapLab: phase-3 defect C(f), phase-7 baseline/single/gaussian/double C(f,T), phase-8
uniform/step C(V), all 6 T/f combinations. G and Z are not golden observables;
zero conductance supplied to the AS C-only API is a placeholder, not measured G.
Phase 6 repeats phase 3: its reference is checked without double-counting data.
AS uses SCAPS branch windows and quadratic peak interpolation in ln(f), then
ln(omega/T2) Arrhenius. This is same-method replay, not automatic branch assignment.
Walter comparison uses baseline-subtracted C, saved width/Vbi and an explicit Ef=0 comparison. Native LinearField1996 has an energy-dependent
field factor; SCAPS used constant Vbi/(qW). Tests report both the native peak and
the peak after explicitly removing that factor, without hiding the DOS discrepancy.
