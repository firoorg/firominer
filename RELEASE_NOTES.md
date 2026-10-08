Firominer v1.6.0 restyles the desktop launcher in the Firo Core wallet's identity and rebuilds its overview around the miner's state.

### Changes

- The launcher uses the Firo Core wallet's colors, typefaces (Saira SemiCondensed and Source Sans Pro), icons and status badges in the Light and Dark themes. System and high-contrast settings keep your platform's colors. The sidebar collapses to icons in narrow windows.
- The overview leads with the miner's state. Until setup is complete it says what is missing and the start button reads **Set up mining**; a saved solo setup asks only for its RPC password, which is never saved. Once ready, it names the pool or node and the payout address, and shows the last session's runtime, accepted shares and average hashrate, which are kept across restarts.
- While mining, a summary banner shows total hashrate, accepted shares, the last share and GPU power with efficiency, the hashrate chart shows its average, and each GPU has a card with its recent hashrate, temperature, fan, power and shares. Connecting, paused, reconnecting and stopping read consistently across the page, and readings that are no longer current are shown as unavailable rather than stale.
- The bundled fonts are licensed under the SIL Open Font License 1.1; their license texts are installed in `share/firominer-gui/licenses`. Mining, pool and node code is unchanged from v1.5.3.

### Downloads and compatibility

| Package variant | Requirements |
| --- | --- |
| `cuda12.9-opencl` | NVIDIA driver 575.57.08 or newer on Linux, or 576.57 or newer on Windows. Includes CUDA, OpenCL, and CPU diagnostics. |
| `opencl` | A vendor OpenCL driver for GPU mining. Includes OpenCL and CPU diagnostics. Use this variant on machines without a suitable NVIDIA driver. |

Packages target compatible Ubuntu 22.04 or newer x86-64 systems and Windows 10/11 x64. The Linux GUI requires X11 or XWayland; native Wayland plugins are not included. CUDA runtime/compiler libraries and the Windows Visual C++ runtime are bundled. GPU drivers must be installed separately; a full CUDA Toolkit installation is unnecessary.

Extract the entire archive and keep its directory layout intact. Start `./bin/firominer-gui` on Linux or `bin\firominer-gui.exe` on Windows. The GUI supports Mainnet Stratum pool mining and direct solo mining against your own Firo node. Other networks and advanced options remain available through `./bin/firominer` or `.\bin\firominer.exe`. Windows packages also include the editable `bin\mine_firo.bat` launcher.

See the included `docs/GUI.md` for launcher use and `docs/TESTING.md` for command-line examples and verification. `SHA256SUMS-v1.6.0.txt` covers all four downloadable archives, and each archive includes an internal manifest. Keep the bundled `sources/` directory and license notices with redistributed packages.

### Validation

Publication is gated on core tests, AddressSanitizer/UndefinedBehaviorSanitizer, ThreadSanitizer, Linux and Windows CUDA/OpenCL package builds, GUI and solo-node tests, relocated-package smoke tests, and checksum verification. Linux package checks load both the bundled offscreen and X11 Qt plugins. GUI tests run on Qt 6.2 and 6.4 on Linux and Qt 6.8 on Windows. The workflow also verifies that the tag matches the source version and is on main.

The release workflow does not exercise physical GPU mining or accepted live-pool or solo blocks. Keep host solution verification enabled, and use `--cl-no-subgroup` if needed for driver compatibility.

[Full changes since v1.5.3](https://github.com/firoorg/firominer/compare/v1.5.3...v1.6.0). Includes [#39](https://github.com/firoorg/firominer/pull/39).
