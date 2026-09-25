# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## 1.5.1

### Added

- Mainnet solo mining in the desktop launcher, with Firo node setup guidance, a node test before launch, and accepted-block and connection status.
- Separate pool and solo settings; passwords remain in memory for the current GUI session.

### Changed

- Reflowed the launcher for smaller windows, larger text, and display scaling.
- Expanded GUI tests for solo node checks, startup failures, responsive layouts, and Windows builds.

### Fixed

- Rejected malformed solo work before starting the miner and cleared node readiness when startup fails.

## 1.5.0

### Added

- Qt Widgets desktop launcher for mainnet pool mining, with saved settings, live mining statistics and logs, tray support, and Light, Dark, and System themes.
- Bundled launcher, Qt runtime, corresponding GUI/library sources, and license notices in every Linux and Windows release package.
- Graceful Windows shutdown for miners started by the launcher.

### Changed

- Enabled subgroup DAG-offset exchanges by default on supported AMD OpenCL GPUs, using indexed subgroup shuffles when available.
- Retained automatic fallback from subgroup shuffles to broadcasts and then the portable kernel when compilation fails; added `--cl-no-subgroup` to select the portable path explicitly.
- Added GUI tests and relocated-package checks for Windows and Linux, including Linux offscreen and X11 plugin loading.

## 1.4.0

### Added

- Editable `mine_firo.bat` launcher in Windows packages and new Firo miner artwork.
- Packaged dependency versions and licenses for easier release auditing.

### Changed

- Updated managed dependencies to OpenSSL 3.5.8 LTS, Boost 1.92.0, JsonCpp 1.9.7, CLI11 2.6.2, and Khronos OpenCL components v2026.05.29.
- Recognized AMD and NVIDIA OpenCL GPUs by vendor ID across additional runtimes, with consistent PCI domain and function handling across CUDA and OpenCL.
- Released temporary GPU light caches after DAG generation, removed duplicate simulation verification, and reduced CUDA digest-reduction work.
- Required CMake 3.18 or newer and recorded resolved dependency versions in release packages.

### Fixed

- Allowed the managed Windows OpenCL loader to discover platforms exposed by current AMD and NVIDIA drivers.
- Kept overheated miners paused when temperature reads fail.
- Avoided duplicate CUDA and OpenCL subscriptions for unmatched NVIDIA devices in automatic mixed mode.
- Stopped processing buffered Stratum responses after a fatal response and reaped completed asynchronous scripts on Linux.
- Forwarded the selected MSVC runtime to legacy CUDA builds.

### Removed

- Redundant standalone Windows checksum asset; the combined `SHA256SUMS.txt` still covers every release archive.
- Obsolete OpenGL build prerequisites and the vendored legacy OpenCL C++ header.

## 1.3.0

### Added

- Optional `--cl-subgroup` DAG-offset broadcasts for compatible AMD OpenCL GPUs, with portable fallback.
- Combined `SHA256SUMS.txt` covering all Linux and Windows release archives.

### Changed

- Enabled OpenCL helper inlining by default, with automatic legacy retry on compilation failure and `--cl-no-inline` for legacy compiler compatibility.
- Updated release builds and bundled CUDA runtime/compiler libraries to CUDA 12.9 Update 2.
- Enabled C/C++ link-time optimization for Linux release packages while retaining a generic x86-64 CPU baseline.

### Fixed

- Preserved raw Windows ZIP archives when downloading release artifacts.
- Split embedded OpenCL source to stay within MSVC string literal limits.

### Removed

- Desktop launcher and its Python/Tk runtime requirement. Start the miner from a terminal.

## 1.2.0

### Added

- Desktop launcher for solo and pool mining, with live logs and OpenCL device detection.
- `--firopow-network` support for mainnet, testnet, devnet, and regtest epoch schedules.
- Optional UTF-8 solo coinbase messages, with a companion Firo daemon patch.
- Opt-in `--cl-experimental-inline` OpenCL kernel variant.
- Checksum-verified Linux and Windows CUDA/OpenCL release packages.

### Changed

