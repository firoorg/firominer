# firominer (ethminer fork with ProgPoW implementation)

> firopow miner with OpenCL, CUDA and stratum support

**firominer** is an ProgPoW GPU mining worker: with firominer you can mine Firo, which relies on an ProgPoW-based Proof of Work thus including Ethereum ProgPoW and others. This is the actively maintained version of firominer. It originates from the [ethminer](https://github.com/ethereum-mining/ethminer) project. Check the original [ProgPoW](https://github.com/ifdefelse/progpow) implementation and [EIP-1057](https://eips.ethereum.org/EIPS/eip-1057) for specification.

## Features

* First commercial ProgPOW Firo miner software for miners.
* OpenCL mining
* Nvidia CUDA mining
* realistic benchmarking against arbitrary epoch/DAG/blocknumber
* on-GPU DAG generation (no more DAG files on disk)
* stratum mining without proxy
* OpenCL devices picking
* farm failover (getwork + stratum)
* desktop launcher for solo and pool mining, with live logs and OpenCL experiment detection
* custom solo coinbase messages with the companion Firo daemon patch


## Table of Contents

* [Install](#install)
* [Usage](#usage)
    * [Examples connecting to pools](#examples-connecting-to-pools)
* [Build](#build)
    * [Continuous Integration and development builds](#continuous-integration-and-development-builds)
    * [Building from source](#building-from-source)
* [Maintainers & Authors](#maintainers--authors)
* [Contribute](#contribute)
* [F.A.Q.](#faq)


## Install

[Releases][Releases]

Prebuilt **executables** for *Linux*, *macOS* and *Windows* are provided in
the [Releases] section.
Download an archive for your operating system and unpack the content to a place
accessible from command line. The firominer is ready to go.

| Builds | Release |
| ------ | ------- |
| Last   | [GitHub release](https://github.com/firoorg/firominer/releases) 


On Windows, install the [Microsoft Visual C++ 2015-2022 Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).
CUDA or OpenCL errors require a current NVIDIA or AMD graphics driver.

## Usage

Use the desktop launcher below, or launch **firominer** from a terminal.
For a full list of command line options, run:

```sh
firominer --help
```

### Desktop GUI

The launcher requires Python 3.9 or newer with Tk 8.6 or newer. Windows and macOS Python
installers from python.org include Tk; on Ubuntu/Debian install `python3-tk`.
Apple's old system Python/Tk is not supported; use a current Python installer on macOS.
Run from the source checkout:

```sh
python3 gui/firominer_gui.py
```

Installed packages include `bin/firominer-gui.py` on Linux/macOS
(`python3 bin/firominer-gui.py`) or `bin/firominer-gui.pyw` on Windows
(double-click it, or run `py bin/firominer-gui.pyw`). Select the miner executable
if it is not found automatically.

Choose **Pool** or **Solo**, enter the endpoint and login details, and select
the network. Solo mining also needs your block reward address. Start and Stop
control the miner, and the log shows its connection, device and share status.
Windows 10 version 1903 or newer is needed for Unicode coinbase messages
([Windows UTF-8 support](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)).
Credentials are kept only for the current session. The launcher passes them to
the miner on its command line, as the CLI does.

The OpenCL scan enables **Experimental OpenCL inline** only when the selected
miner detects a usable OpenCL GPU/accelerator. Checking it selects OpenCL mining
so the experiment runs even on cards that also support CUDA. It is off by default;
see [hardware verification](docs/TESTING.md#experimental-opencl-inlining).

The optional coinbase message is available for solo mining with a patched node;
see the next section. Pools construct their own coinbase and cannot accept a
worker-supplied message through this miner's Stratum protocol.

### Examples connecting to pools

Use your pool's advertised endpoint and replace the placeholders:

`./firominer -P stratum+tcp://WALLET.WORKER:PASSWORD@pool.example.invalid:PORT` or

`firominer.exe -P stratum+tcp://WALLET.WORKER:PASSWORD@pool.example.invalid:PORT`

### Solo mining and network selection

Firo's `getwork://` (or `http://`) connections require a block reward address:

```sh
firominer -P getwork://rpcuser:rpcpass@127.0.0.1:8888 -r <Firo-address>
```

Use `--firopow-network testnet`, `devnet`, or `regtest` when connecting to those networks; the default is `mainnet`. The miner rejects daemon templates whose advertised epoch disagrees with the selected network. Stratum jobs use the selected network's epoch schedule, with a warning for conflicting pool metadata. Height-bearing EthereumStratum/1.0.0 (`stratum2+tcp`) jobs remain supported.

To include a pool-style signature in blocks you solo-mine, first apply the
[companion daemon patch](patches/README.md) to Firo and rebuild your node, then use:

```sh
firominer -P getwork://rpcuser:rpcpass@127.0.0.1:8888 -r <Firo-address> --coinbase-message "Mined by my rig"
```

Messages may contain up to 80 UTF-8 bytes. Leaving the option empty preserves
normal behavior with unmodified nodes. With a message set, the miner requires
the node to acknowledge it before accepting work; an unmodified node is rejected.
The patch is also included in installed packages under `share/firominer/patches/`.

`--work-timeout` reconnects Stratum sessions that receive no new job for 600 seconds by default. Values from 180 to 1000000 seconds are accepted. Getwork requests have a 30-second response deadline and a 16 MiB response body limit.

### Monitoring and device recovery

When `--api-password` is set, the HTTP status page and `/getstat1` return HTTP 401. Authenticate the plain TCP JSON-RPC API with `api_authorize` to read status or control the miner. HTTP has no password authentication mechanism. Without a password, HTTP monitoring remains available and hides pool URI credentials.

GPU devices wait for a new job after exhausting their assigned nonce range and log the reason for idling. This preserves other devices' ranges and pool extranonce bits. Epoch or kernel initialization failures pause the device and are retried on the next job. `--cl-global-work` controls the multiplier directly; it is not rounded to a power of two.

Very low-difficulty development networks use small GPU batches and may be limited by launch overhead. When even one work group produces more solutions than the result buffer holds, excess results are discarded safely.

## Build

### Continuous Integration and development builds

GitHub Actions runs core tests normally and under AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer. Release builds for Linux and Windows each provide CUDA 11.8 + OpenCL and OpenCL-only packages. All include the API server and CPU diagnostics, selected explicitly with `--cpu`; normal runs still select GPUs. The OpenCL-only builds also run the core tests and smoke-test the packaged executable.

Packages include runtime libraries, documentation, source/build identification, and checksums. They require a compatible GPU driver; the CUDA package requires an NVIDIA driver even when selecting another backend. Linux packages target Ubuntu 22.04 or newer compatible x86-64 systems, and Windows packages target Windows 10/11 x64. See [Testing PR artifacts](docs/TESTING.md) for setup and a functional test guide.

Downloads appear in the associated workflow run's artifacts for pull requests and pushes to `main`. These are unsigned development packages. CI has no physical GPUs, so device execution, accepted pool shares, and hashrate still require hardware testing.

After cloning this repository into `firominer`, it can be built with commands like:

### Ubuntu / OSX
```
cd firominer
git submodule update --init --recursive
mkdir build
cd build
cmake .. -DETHASHCUDA=ON -DETHASHCL=ON -DAPICORE=ON
make -sj $(nproc)
```


### Windows

### Prerequisites:
1. Install Visual Studios (2019) (with the additional installation package "C++ Cmake Tools for Windows)
2. Install latest perl to C:\Perl (https://www.perl.org/get.html)
   Follow the steps outlined and the default perl installtion should work

### Building via Visual Studios Command Line:
Open "Developer Command Prompt for VS 2019"
1. Open StartMenu and search for "Developer Command Prompt for VS 2019"
2. Follow these steps:
```
cd C:\Users\USER_NAME\PATH_TO_FIROMINER\firominer
git submodule update --init --recursive
mkdir build
cd build
cmake -G "Visual Studio 16 2019" -A X64 -H. -Bbuild -DETHASHCL=ON -DETHASHCUDA=ON -DAPICORE=ON ..
cd build
cmake --build . --config Release
```
(Yes, two nested build/build directories.)

### Building via Visual Studios GUI (This build doesn't seem to work for some 20XX Nvidia cards)
   1. Open Visual Studios
   2. Open CMakeLists.txt file with File->Open->CMake
   3. Wait for intelligence to build the cache (this can take some time)
   4. Build the project (CTRL+SHIFT+B) or find the build command in the menu

ProgPoW can be tuned using the following parameters.  The proposed settings have been tuned for a range of existing, commodity GPUs:

* `PROGPOW_PERIOD`: Number of blocks before changing the random program
* `PROGPOW_LANES`: The number of parallel lanes that coordinate to calculate a single hash instance
* `PROGPOW_REGS`: The register file usage size
* `PROGPOW_DAG_LOADS`: Number of uint32 loads from the DAG per lane
* `PROGPOW_CACHE_BYTES`: The size of the cache
* `PROGPOW_CNT_DAG`: The number of DAG accesses, defined as the outer loop of the algorithm (64 is the same as Ethash)
* `PROGPOW_CNT_CACHE`: The number of cache accesses per loop
* `PROGPOW_CNT_MATH`: The number of math operations per loop

The value of these parameters has been tweaked to use 0.9.4 specs with a PROGPOW_PEROD of 1 to fit Firo's blocktimes.  See [this medium post](https://medium.com/@ifdefelse/progpow-progress-da5bb31a651b) for details.

| Parameter             | 0.9.2 | 0.9.3 | 0.9.4 |
|-----------------------|-------|-------|--------|
| `PROGPOW_PERIOD`      | `50`  | `10`  |  `1`   |
| `PROGPOW_LANES`       | `16`  | `16`  |  `16`  |
| `PROGPOW_REGS`        | `32`  | `32`  |  `32`  |
| `PROGPOW_DAG_LOADS`   | `4`   | `4`   |  `4`   |
| `PROGPOW_CACHE_BYTES` | `16x1024` | `16x1024` | `16x1024` |
| `PROGPOW_CNT_DAG`     | `64`  | `64`  | `64`  |
| `PROGPOW_CNT_CACHE`   | `12`  | `11`  | `11`  |
| `PROGPOW_CNT_MATH`    | `20`  | `18`  | `18`  |

Epoch length = 1300 blocks

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


## F.A.Q

### Why is my hashrate with Nvidia cards on Windows 10 so low?

The new WDDM 2.x driver on Windows 10 uses a different way of addressing the GPU. This is good for a lot of things, but not for ETH mining.

* For Kepler GPUs: I actually don't know. Please let me know what works best for good old Kepler.
* For Maxwell 1 GPUs: Unfortunately the issue is a bit more serious on the GTX750Ti, already causing suboptimal performance on Win7 and Linux. Apparently about 4MH/s can still be reached on Linux, which, depending on ETH price, could still be profitable, considering the relatively low power draw.
* For Maxwell 2 GPUs: There is a way of mining ETH at Win7/8/Linux speeds on Win10, by downgrading the GPU driver to a Win7 one (350.12 recommended) and using a build that was created using CUDA 6.5.
* For Pascal GPUs: You have to use the latest WDDM 2.1 compatible drivers in combination with Windows 10 Anniversary edition in order to get the full potential of your Pascal GPU.

### Why is a GTX 1080 slower than a GTX 1070?

Because of the GDDR5X memory, which can't be fully utilized for FIRO mining (yet).

### Are AMD cards also affected by slowdowns with increasing DAG size?

Only GCN 1.0 GPUs (78x0, 79x0, 270, 280), but in a different way. You'll see that on each new epoch (30K blocks), the hashrate will go down a little bit.

### Can I still mine FIRO with my 2GB GPU?

Not really, your VRAM must be above the DAG size (Currently about 4 GB.) to get best performance. Without it severe hash loss will occur.

### What are the optimal launch parameters?

The default parameters are fine in most scenario's (CUDA). For OpenCL it varies a bit more. Just play around with the numbers and use powers of 2. GPU's like powers of 2.

### What does the `--cuda-parallel-hash` flag do?

[@davilizh](https://github.com/davilizh) made improvements to the CUDA kernel hashing process and added this flag to allow changing the number of tasks it runs in parallel. These improvements were optimised for GTX 1060 GPUs which saw a large increase in hashrate, GTX 1070 and GTX 1080/Ti GPUs saw some, but less, improvement. The default value is 4 (which does not need to be set with the flag) and in most cases this will provide the best performance.

### What is firominer's relationship with [Genoil's fork]?

[Genoil's fork] was the original source of this version, but as Genoil is no longer consistently maintaining that fork it became almost impossible for developers to get new code merged there. In the interests of progressing development without waiting for reviews this fork should be considered the active one and Genoil's as legacy code.

### Can I CPU Mine?

No.

### CUDA GPU order changes sometimes. What can I do?

There is an environment var `CUDA_DEVICE_ORDER` which tells the Nvidia CUDA driver how to enumerates the graphic cards.
The following values are valid:

* `FASTEST_FIRST` (Default) - causes CUDA to guess which device is fastest using a simple heuristic.
* `PCI_BUS_ID` - orders devices by PCI bus ID in ascending order.

To prevent some unwanted changes in the order of your CUDA devices you **might set the environment variable to `PCI_BUS_ID`**.
This can be done with one of the 2 ways:

* Linux:
    * Adapt the `/etc/environment` file and add a line `CUDA_DEVICE_ORDER=PCI_BUS_ID`
    * Adapt your start script launching firominer and add a line `export CUDA_DEVICE_ORDER=PCI_BUS_ID`

* Windows:
    * Adapt your environment using the control panel (just search `setting environment windows control panel` using your favorite search engine)
    * Adapt your start (.bat) file launching firominer and add a line `set CUDA_DEVICE_ORDER=PCI_BUS_ID` or `setx CUDA_DEVICE_ORDER PCI_BUS_ID`. For more info about `set` see [here](https://docs.microsoft.com/en-us/windows-server/administration/windows-commands/set_1), for more info about `setx` see [here](https://docs.microsoft.com/en-us/windows-server/administration/windows-commands/setx)

### nvrtc64_102_0.dll not found...

```text
Error: The code execution cannot be processed because nvrtc64_102_0.dll was not found.
or
error while loading shared libraries: libnvrtc.so.10.2: cannot open shared object file: No such file or directory
```

You have to upgrade your Nvidia drivers. Install cuda 10.2.


[Amazon S3 is needed]: https://docs.travis-ci.com/user/uploading-artifacts/
[cpp-ethereum]: https://github.com/ethereum/cpp-ethereum
[Contributors statistics since 2015-08-20]: https://github.com/firoorg/firominer/graphs/contributors?from=2015-08-20
[Genoil's fork]: https://github.com/Genoil/cpp-ethereum
[Releases]: https://github.com/firoorg/firominer/releases
