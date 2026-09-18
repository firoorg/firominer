Firominer v1.5.0 adds a Qt desktop launcher to the Linux and Windows packages and enables subgroup exchanges on supported AMD OpenCL GPUs.

### Changes

- Configure mainnet pool mining in the desktop launcher, with saved pool and worker settings, live statistics and logs, and system tray support. Pool passwords stay in memory for the current session.
- Choose Light, Dark, or System appearance. The launcher starts the bundled miner and uses a password-protected local API connection to monitor and control it.
- Stop GUI-launched miners gracefully on Windows. The command-line miner and Windows batch launcher remain available.
- Use indexed subgroup shuffles for DAG-offset exchanges when the AMD OpenCL driver supports them. Subgroup exchanges are enabled by default on eligible AMD GPUs. Compilation failures fall back to subgroup broadcasts and then the portable kernel; `--cl-no-subgroup` explicitly selects the portable path. No measured hashrate improvement is claimed.
- Include the launcher, Qt libraries, matching GUI/library sources, and license notices in every release archive. No separate Qt or Python installation is needed.

### Downloads and compatibility

| Package variant | Requirements |
| --- | --- |
| `cuda12.9-opencl` | NVIDIA driver 575.57.08 or newer on Linux, or 576.57 or newer on Windows. Includes CUDA, OpenCL, and CPU diagnostics. |
| `opencl` | A vendor OpenCL driver for GPU mining. Includes OpenCL and CPU diagnostics. Use this variant on machines without a suitable NVIDIA driver. |

Packages target compatible Ubuntu 22.04 or newer x86-64 systems and Windows 10/11 x64. The Linux GUI requires X11 or XWayland; native Wayland plugins are not included. CUDA runtime/compiler libraries and the Windows Visual C++ runtime are bundled. GPU drivers must be installed separately; a full CUDA Toolkit installation is unnecessary.

Extract the entire archive and keep its directory layout intact. Start `./bin/firominer-gui` on Linux or `bin\firominer-gui.exe` on Windows. The GUI setup supports mainnet Stratum pool mining. Solo mining, other networks, and advanced options remain available through `./bin/firominer` or `.\bin\firominer.exe`. Windows packages also include the editable `bin\mine_firo.bat` launcher.

See the included `docs/GUI.md` for launcher use and `docs/TESTING.md` for command-line examples and verification. `SHA256SUMS.txt` covers all four downloadable archives, and each archive includes an internal manifest. Keep the bundled `sources/` directory and license notices with redistributed packages.

### Validation

Publication is gated on core tests, AddressSanitizer/UndefinedBehaviorSanitizer, ThreadSanitizer, Linux and Windows CUDA/OpenCL package builds, GUI tests, relocated-package smoke tests, and checksum verification. Linux package checks load both the bundled offscreen and X11 Qt plugins. The workflow also verifies that the tag matches the source version and is on main.

The release workflow does not exercise physical GPU mining or accepted live-pool shares. Keep host solution verification enabled, and use `--cl-no-subgroup` if needed for driver compatibility.

[Full changes since v1.4.0](https://github.com/firoorg/firominer/compare/v1.4.0...v1.5.0). Includes [#28](https://github.com/firoorg/firominer/pull/28) and [#29](https://github.com/firoorg/firominer/pull/29).
