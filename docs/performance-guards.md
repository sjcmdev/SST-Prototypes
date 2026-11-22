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

## CapLab and JFM

Both GTest suites include `PerformanceGuard.StaticDataBoundaries`.
CapLab's Plot panel renders a real imported project with 100000 frequency samples.
2026-11-22 calibration: median 15.671 ms, p95 19.783 ms; ceiling 40 ms (F05,
expected to improve after shared renderer work). Its memory test imports that fixture,
executes real Circuit analysis jobs, accepts the typed project with the normal Core
command, and repeats on the same workflow. Initial growth ceiling 8 MiB, expected
to tighten after perfret1. 2026-11-22 warm PrivateUsage 495337472 bytes, peak 495779840, growth 442368 bytes;
the 8 MiB ceiling leaves allocator/cache margin and catches a retained full data column per operation.
This headless operation uses a four-frequency model-only Circuit analysis and no
UndoStack; it covers import/job retention, not full CV/Walter or Undo history budgets.

JFM renders the actual MonteCarloCurvesPanel with 512 accepted fitted/noisy curves,
1000 samples each, and a real WorkspaceStore/MonteCarloCompleted event. Calibration
median 195.794 ms, p95 199.510 ms; ceiling 350 ms. F32 is expected to improve after
curve caching. Draw-list vertex assertions prevent a silently empty probe.

The intrusive serializer seam is limited to engine + ScapsLab as authorized by scope.
CapLab still has F06/F12 JSON-native analysis and embedded Python transport; claiming
zero serialization there would be false. For future typed analysis migration, add
counter calls to analysis/file JSON adapters, dispatch caplab.analyze through Core,
and assert the typed analysis consumer leaves those counters at zero. JFM can extend
the same pattern to JFMFitJob -> WorkspaceStore with project/bundle/Python adapters
counted. Existing numeric/fit tests remain in the normal suite.

Build: repo `build`, MSVC Release x64. Standalone Libraries have their own `build`.
