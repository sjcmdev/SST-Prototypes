# jfmgpa - GPU organization, parity and robustness

Variant A was already present before this task: one fit per thread, 128 threads per block. LM, explicit NM and LM's NM fallback all use `sharedFitKernel` / `lm::fitSample`. The pre-task kernel body, captured snapshot and shipping body are identical; the product kernel diff is an explanatory comment. No further port or arithmetic change was needed.

G0 below is an otherwise identical reconstruction with one active lane per block. It measures organization, not a new speedup over pre-task JFM. Ratios against the actual pre-task A kernel range from 0.890x to 1.120x; they are timing variation for identical code.

RTX 4060, CUDA 12.6.20, MSVC 19.44.35225, sm_89, `--fmad=false`, `/fp:strict`. Each number is the median of three warmed solver-only runs. Allocation, uploads, noise generation, comparisons and downloads are excluded. WDDM/GPU and host were shared with other jobs.

| Input | N | G0/A LM | G0/A NM |
|---|---:|---:|---:|
| HZB_T330_4p | 1000 | 1.09-1.55x | 6.45-6.75x |
| HZB_T330_4p | 20000 | 4.05-4.33x | 13.58-16.40x |
| HZB_T181_6p | 1000 | 2.04-2.05x | 6.54-6.82x |
| HZB_T181_6p | 20000 | 5.42-6.20x | 15.02-15.21x |
| HZB_T247_7p | 1000 | 1.36-1.50x | 6.31-6.75x |
| HZB_T247_7p | 20000 | 4.03-4.33x | 14.94-15.06x |
| synthetic_forward_4p | 1000 | 6.21-7.59x | 3.39-3.63x |
| synthetic_forward_4p | 20000 | 9.05-18.53x | 9.16-10.57x |

Each range covers `log_legacy`, `isc_shift`, and `signed_sigma`; `auto` is resolved before kernel dispatch. [All timings and paired chi-squared counts](matrix/speedups.csv).

Bitwise gate: **PASS**, 48 configurations / 504,000 samples, zero result or predicted-current mismatches. The check compares active parameters, all chi-squared statistics, iterations, stop/LM status, accepted/converged flags, fallback and flat flags; it also calls the actual `gpuRunMonteCarlo` entry point. Three additional 133-sample cases force LM -> NM in each mode, including a partial block: **399/399 fallbacks**, zero mismatches.

Resume provenance: 35 complete configurations (358,000 samples), plus the three forced-fallback cases, were retained from the interrupted task after checking kernel and snapshot SHA-256 values. This resume built the GPU target and ran the remaining 13 configurations (146,000 samples). Final aggregation checks every cached JSON/binary pair and enforces the gate again. [Source/capture provenance](provenance.json), [resume GPU log](matrix-final.log), [final summary](matrix/summary.json).

Owner-approved chi-squared gate: **252,000/252,000 PASS** for `chi2_lm <= chi2_nm * (1 + 1e-12)`, with both values required finite, using the noisy-data fitting objective. The jfmlm GTest expectation and matrix runner now use this definition; a violating pair fails the runner.

Product fixes: applied the carried-over `count < 2` guard before sigma estimation / Isc interpolation; direct C++ fit/MC return structured validation errors for empty data, and embedded Python returns `jfm.fit.validation`. The MC legend groups every visible series under its actual line colour: Ideal, Noisy, Fitted, and separate rejected/flat entries when shown. Flat curves follow the rejected visibility switch. GTest renders the panel, checks legend labels and colours, toggles rejected visibility and disables Noisy.

Invalid, reversed, empty or non-finite ranges and out-of-domain/non-finite data are rejected before dispatch with notification-visible structured errors. The pre-existing objective validation already provides this behavior; the added tests cover it through single fit and MC. Nonpositive current remains valid for the signed objective modes; `log_legacy` rejects it. Existing zero-accepted warnings and command -> job -> notification delivery are retained.

Fresh validation in this resume: GTest **104/104 PASS** ([log](gtest-final.log), [XML](gtest-final.xml)); embedded pytest **235 passed, 24 xfailed, 0 failed, 0 skipped beyond the expected xfails** ([log](pytest-final.log), [XML](pytest.xml)); 1,377 numerical warnings, same count as the interrupted baseline; runtime 1542.31 s. GPU build passed in `build/jfmgpa-gpu` ([log](build-gpu-final2.log)). Product Release MSVC build **PASS** ([log](build-product-final.log)); JFMTests build **PASS** ([log](build-tests-final2.log)); embedded host build **PASS** ([log](build-python-host-final.log)). Product/tests use `build/generated/vs2022` and `build/bin/Release-windows-x86_64`; the embedded Python host is built in `%TEMP%/codex-builds/SST-Prototypes/jfmgpa`. Every build, test binary/suite and GPU process is wrapped separately by the shared build-lock; CPU aggregation stays outside it.

Graphify was updated after the code changes with one worker into the existing task-specific `%TEMP%/codex-builds/SST-Prototypes/jfmgpa/graphify-out`: 3,916 nodes / 8,684 edges. The shared repository graph was not overwritten during parallel jobs.

Limits: the matrix reuses gpu0b forward/positive-current captures. Signed/shifted modes use a data-derived sigma floor and zero Isc shift; this is not a full signed illuminated-data MC matrix. The pre-port snapshot shares the unchanged solver/evaluator, so parity is not an independent physical oracle. The global gpuref audit was not rerun; gpu0b's reported oracle failures are not waived by this gate. T247's flat flags are preserved (NM: 100%; LM about 28-31%, depending on mode/N). Other GPUs/toolchains and manual GUI screenshots were not checked.
