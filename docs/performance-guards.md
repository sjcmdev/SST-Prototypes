# Performance guards

The normal suite runs `tools/boundary_check.py`. It scans first-party C++ only,
excluding Vendor/build trees. Comments and string literals are not executable hits.
Rules cover JSON dump/parse, YAML construction/load/emit, data streams, string-to-number
conversion, packed results, `*_json` calls and Python/interpreter access.

`tools/boundary_allowlist.json` is an inventory, not a path exemption. Each entry pins
file + rule + SHA-256 of the normalized source line + occurrence count, and contains
the readable code and a reason. Moving a line does not require permission; adding,
changing or duplicating a hit fails even in a file with existing exceptions.
`known-debt` entries also require an audit ID (Fxx); they remain visible in every run.
Deleted entries can be pruned. No blanket directory/function exemption is supported.

To register a real edge, run `python tools/boundary_check.py --inventory`, inspect the
call flow, and add ONLY its exact entry under `edges` with a one-line reason naming
the file, Python binding, CLI, network/IPC or human-readable output boundary. A file
named Bindings does not make a GUI-to-Python JSON roundtrip legal. Use `known-debt`
with the audit finding for existing violations. New debt needs owner approval.
There is intentionally no automatic baseline/rebaseline switch.

The scanner is a lexical regression tripwire, not a C++ call graph or proof that a
pre-existing adapter is called only at an edge. Runtime probes below cover selected
real consumers. Alias/indirect calls and arbitrary hand-written serializers require
review. The checker self-test rejects a deliberate solver `Json::parse(x.dump())`,
comments/literals, and a duplicate allowed call. Run it with `--self-test`.

The identical script is checked in per repository so an independent checkout and its
normal suite do not depend on a sibling worktree or tools/agents. Synchronize changes
to these copies and run the same self-test. No additional Python dependency is used.

All compiler/test invocations use the owner's `build-lock.sh`; MSVC Release only.
Headless ImGui/ImPlot probes render CPU draw lists (no GPU/FPS claim). They warm five
frames and check the median of 30 full frames; p95 is logged for diagnosis. Large
plots enable RendererHasVtxOffset, as the production backend does. Wall-time budgets
include data preparation/allocations but do not count every C++ allocation separately.

Memory probes keep ONE Workflow/JobSystem, warm two operations, then perform ten more.
They log Windows PrivateUsage (private commit, not working set) after each operation
and bound peak-minus-warm-baseline growth. The ceiling is asserted after each measured repeat, stopping at the first breach.
This removes prior-suite allocator/cache
history from the assertion. A fixed-K growth budget does not prove an infinite-time
plateau. F01 currently grows in ScapsLab; its budget is explicitly expected to tighten
after perfret1. Deliberately changing K or retention requires recalibration/review.

Ceilings may only move DOWN without owner OK. To tighten, use the same fixture, MSVC
configuration and normal suite; record date, median/p95/private bytes and margin here.
Never silently raise a ceiling to make a failure green. There is no environment
variable that disables the checks or accepts fresh measurements as a baseline.

## ScapsLab

GTest `PerformanceGuard.*` runs with ScapsLabTests. The runtime probe executes
`Cmd::Run` and `Cmd::RunBatch` through the real JobSystem and reads typed I-V/BatchRow
results. It counts engine JSON adapters plus result Dump, Pack/Unpack adapters;
explicit Dump is the positive control. No Panel.cpp/Workflow.cpp refactor was made.

Frame fixture: checked-in original sweep5 input, normalized real axis combinations,
342 typed member results with 200 bias samples each. Only fixture construction is
synthetic; panels 16 (Batch), 17 (Recorder), 18 (Batch overlay) execute real rendering.
2026-11-22 calibration: Batch 7.633 ms (p95 8.162), Recorder 0.538 ms (0.589), overlay
1.676 ms (1.749). Ceilings 20 / 2 / 5 ms include scheduling margin. F04/F35 are
expected to improve after typed3/perf-frame1; these are regression ceilings.

Memory: real sweep5 light device, 342 members per operation, 4 workers, retention 1,
same Workflow and JobSystem. Calibration warm PrivateUsage 954294272 bytes, peak
4864950272, growth 3910656000 over ten operations. This is NOT a plateau: F01 is
visible at about 391 MB/operation. Initial growth ceiling 4608 MiB (23.6% margin)
is expected to tighten after perfret1; the test prevents further growth regression.

Build: repo `build`, external read-only ScapsLab Libraries artifacts redirected by
`perfguard-build.lua` to `%TEMP%/codex-builds/perfguard1-sl-libraries`, after the sandbox
refused writes to the sibling Libraries checkout. Run premake with that wrapper and
`--scaps-core=D:/SJCM/01_SemiconductorStudio/scaps_core-wt/perfguard1`.
