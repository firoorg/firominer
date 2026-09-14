# Building from source

## Table of Contents

* [Requirements](#requirements)
    * [Common](#common)
    * [Linux](#linux)
    * [macOS](#macos)
    * [Windows](#windows)
* [CMake configuration options](#cmake-configuration-options)
* [Disable Hunter](#disable-hunter)
* [Instructions](#instructions)
    * [Windows-specific script](#windows-specific-script)


## Requirements

This project uses [CMake] and [Hunter] package manager.

### Common

1. [CMake] >= 3.18.
2. [Git](https://git-scm.com/downloads)
3. [Perl](https://www.perl.org/get.html), needed to build OpenSSL
4. [CUDA Toolkit 12.9 Update 2](https://developer.nvidia.com/cuda-12-9-2-download-archive) (required when `ETHASHCUDA` is enabled, as it is by default; pass `-DETHASHCUDA=OFF` to build without NVIDIA CUDA support)

### Linux

1. A C++17 compiler (CI uses GCC 11 and GCC 13).
2. DBUS development libs if building with `-DETHDBUS`. E.g. on Ubuntu run:

```shell
sudo apt install libdbus-1-dev
```

### macOS

1. GCC version >= TBF

### Windows

1. [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/) with the MSVC v142 x64 toolset

## Instructions

1. Make sure git submodules are up to date:

    ```shell
    git submodule update --init --recursive
    ```

2. Create a build directory:

    ```shell
    mkdir build
    cd build
    ```

3. Configure the project with CMake. Check out the additional [configuration options](#cmake-configuration-options).

    ```shell
    cmake ..
    ```

    On Windows, use the Visual Studio 2019 generator and v142 toolset supported
    by CUDA 12.9:

    ```shell
    cmake .. -G "Visual Studio 16 2019" -A x64 -T v142
    ```

4. Build the project using [CMake Build Tool Mode]. This is a portable variant of `make`.

    ```shell
    cmake --build .
    ```

    Note: On Windows, it is possible to have compiler issues if you don't specify the build config. In that case use:

    ```shell
    cmake --build . --config Release
    ```

5. _(Optional, Linux only)_ Install the built executable:

    ```shell
    sudo make install
    ```

### Windows-specific script

Complete sample Windows batch file - **adapt it to your system**. Assumes that:

* it's placed one folder up from the firominer source folder
* you have CMake installed
* you have Perl installed

```bat
@echo off
setlocal

rem add MSVC in PATH
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\VsMSBuildCmd.bat"

rem add Perl in PATH; it's needed for OpenSSL build
set "PERL_PATH=C:\Perl\perl\bin"
set "PATH=%PERL_PATH%;%PATH%"

rem switch to firominer's source folder
cd "%~dp0\firominer\"

if not exist "build\" mkdir "build\"

cmake -G "Visual Studio 16 2019" -A x64 -T v142 -H. -Bbuild -DETHASHCL=ON -DETHASHCUDA=ON -DAPICORE=ON
cmake --build build --config Release --target package

endlocal
pause
```

## CMake configuration options

Pass these options to CMake configuration command, e.g.

```shell
cmake .. -DETHASHCUDA=ON -DETHASHCL=OFF
```

* `-DETHASHCL=ON` - enable OpenCL mining, `ON` by default.
* `-DETHASHCUDA=ON` - enable CUDA mining, `ON` by default.
* `-DAPICORE=ON` - enable API Server, `ON` by default.
* `-DBINKERN=ON` - install AMD binary kernels, `ON` by default.
* `-DETHDBUS=ON` - enable D-Bus support, `OFF` by default.

## Disable Hunter

If you want to install dependencies yourself or use system package manager you can disable Hunter by adding
[`-DHUNTER_ENABLED=OFF`](https://docs.hunter.sh/en/latest/reference/user-variables.html#hunter-enabled)
to the configuration options.

The managed build pins Boost 1.92.0, OpenSSL 3.5.8 LTS, JsonCpp 1.9.7,
CLI11 2.6.2, and intx 0.5.1 in `cmake/Boost.cmake` and
`cmake/Hunter/config.cmake`. Boost uses its upstream CMake build. System builds
require at least these versions. OpenCL builds also require the Khronos
OpenCL-Headers and OpenCL-CLHPP CMake packages (`OpenCLHeaders` and
`OpenCLHeadersCpp`); managed builds pin both and the ICD loader to
v2026.05.29. The OpenCL API target remains 1.2.

Packages include `share/firominer/dependencies.txt` and append these resolved
versions to `BUILD-INFO.txt` in CI. `firominer --version` reports the compiled
Boost, JsonCpp and CLI11 versions plus the linked OpenSSL runtime. The package
smoke tests check these against the release pins. Update the pins and those
checks together when upgrading dependencies.

CUDA remains at 12.9.2 to retain Maxwell, Pascal and Volta support. Dependency
updates do not imply a measured hashrate improvement.


[CMake]: https://cmake.org/
[CMake Build Tool Mode]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#build-tool-mode
[Hunter]: https://docs.hunter.sh/
