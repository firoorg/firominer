Firominer v1.5.2 adds optional solo coinbase messages to the desktop launcher and includes the version number in release download filenames.

### Changes

- Set an optional **Coinbase message** in the launcher's Solo settings. The message is saved for your next launch and embeds public text in blocks you mine. Enter it directly, without wrapping it in quotes, up to 80 UTF-8 bytes. Explorer display depends on the explorer.
- **Test node** and the check before starting solo mining require the node to acknowledge the exact message. Use Firo Core 0.14.18.1 or another version with coinbase-message support, or leave the field empty for nodes without it. Invalid Unicode, null characters, and messages over the byte limit are rejected locally. Unicode coinbase messages on Windows require Windows 10 version 1903 or newer.
- Identify downloads by version: all four archives and both Linux checksum sidecars now contain `-v1.5.2` in their names. The combined checksum file is `SHA256SUMS-v1.5.2.txt`. Internal package paths are unchanged.

### Downloads and compatibility

| Package variant | Requirements |
| --- | --- |
| `cuda12.9-opencl` | NVIDIA driver 575.57.08 or newer on Linux, or 576.57 or newer on Windows. Includes CUDA, OpenCL, and CPU diagnostics. |
| `opencl` | A vendor OpenCL driver for GPU mining. Includes OpenCL and CPU diagnostics. Use this variant on machines without a suitable NVIDIA driver. |

Packages target compatible Ubuntu 22.04 or newer x86-64 systems and Windows 10/11 x64. The Linux GUI requires X11 or XWayland; native Wayland plugins are not included. CUDA runtime/compiler libraries and the Windows Visual C++ runtime are bundled. GPU drivers must be installed separately; a full CUDA Toolkit installation is unnecessary.

Extract the entire archive and keep its directory layout intact. Start `./bin/firominer-gui` on Linux or `bin\firominer-gui.exe` on Windows. The GUI supports Mainnet Stratum pool mining and direct solo mining against your own Firo node. Other networks and advanced options remain available through `./bin/firominer` or `.\bin\firominer.exe`. Windows packages also include the editable `bin\mine_firo.bat` launcher.

See the included `docs/GUI.md` for launcher use and `docs/TESTING.md` for command-line examples and verification. `SHA256SUMS-v1.5.2.txt` covers all four downloadable archives, and each archive includes an internal manifest. Keep the bundled `sources/` directory and license notices with redistributed packages.

### Validation

Publication is gated on core tests, AddressSanitizer/UndefinedBehaviorSanitizer, ThreadSanitizer, Linux and Windows CUDA/OpenCL package builds, GUI and solo-node tests, relocated-package smoke tests, and checksum verification. Linux package checks load both the bundled offscreen and X11 Qt plugins. The workflow also verifies that the tag matches the source version and is on main.

The release workflow does not exercise physical GPU mining or accepted live-pool or solo blocks. Keep host solution verification enabled, and use `--cl-no-subgroup` if needed for driver compatibility.

[Full changes since v1.5.1](https://github.com/firoorg/firominer/compare/v1.5.1...v1.5.2). Includes [#33](https://github.com/firoorg/firominer/pull/33) and [#34](https://github.com/firoorg/firominer/pull/34).
