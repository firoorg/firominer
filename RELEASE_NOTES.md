Firominer v1.5.3 reduces repeated GPU work and fixes mining telemetry and hashrate reporting.

### Changes

- CUDA and OpenCL continue from the next unscheduled nonce when a pool resends unchanged work, including a new share target, or mining resumes after a pause. This avoids searching the same range again. Progress is kept within the running miner, not across process restarts.
- Resumed GPU launches stay within bounded nonce ranges. CUDA searches remaining whole blocks with smaller launches; OpenCL waits for new work when its assigned range is spent.
- Default spacing between GPUs increases from 2^32 to 2^40 nonces, delaying overlap on unchanged ordinary jobs to over five hours at 60 MH/s per device. Ordinary jobs still have no hard segment limit and can eventually overlap; pool extranonce jobs use bounded ranges.
- Miner stop/start cycles within the same process preserve device telemetry rows and cumulative share counters, and stopped hashrates clear to zero. JSON and HTTP statistics and pool submissions use 64-bit hashrate conversions to avoid truncating large rates.

### Downloads and compatibility

| Package variant | Requirements |
| --- | --- |
| `cuda12.9-opencl` | NVIDIA driver 575.57.08 or newer on Linux, or 576.57 or newer on Windows. Includes CUDA, OpenCL, and CPU diagnostics. |
| `opencl` | A vendor OpenCL driver for GPU mining. Includes OpenCL and CPU diagnostics. Use this variant on machines without a suitable NVIDIA driver. |

Packages target compatible Ubuntu 22.04 or newer x86-64 systems and Windows 10/11 x64. The Linux GUI requires X11 or XWayland; native Wayland plugins are not included. CUDA runtime/compiler libraries and the Windows Visual C++ runtime are bundled. GPU drivers must be installed separately; a full CUDA Toolkit installation is unnecessary.

Extract the entire archive and keep its directory layout intact. Start `./bin/firominer-gui` on Linux or `bin\firominer-gui.exe` on Windows. The GUI supports Mainnet Stratum pool mining and direct solo mining against your own Firo node. Other networks and advanced options remain available through `./bin/firominer` or `.\bin\firominer.exe`. Windows packages also include the editable `bin\mine_firo.bat` launcher.

See the included `docs/GUI.md` for launcher use and `docs/TESTING.md` for command-line examples and verification. `SHA256SUMS-v1.5.3.txt` covers all four downloadable archives, and each archive includes an internal manifest. Keep the bundled `sources/` directory and license notices with redistributed packages.

### Validation

Publication is gated on core tests, AddressSanitizer/UndefinedBehaviorSanitizer, ThreadSanitizer, Linux and Windows CUDA/OpenCL package builds, GUI and solo-node tests, relocated-package smoke tests, and checksum verification. Linux package checks load both the bundled offscreen and X11 Qt plugins. The workflow also verifies that the tag matches the source version and is on main.

The release workflow does not exercise physical GPU mining or accepted live-pool or solo blocks. No measured hashrate increase is claimed. Keep host solution verification enabled, and use `--cl-no-subgroup` if needed for driver compatibility.

[Full changes since v1.5.2](https://github.com/firoorg/firominer/compare/v1.5.2...v1.5.3). Includes [#36](https://github.com/firoorg/firominer/pull/36) and [#37](https://github.com/firoorg/firominer/pull/37).
