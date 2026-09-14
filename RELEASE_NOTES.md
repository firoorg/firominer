Firominer v1.3.0 updates CUDA packages to CUDA 12.9 Update 2, enables OpenCL helper inlining by default, and removes the desktop launcher. Start the miner from a terminal; Python and Tk are no longer required to run it.

### Changes

- OpenCL automatically retries the legacy mix kernel if compilation fails. Use `--cl-no-inline` for driver compatibility. The existing `--cl-experimental-inline` flag remains accepted.
- Added experimental `--cl-subgroup` DAG-offset broadcasts for compatible AMD GPUs. This option remains off by default, with portable fallbacks for unsupported devices, incompatible subgroup layouts, and compilation failures.
- Linux packages now use C/C++ link-time optimization while retaining a generic x86-64 CPU baseline.
- Fixed Windows release archive downloads and OpenCL source embedding for MSVC builds.
- Added `SHA256SUMS.txt` covering all four Linux and Windows download archives. The existing Linux `.sha256` files, Windows checksum manifest, and checksums inside each package remain available.

### Downloads and compatibility

| Package variant | Requirements |
| --- | --- |
| `cuda12.9-opencl` | NVIDIA driver 575.57.08 or newer on Linux, or 576.57 or newer on Windows. Includes CUDA, OpenCL, and CPU diagnostics. |
| `opencl` | A vendor OpenCL driver for GPU mining. Includes OpenCL and CPU diagnostics. Use this variant on machines without a suitable NVIDIA driver. |

Packages target compatible Ubuntu 22.04 or newer x86-64 systems and Windows 10/11 x64. CUDA runtime/compiler libraries and the Windows Visual C++ runtime are bundled; GPU drivers are installed separately. A full CUDA Toolkit installation is unnecessary. Keep the extracted directory intact so the executable can find its companion libraries.

Maxwell, Pascal, and Volta NVIDIA GPUs remain supported with an appropriate R575 or R580 driver. R580 is the final driver branch supporting these architectures. CUDA packages require the driver versions above because FiroPoW kernels compile to PTX at runtime.

Launch `./bin/firominer` on Linux or `.\bin\firominer.exe` in Windows PowerShell. Select `-U` for CUDA or `-G` for OpenCL. See the included `docs/TESTING.md` for checksum verification, device selection, and pool or solo mining examples.

### Validation

Publication is gated on core tests, AddressSanitizer/UndefinedBehaviorSanitizer, ThreadSanitizer, Linux and Windows package builds, package smoke tests, and checksum verification. Offline OpenCL checks cover four kernel variants across three periods and three workgroup sizes.

Physical GPU execution, accepted pool shares, and hashrate improvements have not been validated for this release. Keep host solution verification enabled. Automatic kernel fallback handles compilation failures; it cannot detect a driver that compiles an incorrect kernel.

[Full changes since v1.2.0](https://github.com/firoorg/firominer/compare/v1.2.0...v1.3.0). Includes [#15](https://github.com/firoorg/firominer/pull/15), [#16](https://github.com/firoorg/firominer/pull/16), and [#17](https://github.com/firoorg/firominer/pull/17).
