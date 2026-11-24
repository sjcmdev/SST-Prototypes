# CUDA Monte Carlo checks

The normal `JFMTests` build keeps the CPU stub. These tests also have a small CUDA
target, using the same fit engine and backend. Build the normal MSVC JFM tests
first so that `SST-Math.lib` exists in the repository build directory.

From the SST-Prototypes root in PowerShell, with CUDA installed:

```powershell
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh cmake -S Prototypes/JFM/tests/gpu -B build/gpu-tests -G "Visual Studio 17 2022" -A x64 "-Tcuda=$env:CUDA_PATH" -DCMAKE_CUDA_ARCHITECTURES=native
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh cmake --build build/gpu-tests --config Release -- -nr:false -m:1
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh ctest --test-dir build/gpu-tests -C Release --output-on-failure
```

`JFM_CUDA_DEVICE` selects one device by index (default `0`), once per process;
restart after changing it. Devices are never pooled. `JFM_GPU_MC_CHUNK_SIZE` sets
a positive experimental chunk size. It remains bounded by available memory,
64 MiB of host sample scratch, 65,536 samples, and the remaining sample count.
Invalid overrides use the existing CPU fallback and report a warning.

Automatic sizing targets two waves of resident threads, using kernel occupancy,
SM count and the device's thread limit. It reserves at least 256 MiB or 25% of
free VRAM, then uses at most half the remainder, including the kernel's reported
local memory per sample. Properties are cached; free memory is queried per job.
The job log records the device, compute capability, memory and chosen chunk.

Cancellation is checked before and after each calibration/fit chunk. Progress
updates follow calibration and fit chunks. Noisy curves use the original seed/index recipe;
only an explicitly requested prefix is retained. The GPU never downloads curves
that were already generated on the host.
