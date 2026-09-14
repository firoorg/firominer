# firominer

> FiroPoW miner for Firo, with OpenCL, CUDA and Stratum support

**firominer** mines Firo using FiroPoW, Firo's ProgPoW-based proof of work. It supports pool mining over Stratum and solo mining against a Firo daemon over HTTP. It originates from the [ethminer](https://github.com/ethereum-mining/ethminer) project and the [ProgPoW](https://github.com/ifdefelse/progpow) implementation. Its hashing and network rules are specific to Firo; it is not a general Ethereum or ProgPoW miner.

## Features

* OpenCL and NVIDIA CUDA mining, with individual device selection
* Local mining simulation starting at a specified block height
* On-GPU DAG generation, with no DAG files on disk
* Stratum pool mining and HTTP solo mining, with endpoint failover
* Mainnet, testnet, devnet and regtest epoch schedules
* Optional HTTP monitoring and TCP JSON-RPC control API
* Development CPU backend, selected explicitly with `--cpu` when compiled in
* Custom solo coinbase messages with the companion Firo daemon patch


## Table of Contents

* [Install](#install)
* [Usage](#usage)
    * [Examples connecting to pools](#examples-connecting-to-pools)
    * [Device selection and local testing](#device-selection-and-local-testing)
    * [Solo mining and network selection](#solo-mining-and-network-selection)
    * [Monitoring and device recovery](#monitoring-and-device-recovery)
* [Build](#build)
    * [Continuous Integration and development builds](#continuous-integration-and-development-builds)
    * [Building from source](#building-from-source)
* [FiroPoW parameters](#firopow-parameters)
* [Maintainers & Authors](#maintainers--authors)
* [Contribute](#contribute)
* [License](#license)
* [F.A.Q.](#faq)


## Install

Download a package for your operating system from [Releases], or use a
development artifact from GitHub Actions. The current packaging workflow produces
Linux x86-64 and Windows x64 packages in two variants:

| Package | Required driver | Backends |
| --- | --- | --- |
| `cuda12.9-opencl` | NVIDIA 575.57.08+ (Linux) or 576.57+ (Windows); legacy GPU limits below | CUDA, OpenCL, CPU diagnostics |
| `opencl` | Vendor OpenCL driver for GPU mining | OpenCL, CPU diagnostics |

The CUDA package uses CUDA 12.9 Update 2 and supports Maxwell or newer NVIDIA
GPUs. Maxwell, Pascal, and Volta require an R575 or R580 driver; later driver
branches no longer support them. The package needs the CUDA 12.9 driver floor
because FiroPoW kernels are compiled to PTX at runtime.

Both variants include the command-line miner and API. Keep the extracted
directory intact: the executable in `bin/` needs its companion libraries.
Current packages bundle the CUDA runtime/compiler libraries where applicable
and the Visual C++ runtime on Windows. A full CUDA Toolkit is needed to build
the CUDA backend, but not to run these packages. GPU drivers are installed
separately. See [Testing PR artifacts](docs/TESTING.md) for
checksum verification and platform requirements.

## Usage

Launch **firominer** from a terminal.
The commands below assume `firominer` is on your `PATH`. From an extracted
package, use `./bin/firominer` on Linux or `.\bin\firominer.exe` in Windows
PowerShell. For command line help, run:

```sh
firominer --help
firominer --help-ext con
```

### Examples connecting to pools

For Firo pool mining, replace `WALLET`, `WORKER` and `PASSWORD` with your
Firo payout address, worker name and pool password:

```sh
./bin/firominer -P "stratum+tcp://WALLET.WORKER:PASSWORD@firo.cedric-crispin.com:4064"
```

Windows PowerShell:

```powershell
.\bin\firominer.exe -P "stratum+tcp://WALLET.WORKER:PASSWORD@firo.cedric-crispin.com:4064"
```

Follow the pool's login requirements. URL-encode reserved characters in login
values. Repeat `-P` with additional pool URLs to configure failover in order;
`-P exit` ends the cycle when reached. Use `--help-ext con` for connection schemes
and `--help-ext misc` for retry and failover settings.

### Device selection and local testing

By default, the miner uses detected GPUs and prefers CUDA for devices available
through both CUDA and OpenCL. Use `-G` for OpenCL only or `-U` for CUDA only.
List devices with the same backend selection you will use for mining:

```sh
firominer -G --list-devices
firominer -U --list-devices
```

Select devices with `--cl-devices 0 1` or `--cu-devices 0 1`. Only options for
backends compiled into the executable are available. Use `--help-ext cl` or
`--help-ext cu` for their work-size settings.

To exercise a GPU locally without a pool connection:

```sh
firominer -G -M 0 --diff 0.01
```

`-M` (`--benchmark`) and `-Z` (`--simulation`) select the same simulation mode.
The argument is the starting block height; the simulation advances on accepted
solutions. Allow DAG generation and kernel compilation to finish, then press
Ctrl-C to stop. Block 0 only starts at the initial epoch. Test a recent height
and the intended network to exercise the corresponding DAG size. See
[hardware testing](docs/TESTING.md) for validation and performance comparisons.

### Solo mining and network selection

Run a synchronized Firo daemon with RPC enabled and use its configured RPC
credentials and port. Firo's `getwork://` (or `http://`) connections require a
block reward address. These URI schemes use Firo's `getblocktemplate` RPC.
Replace `FIRO_REWARD_ADDRESS` below with your payout address:

```sh
firominer -P "getwork://rpcuser:rpcpass@127.0.0.1:8888" -r FIRO_REWARD_ADDRESS
```

Use `--firopow-network testnet`, `devnet`, or `regtest` when connecting to those networks; the default is `mainnet`. The miner rejects daemon templates whose advertised epoch disagrees with the selected network. Stratum jobs use the selected network's epoch schedule, with a warning for conflicting pool metadata. Height-bearing EthereumStratum/1.0.0 (`stratum2+tcp`) jobs remain supported.

To include a pool-style signature in blocks you solo-mine, first apply the
[companion daemon patch](patches/README.md) to Firo and rebuild your node, then use:

```sh
firominer -P "getwork://rpcuser:rpcpass@127.0.0.1:8888" -r FIRO_REWARD_ADDRESS --coinbase-message "Mined by my rig"
```

Messages may contain up to 80 UTF-8 bytes. Leaving the option empty preserves
normal behavior with unmodified nodes. With a message set, the miner requires
the node to acknowledge it before accepting work; an unmodified node is rejected.
The patch is also included in installed packages under `share/firominer/patches/`.

Windows 10 version 1903 or newer is needed for Unicode coinbase messages
([Windows UTF-8 support](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)).
Pools construct their own coinbase and cannot accept a worker-supplied message
through this miner's Stratum protocol.

`--work-timeout` reconnects Stratum sessions that receive no new job for 600 seconds by default. Values from 180 to 1000000 seconds are accepted. Getwork requests have a 30-second response deadline and a 16 MiB response body limit.

### Monitoring and device recovery

The API is disabled by default. Add `--api-bind 127.0.0.1:3333` to a mining
command to enable it locally, or `--api-bind 127.0.0.1:-3333` for read-only
access. HTTP monitoring and TCP JSON-RPC share the port. See the
[API documentation](docs/API_DOCUMENTATION.md) for methods and response formats.

When `--api-password` is set, the HTTP status page and `/getstat1` return HTTP 401. Authenticate the plain TCP JSON-RPC API with `api_authorize` to read status or control the miner. HTTP has no password authentication mechanism. Without a password, HTTP monitoring remains available and hides pool URI credentials.

GPU devices wait for a new job after exhausting their assigned nonce range and log the reason for idling. This preserves other devices' ranges and pool extranonce bits. Epoch or kernel initialization failures pause the device and are retried on the next job. `--cl-global-work` controls the multiplier directly; it is not rounded to a power of two.

Very low-difficulty development networks use small GPU batches and may be limited by launch overhead. When even one work group produces more solutions than the result buffer holds, excess results are discarded safely.

## Build

### Continuous Integration and development builds

GitHub Actions runs core tests normally and under AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer. Release builds for Linux and Windows each provide CUDA 12.9 Update 2 + OpenCL and OpenCL-only packages. All include the API server and CPU diagnostics, selected explicitly with `--cpu`; normal runs still select GPUs. The OpenCL-only builds also run the core tests and smoke-test the packaged executable.

Packages include runtime libraries, documentation, source/build identification, and checksums. They require a compatible GPU driver; the CUDA package requires an NVIDIA driver even when selecting another backend. Linux packages target Ubuntu 22.04 or newer compatible x86-64 systems, and Windows packages target Windows 10/11 x64. See [Testing PR artifacts](docs/TESTING.md) for setup and a functional test guide.

Downloads appear in the associated workflow run's artifacts for pull requests and pushes to `main`. These are unsigned development packages. A tag matching `v` plus `PROJECT_VERSION` (for example, `v1.2.0`) publishes the same verified packages as a GitHub release. CI has no physical GPUs, so device execution, accepted pool shares, and hashrate still require hardware testing.

### Building from source

The project uses C++17, CMake and Hunter to build its dependencies. Initialize
submodules before configuring. The examples below use CMake 3.20 or newer within
the 3.x series and build
OpenCL, the API and tests; CUDA is optional. Python 3.9+ enables the OpenCL
kernel embedding test, and Clang enables offline OpenCL kernel compilation tests.

#### Linux

The Linux packaging job builds on Ubuntu 22.04 with GCC. Install its build
prerequisites, then run these commands from the repository root:

```sh
sudo apt-get update
sudo apt-get install build-essential ca-certificates cmake git ninja-build perl python3 mesa-common-dev libglu1-mesa-dev freeglut3-dev
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DETHASHCL=ON -DETHASHCUDA=OFF -DAPICORE=ON -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/stage"
```

The installed miner is `stage/bin/firominer`. For CUDA support, install CUDA
Toolkit 12.9 Update 2 (the version used by CI), set `-DETHASHCUDA=ON`, and pass
`-DCUDA_TOOLKIT_ROOT_DIR=/usr/local/cuda` if needed. A local installation does
not bundle CUDA libraries automatically; the CI packaging steps add them.

#### Windows

Install Visual Studio C++ build tools with the v142 (VS 2019) x64 toolset,
CMake, Git and Perl. For a VS 2019 installation, open its x64 Native Tools
Command Prompt and run these commands from the repository root:

```bat
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -T v142 -DETHASHCL=ON -DETHASHCUDA=OFF -DAPICORE=ON -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build --build-config Release --output-on-failure
cmake --install build --config Release --prefix stage
```

The installed miner is `stage\bin\firominer.exe`. For CUDA support, install
CUDA Toolkit 12.9 Update 2 and configure with `-DETHASHCUDA=ON` and
`-DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9"`.
CI uses VS 2022 with the v142 toolset and explicitly sets Hunter's compiler
environment to match. See the Windows configure steps in
[the CI workflow](.github/workflows/ci.yaml) when using that setup.

#### Build options and platform coverage

| CMake option | Default | Purpose |
| --- | --- | --- |
| `ETHASHCL` | `ON` | OpenCL backend |
| `ETHASHCUDA` | `ON` | CUDA backend |
| `ETHASHCPU` | `OFF` | Development CPU backend, enabled at runtime with `--cpu` |
| `APICORE` | `ON` | HTTP and TCP JSON-RPC API |
| `ETHDBUS` | `OFF` | D-Bus support |
| `DEVBUILD` | `OFF` | Developer logging |
| `BUILD_TESTING` | `ON` | CTest targets |

For core tests without GPU SDKs, configure with `-DETHASHCL=OFF`,
`-DETHASHCUDA=OFF` and `-DETHASHCPU=ON`. Tests check reference hashes, miner state,
protocol parsing and, when enabled, the API. They do not replace
GPU execution tests. The code contains macOS support, but the current CI
workflow does not build or package macOS.

## FiroPoW parameters

FiroPoW uses the ProgPoW 0.9.4 parameters with a program period of one block.
These are hashing rules, not performance tuning options. Changing them produces
hashes that do not match Firo's proof of work. The implementation is in
[`libcrypto/progpow.hpp`](libcrypto/progpow.hpp) and
[`libcrypto/ethash.cpp`](libcrypto/ethash.cpp).

DAG size depends on the epoch derived from the block height and selected
network. The code uses `floor(height / 1300)` until the network's DAG reduction
height, then fixes the epoch as follows:

| Network | Reduction height | Fixed epoch from that height |
| --- | --- | --- |
| Mainnet | 1,205,100 | 650 |
| Testnet | 189,800 | 100 |
| Devnet / regtest | 3,900 | 1 |

Use `--firopow-network` to match the daemon or pool. Tune GPU launch settings
through the CLI backend options instead of changing algorithm constants.

## Maintainers & Authors

[![Discord](https://img.shields.io/badge/discord-join%20chat-blue.svg)](https://discord.gg/uvyuqWm)

The list of current and past maintainers, authors and contributors to the firominer project.
Ordered alphabetically. [Contributors statistics since 2015-08-20].

| Name                  | Contact                                                      |     |
| --------------------- | ------------------------------------------------------------ | --- |
| Jeremy Anderson       | [@Blondfrogs](https://github.com/Blondfrogs)     | --- |
| Traysi                | [@traysi](https://github.com/traysi)                         | --  |
| Andrea Lanfranchi     | [@AndreaLanfranchi](https://github.com/AndreaLanfranchi)     | ETH: 0xa7e593bde6b5900262cf94e4d75fb040f7ff4727 |
| EoD                   | [@EoD](https://github.com/EoD)                               |     |
| Genoil                | [@Genoil](https://github.com/Genoil)                         |     |
| goobur                | [@goobur](https://github.com/goobur)                         |     |
| Marius van der Wijden | [@MariusVanDerWijden](https://github.com/MariusVanDerWijden) | ETH: 0x57d22b967c9dc64e5577f37edf1514c2d8985099 |
| Paweł Bylica          | [@chfast](https://github.com/chfast)                         | ETH: 0x8FB24C5b5a75887b429d886DBb57fd053D4CF3a2 |
| Philipp Andreas       | [@smurfy](https://github.com/smurfy)                         |     |
| Stefan Oberhumer      | [@StefanOberhumer](https://github.com/StefanOberhumer)       |     |
| ifdefelse             | [@ifdefelse](https://github.com/ifdefelse)                   |     |
| Won-Kyu Park          | [@hackmod](https://github.com/hackmod)                       | ETH: 0x89307cb2fa6b9c571ab0d7408ab191a2fbefae0a |
| Ikmyeong Na           | [@naikmyeong](https://github.com/naikmyeong)                 |     |


## Contribute

All bug reports, pull requests and code reviews are very much welcome.


## License

Licensed under the [GNU General Public License, Version 3](LICENSE).


## F.A.Q.

### How much GPU memory do I need?

Each GPU needs enough available memory for the current network's DAG, light
cache and working buffers. The requirement depends on the network's epoch
schedule, including the DAG reduction and fixed epoch described above. Check the
miner's DAG and memory logs at the height you intend to mine; a fixed claim
such as "4 GB is enough" becomes outdated. A device that cannot allocate the
required memory cannot mine that epoch.

### Which performance settings should I use?

Start with the defaults, then compare accepted shares and sustained hashrate
on your hardware. Use `--help-ext cl` and `--help-ext cu` for supported options.
`--cl-global-work` is a direct multiplier; it need not be a power of two.
OpenCL helper inlining is enabled by default. Use `--cl-no-inline` for the legacy
compiler workaround if the default kernel produces invalid results. Compilation
failures automatically retry the legacy kernel with a warning.
`--cl-subgroup` optionally replaces DAG-offset workgroup barriers with subgroup
broadcasts on detected AMD GPUs with `cl_khr_subgroups` and an OpenCL C 2.0
compiler. It defaults to off. Other vendors and unsupported lane layouts use
portable broadcasts; a subgroup build failure retries the portable kernel.
Compare sustained hashrate on your card before keeping this option enabled.
Keep host solution verification enabled by leaving out `--noeval`.

### Can I CPU mine?

A development CPU backend is available when built with `-DETHASHCPU=ON`.
Select it explicitly with `--cpu`; it is intended for diagnostics and testing.
The CI packages include it, but normal mining runs select GPUs only.

### Why does the miner fail to load CUDA or OpenCL?

Use a package that matches your installed driver and keep its companion
libraries in their original directories. The CUDA
package requires an NVIDIA driver even when you select OpenCL or CPU. On a
machine without that driver, use the OpenCL-only package. For a source build,
make the matching CUDA runtime/compiler libraries available to the executable.
See [package setup](docs/TESTING.md#choose-and-unpack-a-package).

### How do I select the same CUDA devices between runs?

Use `--list-devices` with your chosen backend and select its reported indexes.
Setting `CUDA_DEVICE_ORDER=PCI_BUS_ID` before launching asks CUDA to enumerate
by PCI bus ID. For example, use `export CUDA_DEVICE_ORDER=PCI_BUS_ID` in a Linux
shell or `$env:CUDA_DEVICE_ORDER = "PCI_BUS_ID"` in PowerShell, then list devices
again before choosing `--cu-devices`.

[Contributors statistics since 2015-08-20]: https://github.com/firoorg/firominer/graphs/contributors?from=2015-08-20
[Releases]: https://github.com/firoorg/firominer/releases
