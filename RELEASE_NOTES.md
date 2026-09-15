Firominer v1.4.0 updates the supported dependency stack, improves GPU discovery across modern OpenCL runtimes, and reduces miner memory and verification overhead. It also fixes thermal pause handling, fatal Stratum response handling, and duplicate CUDA/OpenCL selection on NVIDIA systems.

### Changes

- Updated managed packages to OpenSSL 3.5.8 LTS, Boost 1.92.0, JsonCpp 1.9.7, CLI11 2.6.2, and Khronos OpenCL components v2026.05.29. Release packages include resolved dependency versions and licenses.
- Replaced the old managed OpenCL loader so current Windows AMD and NVIDIA drivers can expose all installed platforms.
- Recognized AMD and NVIDIA GPUs by vendor ID on additional OpenCL runtimes, including Mesa Rusticl. PCI domains and functions are preserved when the runtime reports them.
- Prevented automatic mixed mode from starting separate CUDA and OpenCL miners for an NVIDIA GPU that cannot be matched safely. Use `-G` to select OpenCL explicitly in that case.
- Released temporary CUDA and OpenCL light caches after DAG generation, avoided duplicate simulation solution verification, and reduced CUDA digest-reduction operations. These changes do not establish an end-to-end hashrate gain.
- Kept thermal pauses unchanged when a temperature read fails, stopped buffered Stratum processing after fatal responses, and reaped completed asynchronous scripts on Linux.
- Added an editable `bin/mine_firo.bat` launcher to Windows packages and refreshed the miner artwork.
- Raised the minimum CMake version to 3.18 and removed obsolete OpenGL build prerequisites.
- Removed the redundant `SHA256SUMS-windows.txt` release asset. The combined `SHA256SUMS.txt` covers all four downloadable archives, and every package retains its internal manifest.

### Downloads and compatibility

| Package variant | Requirements |
| --- | --- |
| `cuda12.9-opencl` | NVIDIA driver 575.57.08 or newer on Linux, or 576.57 or newer on Windows. Includes CUDA, OpenCL, and CPU diagnostics. |
| `opencl` | A vendor OpenCL driver for GPU mining. Includes OpenCL and CPU diagnostics. Use this variant on machines without a suitable NVIDIA driver. |

Packages target compatible Ubuntu 22.04 or newer x86-64 systems and Windows 10/11 x64. CUDA runtime/compiler libraries and the Windows Visual C++ runtime are bundled; GPU drivers are installed separately. A full CUDA Toolkit installation is unnecessary. Keep the extracted directory intact so the executable can find its companion libraries.

Maxwell, Pascal, and Volta NVIDIA GPUs remain supported with an appropriate R575 or R580 driver. R580 is the final driver branch supporting these architectures. CUDA packages require the driver versions above because FiroPoW kernels compile to PTX at runtime.

Launch `./bin/firominer` on Linux or `.\bin\firominer.exe` in Windows PowerShell. Windows packages also include `bin\mine_firo.bat`, which can be edited with pool and wallet details. Select `-U` for CUDA or `-G` for OpenCL. See the included `docs/TESTING.md` for checksum verification, device selection, and pool or solo mining examples.

### Validation

Publication is gated on core tests, AddressSanitizer/UndefinedBehaviorSanitizer, ThreadSanitizer, Linux CUDA/OpenCL package builds, Windows CUDA/OpenCL package builds, package smoke tests, and checksum verification. The release workflow also verifies that the tag matches the source version and is on main.

Development validation covered Windows discovery of an RTX 4090 and AMD gfx1036, CUDA/OpenCL PCI identity normalization, managed and system OpenCL builds, and CPU-verified CUDA kernel results. The exact release binaries have not been exercised on physical GPUs or a live pool, and no end-to-end hashrate improvement is claimed. Keep host solution verification enabled.

[Full changes since v1.3.0](https://github.com/firoorg/firominer/compare/v1.3.0...v1.4.0). Includes [#18](https://github.com/firoorg/firominer/pull/18), [#19](https://github.com/firoorg/firominer/pull/19), [#20](https://github.com/firoorg/firominer/pull/20), [#21](https://github.com/firoorg/firominer/pull/21), [#22](https://github.com/firoorg/firominer/pull/22), [#23](https://github.com/firoorg/firominer/pull/23), [#24](https://github.com/firoorg/firominer/pull/24), and [#25](https://github.com/firoorg/firominer/pull/25).
