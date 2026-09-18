# Firominer desktop launcher

`firominer-gui` is a C++ / Qt Widgets application that starts the existing
command-line miner as a separate process. The miner's command-line interface,
batch files and API remain available without the GUI. No Python is needed to
run either application.

## Running

The **GUI** GitHub Actions workflow uploads `firominer-gui-windows-x64.zip`.
This companion contains the launcher, Qt and Visual C++ runtime libraries,
licenses, and corresponding source archives. It does not contain the miner.
Obtain a Windows miner package from the **CI** workflow for the same commit,
then extract the companion into that package, preserving its directory layout.
Released miner packages do not currently include the GUI. Windows launchers
require the matching miner build for graceful shutdown; older releases such as
v1.4.0 do not accept the launcher's shutdown option.

Keep `firominer-gui.exe` and its bundled libraries and plugin folders together.
Place it beside `firominer.exe` in the miner package's `bin` directory, or
select the miner executable in the GUI's settings. The GUI does not contain a
mining backend; it needs the matching Firominer build with API support
(`APICORE=ON`) and the appropriate GPU driver.

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

## Building the GUI only

The build requires CMake 3.18+, a C++17 compiler, and Qt 6.2+ with Widgets and
Network. Enable tests to include Qt Test. These are developer requirements,
not separate installations required by users of the bundled Windows package.

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
requirements below or use the source-inclusive CI companion. Building from an
MSYS2 Qt package may require additional third-party DLLs supplied by that distribution;
`windeployqt` does not collect all of those libraries.

On Linux, install your distribution's Qt 6 development packages, then run:

```sh
cmake -S gui -B build-gui -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-gui --parallel
ctest --test-dir build-gui --output-on-failure
./build-gui/firominer-gui
```

The tests set `QT_QPA_PLATFORM=offscreen` and run without a GPU or pool. Linux
installation uses system Qt libraries; it does not produce a portable Linux
bundle.

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

The Windows CI companion includes the exact Firominer checkout and the
SHA256-verified Qt 6.8.3 `qtbase` source archive under `sources/`. That module
contains the source for every deployed Qt library and plugin, including its
bundled third-party components and license notices. The source is distributed
in the same artifact as the binaries, under the distributor's control.
`sources/README.txt` records the build revision and Qt source provenance.

Keep these archives and all license notices with any redistributed companion.
If using another Qt build, include its matching source, all applied patches,
build instructions and any additional dependency source required by their
licenses. An upstream download link alone is not a corresponding-source
arrangement. See [Qt's open-source obligations](https://www.qt.io/development/download-open-source).

To replace Qt, extract its source archive. The CI package uses Visual Studio
2022, x64, shared Qt 6.8.3; its feature/compiler configuration is recorded in
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
