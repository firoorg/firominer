# Firominer desktop launcher

`firominer-gui` is a C++ / Qt Widgets application that starts the existing
command-line miner as a separate process. The miner's command-line interface,
batch files and API remain available without the GUI. No Python is needed to
run either application.

## Running

Each Linux and Windows package from the **CI** workflow contains both the desktop
launcher and the matching command-line miner. The release workflow publishes
the same combined packages, in `cuda12.9-opencl` and `opencl` variants. Qt
libraries, licenses, and corresponding GUI/Qt source are included, along with
the Visual C++ runtime on Windows. There is no separate launcher download to
combine with a miner.

On Windows, extract the entire ZIP, then double-click `bin/firominer-gui.exe`. Command-line
users can run `bin/firominer.exe` or the included batch file from the same
package. Keep the executables, libraries, and plugin folders together. Only
the appropriate GPU driver needs to be installed separately.

On Linux, extract the entire `.tar.gz` archive and run `./bin/firominer-gui`, or
`./bin/firominer` for the command line. The GUI targets Ubuntu 22.04 or compatible
newer x86-64 desktops with X11 or XWayland. No separate Qt installation is needed.
The operating system supplies the desktop/display server, fonts, standard C/C++
runtime and graphics drivers. Native Wayland plugins are not included.

The GUI starts the adjacent `firominer` (`firominer.exe` on Windows) automatically.
If selecting a different miner executable in Settings, keep it inside its complete extracted
package with its libraries, and use a build with API support (`APICORE=ON`).
The bundled current miner supports graceful Windows shutdown. Older miners
can run but may require the launcher's forced-stop fallback when stopping.

Enter your pool connection, payout address and worker name, select a backend,
then start mining. The dashboard displays data reported by the miner. Hardware
monitoring depends on the device and driver, so some values may be unavailable.
On Windows, the background miner has no separate console window; its output
appears in the GUI's log view.

The GUI controls only the process it starts. Its monitoring connection is
bound to `127.0.0.1` and protected with a fresh API password for each launch.
It does not attach to miners started in another terminal.

The setup screen currently configures mainnet Stratum pool mining. Solo mining,
other networks and advanced miner flags remain available through the CLI.

Pool passwords are kept only for the current GUI session. Other mining settings
are saved for your next launch. Closing the window while mining offers to stop
and quit, or keep mining in the system tray when a tray is available.

Settings also provides Light, Dark, and System appearance. Light is the default;
Save applies and remembers your choice, while Cancel leaves the current theme
unchanged. Windows high-contrast settings take precedence.

## Building the GUI only

The build requires CMake 3.18+, a C++17 compiler, and Qt 6.2+ with Widgets and
Network. Enable tests to include Qt Test. These are developer requirements,
not separate installations required by users of the bundled packages.

For Windows, use a Qt kit matching your compiler, for example Qt 6.8.3
`msvc2022_64` with Visual Studio 2022:

```powershell
cmake -S gui -B build-gui -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DBUILD_TESTING=ON
cmake --build build-gui --config Release --parallel
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
ctest --test-dir build-gui --build-config Release --output-on-failure
cmake --install build-gui --config Release --prefix stage-gui
```

Installation runs Qt's `windeployqt` to copy the Qt runtime and plugins.
MSVC builds also copy the redistributable Visual C++ runtime libraries beside
the executable. Copy the contents of `stage-gui` into a matching Firominer
package for local use, preserving its directory layout. This install step alone
does not collect corresponding source for redistribution; follow the source
requirements below or use the source-inclusive CI package. Building from an
MSYS2 Qt package may require additional third-party DLLs supplied by that distribution;
`windeployqt` does not collect all of those libraries.

On Linux, install your distribution's Qt 6 development packages, then run:

```sh
cmake -S gui -B build-gui -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-gui --parallel
ctest --test-dir build-gui --output-on-failure
./build-gui/firominer-gui
```

The tests set `QT_QPA_PLATFORM=offscreen` and run without a GPU or pool. Ordinary
Linux installation uses system Qt libraries. The CI packaging step additionally
runs `cmake/DeployLinuxGui.cmake` and `cmake/CollectLinuxGuiSources.sh` to collect
Qt, its supporting libraries and their corresponding source into each archive.
The private `lib/firominer-gui` directory and relative library search paths keep
GUI dependencies separate from the miner. `bin/qt.conf` selects the bundled
plugins, following [Qt's shared-library deployment layout](https://doc.qt.io/qt-6/linux-deployment.html).

To build the GUI with the miner, add `-DFIROMINER_GUI=ON` and your Qt prefix to
the normal root CMake configuration, keeping `APICORE=ON`. CMake rejects the
GUI with `APICORE=OFF`. The GUI default is `OFF`, so existing miner
builds have no Qt dependency. Standalone `cmake -S gui` builds do not configure
Hunter, CUDA, OpenCL or the miner's other dependencies.

The GUI is covered by the repository's GPLv3 license. It dynamically links Qt
Core, GUI, Widgets and Network, copyright The Qt Company and other contributors,
under the GNU Lesser General Public License version 3. The LGPLv3 text is
included in `share/firominer-gui/licenses`; the GPLv3 text is in
`share/firominer-gui/LICENSE`. Qt library replacement and debugging modifications
to those libraries are permitted under these licenses.

Each Windows CI and release package includes the exact Firominer checkout and the
SHA256-verified Qt 6.8.3 `qtbase` source archive under `sources/`. That module
contains the source for every deployed Qt library and plugin, including its
bundled third-party components and license notices. The source is distributed
in the same artifact as the binaries, under the distributor's control.
`sources/README.txt` records the build revision and Qt source provenance.

Linux packages include the same Firominer source archive and the exact Ubuntu
source packages for every bundled GUI library/plugin in `sources/debian`.
This includes distribution patches, build rules and dependency copyright notices.
`sources/debian/packages.tsv` records binary/source package versions;
`sources/debian/README.txt` explains rebuilding and replacing these libraries.
CI verifies both the offscreen and X11 plugins from a relocated archive and
rejects dependencies that fall back to the build machine outside the documented
platform runtime and graphics stack.

Keep these archives and all license notices with any redistributed package.
If using another Qt build, include its matching source, all applied patches,
build instructions and any additional dependency source required by their
licenses. An upstream download link alone is not a corresponding-source
arrangement. See [Qt's open-source obligations](https://www.qt.io/development/download-open-source).

To replace Qt on Windows, extract its source archive. The Windows CI package uses
Visual Studio 2022, x64, shared Qt 6.8.3; its feature/compiler configuration is recorded in
`sources/qt-build-config.pri`. In an x64 Visual Studio developer shell, with
CMake and Ninja available, build your modified Qt source:

```powershell
mkdir qt-build
cd qt-build
../qtbase-everywhere-src-6.8.3/configure.bat -release -shared -opensource -confirm-license -nomake examples -nomake tests -prefix C:/Qt/modified-6.8.3
cmake --build . --parallel
cmake --install .
```

Rebuild the GUI from the included Firominer source against that Qt prefix if
needed, then run the GUI install command above into a new directory. Keep the
replacement Qt DLLs and plugin folders together beside `firominer-gui.exe`.
There are no signature or checksum checks that prevent running with modified
Qt libraries.