- Improved CUDA and OpenCL work scheduling, nonce-range handling, low-difficulty behavior, and device recovery.
- Hardened Stratum and Getwork parsing, timeouts, reconnection, and asynchronous shutdown; solo Getwork now requires a reward address.
- Protected API status access when a password is configured and redacted pool credentials from the unauthenticated HTTP view.
- Made `--cl-global-work` control the OpenCL work multiplier instead of being ignored.
- Expanded FiroPoW, miner-state, protocol, API, GUI, sanitizer, and packaging tests.

### Fixed

- Fixed mismatched FiroPoW epochs and invalid solo Getwork jobs.
- Fixed overlapping OpenCL scratch storage and generated-kernel paths containing spaces.
- Fixed Getwork socket lifetime, pool shutdown ordering, malformed coinbase UTF-8, and out-of-bounds unit formatting.
- Fixed current Linux and Windows toolchain compatibility and Windows CUDA runtime packaging.

## 0.16.1rc0

### Fixed

- Display interval correction [#1606](https://github.com/ethereum-mining/ethminer/pull/1606)

## 0.16.0rc0

### Fixed

- Eliminated duplicate solutions with stratum2 on difficulty changes.
- Restored proper behavior of `-P` argument to identify workernames and emails

### Added

- Basic API authentication to protect exposure of API port to the internet [#1228](https://github.com/ethereum-mining/ethminer/pull/1228).
- Add `ispaused` information into response of `miner_getstathr` API query [#1232](https://github.com/ethereum-mining/ethminer/pull/1232).
- API responses return "ethminer-" as version prefix. [#1300](https://github.com/ethereum-mining/ethminer/pull/1300).
- Stratum mode autodetection. No need to specify `stratum+tcp` or `stratum1+tcp` or `stratum2+tcp`
- Connection failed due to login errors (wrong address or worker) are marked Unrecoverable and no longer used
- Replaced OpenCL kernel with opensource jawawawa OpenCL kernel
- Added support for jawawawa AMD binary kernels
- AMD auto kernel selection. Try bin first, if not fall back to OpenCL.
- API: New method `miner_setverbosity`. [#1382](https://github.com/ethereum-mining/ethminer/pull/1382).
- Implemented fast job switch algorithm on AMD reducing switch time to 1-2 milliseconds.
- Added localization support for output number formatting.
- Changed the --verbosity option to allow individual enable/disable of logging features.
- Improved hash rate measurement accuracy.

### Removed

- Command line argument `--stratum-email`: any information needed to authenticate on the pool **MUST BE** set using the `-P` argument

## 0.15.0rc1

### Fixed

- Restore the ability to auto-config OpenCL work size [#1225](https://github.com/ethereum-mining/ethminer/pull/1225).
- The API server totally broken fixed [#1227](https://github.com/ethereum-mining/ethminer/pull/1227).


## 0.15.0rc0

### Added

- Add `--tstop` and `--tstart` option preventing GPU overheating [#1146](https://github.com/ethereum-mining/ethminer/pull/1146), [#1159](https://github.com/ethereum-mining/ethminer/pull/1159).
- Added information about ordering CUDA devices in the README.md FAQ [#1162](https://github.com/ethereum-mining/ethminer/pull/1162).

### Fixed

- Reconnecting with mining pool improved [#1135](https://github.com/ethereum-mining/ethminer/pull/1135).
- Stratum nicehash. Avoid recalculating target with every job [#1156](https://github.com/ethereum-mining/ethminer/pull/1156).
- Drop duplicate stratum jobs (pool bug workaround) [#1161](https://github.com/ethereum-mining/ethminer/pull/1161).
- CLI11 command line parsing support added [#1160](https://github.com/ethereum-mining/ethminer/pull/1160).
- Farm mode (get_work): fixed loss of valid shares and increment in stales [#1215](https://github.com/ethereum-mining/ethminer/pull/1215).
- Stratum implementation improvements [#1222](https://github.com/ethereum-mining/ethminer/pull/1222).
- Build fixes & improvements [#1214](https://github.com/ethereum-mining/ethminer/pull/1214).

### Removed

- Disabled Debug configuration for Visual Studio [#69](https://github.com/ethereum-mining/ethminer/issues/69) [#1131](https://github.com/ethereum-mining/ethminer/pull/1131).
