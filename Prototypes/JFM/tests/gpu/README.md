# GPU organization regression gate

`gpu_backend.cu` already uses variant A (one fit per thread, 128 threads/block) for LM,
explicit NM, and the NM fallback inside `lm::fitSample`. This check preserves the
arithmetic and compares the shipping kernel with a captured pre-change kernel and
an otherwise identical one-active-lane-per-block G0 organization.

`pre-port-kernel.cuh` contains the kernel captured before this task and a G0 copy
with only its sample index changed (one active lane per 32-thread block).
The snapshot shares the unchanged solver/evaluator with the product; this is an
organization regression check, not an independent numerical oracle.

Configure in an MSVC Developer Command Prompt, through the global build-lock
(use `build/jfmgpa-gpu`, or the shown temporary directory if the sandbox denies writes):

```text
cmake -S Prototypes/JFM/tests/gpu -B %TEMP%/codex-builds/SST-Prototypes/jfmgpa/gpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_CUDA_HOST_COMPILER=cl -DCMAKE_CUDA_ARCHITECTURES=89
cmake --build %TEMP%/codex-builds/SST-Prototypes/jfmgpa/gpu
```

Wrap each command with `bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh`.
Run `python Prototypes/JFM/tests/gpu/run_matrix.py <exe> <GPU-fit-bench/cases> <output>`
outside the lock; the runner locks each individual GPU process. It covers T330 4p,
T181 6p, T247 7p and synthetic, LM/NM, all three kernel objective modes and
N=1000/20000. `auto` resolves to a kernel objective before dispatch. Inputs are the
resolved gpu0b captures. Signed/shifted modes use the same positive forward curves,
a data-derived sigma floor, and zero Isc shift; they do not test illuminated data.

The executable compares every result field (zero-initialized device storage),
bitwise predicted currents, and the actual `gpuRunMonteCarlo` path. Nonzero mismatch
counts fail the process. CSV timing is a median of three warmed solver-only runs,
excluding allocation, upload, noise generation, current comparisons and download.
Each JSON has counts of LM fallbacks and flat results. The runner separately reports
the owner-approved gate `chi2_lm <= chi2_nm * (1 + 1e-12)` on the noisy-data objective.
A G0 speedup measures organization only; it is not an incremental speedup over a
pre-change JFM that already uses A.

After an interruption, `--resume` reuses matching complete JSON/binary pairs.
First verify that the product kernel, solver/evaluator, captures and executable
are unchanged (see `docs/jfmgpa/provenance.json`); this option does not infer
source provenance from file timestamps. It reruns malformed JSON, mismatched
metadata, failed comparisons and truncated/missing objective arrays. The runner
fails if any LM/NM pair violates the owner-approved chi-squared gate.
