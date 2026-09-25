Firominer v1.5.1 adds Mainnet solo mining to the desktop launcher and improves its layout on smaller and scaled displays.

### Changes

- Choose Pool or Solo in the desktop launcher. Solo connects directly to your own synced Mainnet Firo node and uses a transparent Firo reward address. Pool and solo settings are saved separately; passwords stay in memory for the current session.
- Use the built-in node configuration guide and **Test node** action to check RPC access, network, sync status, reward address, and mining work before launching. Failed or cancelled checks do not start mining, and RPC credentials are kept out of logs.
- Monitor accepted blocks and node connection status in Solo mode. Setup and overview panels reflow for smaller windows, larger text, and display scaling.
- Expand GUI and controller tests for node failures, miner startup errors, responsive layouts, and Windows packaging.

### Downloads and compatibility

| Package variant | Requirements |
| --- | --- |
| `cuda12.9-opencl` | NVIDIA driver 575.57.08 or newer on Linux, or 576.57 or newer on Windows. Includes CUDA, OpenCL, and CPU diagnostics. |
| `opencl` | A vendor OpenCL driver for GPU mining. Includes OpenCL and CPU diagnostics. Use this variant on machines without a suitable NVIDIA driver. |

Packages target compatible Ubuntu 22.04 or newer x86-64 systems and Windows 10/11 x64. The Linux GUI requires X11 or XWayland; native Wayland plugins are not included. CUDA runtime/compiler libraries and the Windows Visual C++ runtime are bundled. GPU drivers must be installed separately; a full CUDA Toolkit installation is unnecessary.

Extract the entire archive and keep its directory layout intact. Start `./bin/firominer-gui` on Linux or `bin\firominer-gui.exe` on Windows. The GUI supports Mainnet Stratum pool mining and direct solo mining against your own Firo node. Other networks and advanced options remain available through `./bin/firominer` or `.\bin\firominer.exe`. Windows packages also include the editable `bin\mine_firo.bat` launcher.

See the included `docs/GUI.md` for launcher use and `docs/TESTING.md` for command-line examples and verification. `SHA256SUMS.txt` covers all four downloadable archives, and each archive includes an internal manifest. Keep the bundled `sources/` directory and license notices with redistributed packages.

### Validation

Publication is gated on core tests, AddressSanitizer/UndefinedBehaviorSanitizer, ThreadSanitizer, Linux and Windows CUDA/OpenCL package builds, GUI and solo-node tests, relocated-package smoke tests, and checksum verification. Linux package checks load both the bundled offscreen and X11 Qt plugins. The workflow also verifies that the tag matches the source version and is on main.

The release workflow does not exercise physical GPU mining or accepted live-pool or solo blocks. Keep host solution verification enabled, and use `--cl-no-subgroup` if needed for driver compatibility.

[Full changes since v1.5.0](https://github.com/firoorg/firominer/compare/v1.5.0...v1.5.1). Includes [#31](https://github.com/firoorg/firominer/pull/31).
