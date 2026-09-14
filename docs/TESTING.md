# Testing PR artifacts

These packages contain the Release build intended for functional testing before a
release. They are unsigned development builds. Check `BUILD-INFO.txt` for the
source commit, build configuration, and CI run. GPU execution and pool acceptance
still need testing on real hardware; passing CI alone does not establish either.

Linux release CI uses `-O3` and C/C++ link-time optimization, enabled with
`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON` (CMake 3.9+). Configuration
fails if the toolchain cannot provide it. Windows releases already use `/O2`,
`/GL` and `/LTCG`. Packaged builds retain a generic x86-64 CPU baseline. GPU
mining kernels are optimized separately by the OpenCL driver or CUDA runtime
compiler; host build flags alone do not establish a mining speedup.

Managed OpenCL builds require CMake 3.16+ and pin the Khronos loader and headers
to `v2026.05.29`. The loader remains statically linked and must be rebuilt to
receive future driver-discovery fixes. On Windows, the `opencl-platform-discovery`
CTest compares its platforms with the system loader; it skips when system OpenCL
is unavailable. macOS and `HUNTER_ENABLED=OFF` builds use system OpenCL.

## Choose and unpack a package

| Package | Intended machine | Included backends |
| --- | --- | --- |
| Linux / Windows `cuda12.9-opencl` | NVIDIA 575.57.08+ (Linux) or 576.57+ (Windows); legacy GPU limits below | CUDA, OpenCL, CPU diagnostics, API |
| Linux / Windows `opencl` | OpenCL vendor driver installed | OpenCL, CPU diagnostics, API |

Linux packages target x86-64 Ubuntu 22.04 or compatible newer systems. Windows
packages target Windows 10/11 x64 and bundle the Visual C++ runtime libraries.
Install the GPU vendor's driver with CUDA/OpenCL support as
appropriate. The CUDA package supports Maxwell or newer NVIDIA GPUs. Maxwell,
Pascal, and Volta require an R575 or R580 driver; later driver branches no
longer support them. The package requires the listed driver floor even when
selecting OpenCL or CPU because its runtime-compiled PTX needs CUDA 12.9 driver
support. Use the OpenCL package on machines without that driver.

CUDA runtime/compiler libraries are bundled, so a full CUDA Toolkit installation
is unnecessary. GPU drivers are not bundled. Keep the extracted directory intact;
moving only the executable loses its companion libraries. The CPU backend is
selected only with `--cpu`; ordinary runs still select GPUs and prefer CUDA over
OpenCL for devices supporting both.

Verify the published archive checksum before unpacking, then verify the extracted
files against `SHA256SUMS`. Releases provide `SHA256SUMS.txt` covering all Linux
and Windows archives. Linux also has a `.sha256` file beside each archive;
`SHA256SUMS-windows.txt` lists both Windows ZIPs. From the download directory,
run `sha256sum --check --ignore-missing SHA256SUMS.txt` on Linux, or compare
`Get-FileHash .\firominer-windows-*.zip -Algorithm SHA256` with the manifest
in Windows PowerShell. Run commands below from the extracted directory. On
Windows PowerShell replace `./bin/firominer` with `.\bin\firominer.exe`.

## Startup and local GPU test

```sh
./bin/firominer --version
./bin/firominer --help
./bin/firominer --help-ext test
./bin/firominer --list-devices
./bin/firominer -G --list-devices
# CUDA package, NVIDIA machine only:
./bin/firominer -U --list-devices
```

Check that every expected GPU appears with the correct memory and backend. Device
indexes come from enumeration with the same backend selection used for mining.
For a brief test without any pool connection, run one backend at a time:

```sh
./bin/firominer -G -M 0 --diff 0.01 --HWMON 1
./bin/firominer -U -M 0 --diff 0.01 --HWMON 1
```

Allow DAG creation and kernel compilation to finish, observe accepted local
solutions and a stable hashrate for a few minutes, then press Ctrl-C. The process
should shut down cleanly. Repeat using a recent block height and the intended
`--firopow-network`; block 0 tests the initial epoch only. Simulation advances the
block on accepted solutions, exercising period changes. Very low difficulties
can be limited by GPU launch overhead and result-buffer capacity.

Test individual devices with `--cl-devices 0` or `--cu-devices 0`, then several
with `--cl-devices 0 1` or `--cu-devices 0 1`. Finally test the default selection on
mixed hardware. Keep host verification enabled (omit `--noeval`).

## OpenCL inlining and legacy compatibility

OpenCL mining uses forced helper inlining and direct private mix storage by
default, without the old volatile-array workaround. Each selected OpenCL miner
logs `OpenCL inline mix kernel enabled`. This requires an OpenCL C 1.2-compatible
compiler that supports `always_inline`. It introduces no new GPU instruction
requirement, but older drivers still need hardware verification. Use
`--cl-no-inline` to select the legacy workaround. The existing
`--cl-experimental-inline` flag remains accepted and explicitly enables inlining.
If both flags are supplied, the last one takes precedence for the initial build.
If compilation fails, the miner logs a warning and retries with the legacy
workaround, retaining it for subsequent periods on that miner. When subgroups
are requested, their portable fallback is tried first. If the legacy build also
fails, normal device error handling applies. This cannot detect a compiler that
accepts the kernel but produces incorrect results; keep host verification enabled.

Run the same workload with and without `--cl-no-inline`, keeping the device, driver,
clocks, local/global work sizes and other options fixed. Leave host verification
enabled and compare accepted, invalid, rejected and stale shares across several
periods before comparing hashrate. Include workgroup sizes 64, 128 and 256. Use
the legacy path if the inline variant fails compilation or produces invalid
results. CPU tests and generated-kernel compilation do not establish GPU
correctness or a speedup.

Use simulation for functional and period-transition checks. Easy simulated work
can change periods about every 200 ms, so it is not a stable kernel-throughput
benchmark. For performance comparisons, allow warmup and sample the same fixed
jobs/periods with a suitable GPU profiling setup, or compare repeated realistic
pool runs while recording the changing jobs and targets.

## Optional OpenCL subgroup broadcasts

`--cl-subgroup` enables an experimental DAG-offset broadcast path only on detected
AMD GPUs advertising `cl_khr_subgroups`. The option defaults to off and works with
both inline and legacy mix storage. It builds with OpenCL C 2.0; a build failure
disables subgroup broadcasts for that miner and retries the portable variant.
Other vendors keep portable broadcasts until they have been validated.

The kernel checks that each logical 16-lane hash fits in a contiguous subgroup
slice. If any lane has an incompatible layout, the entire workgroup uses the
original local-memory broadcast. Seed and digest sharing retain their memory
barriers. A successful subgroup build logs
`(subgroup broadcasts with lane-layout fallback)`; that message does not establish
that the runtime layout passed the check or that hashrate improved.

Compare runs with and without `--cl-subgroup` at the same periods and work sizes,
leaving host verification enabled. Check all returned mix digests against the CPU
reference, target selection, period transitions, and rejected/invalid shares
before comparing sustained hashrate. Include both mix variants, local sizes
64/128/256, and subgroup widths 32 and 64 where available. Also check a device
without the extension and a non-AMD GPU: requesting the option must leave their
broadcast path portable. The offline kernel test covers all four variants at
three periods and three workgroup sizes, 36 compilations in total.

## Pool and daemon tests

Replace all uppercase placeholders and the reserved example hostname before use:

```sh
./bin/firominer -P stratum+tcp://WALLET.WORKER:PASSWORD@pool.example.invalid:PORT
./bin/firominer -P getwork://RPCUSER:RPCPASSWORD@127.0.0.1:RPCPORT -r REWARD_ADDRESS --firopow-network regtest
```

Use the pool's advertised scheme and port; `--help-ext con` describes supported
formats. Getwork requires a reward address valid for the selected network. The
default network is `mainnet`; explicitly select `testnet`, `devnet`, or `regtest`
for those daemons or pools. Do not share logs containing wallet credentials,
pool passwords, RPC passwords, or API passwords.

Confirm accepted shares on the pool or accepted blocks on your test daemon,
including after a new job, period change, and epoch transition. Exercise normal
disconnect/reconnect and a second `-P` failover connection on test infrastructure.
Compare local, API, and pool hashrates after each has had time to average. Record
stale and rejected shares separately and investigate invalid proofs.

For `--coinbase-message`, use an isolated Firo test node with the companion patch
in `patches/` (installed under `share/firominer/patches/`). Follow its verification
instructions, including distinct messages at the same tip and a UTF-8 message.
Confirm the stock daemon is rejected when a message is requested, normal work
still succeeds when it is blank, and nonempty messages are rejected for Stratum
connections.

## API and CPU diagnostics

Add `--api-bind 127.0.0.1:-3333` to a running test for local read-only monitoring.
Without an API password, HTTP monitoring is available at
`http://127.0.0.1:3333/` and `/getstat1`. A negative port blocks API write methods.

For authenticated control testing use `--api-bind 127.0.0.1:3333
--api-password TEST_PASSWORD`. HTTP must return 401 when a password is configured.
On one plain TCP connection, send newline-terminated JSON requests, authorizing
before other calls:

```json
{"id":1,"jsonrpc":"2.0","method":"api_authorize","params":{"psw":"TEST_PASSWORD"}}
{"id":2,"jsonrpc":"2.0","method":"miner_getstatdetail"}
{"id":3,"jsonrpc":"2.0","method":"miner_pausegpu","params":{"index":0,"pause":true}}
{"id":4,"jsonrpc":"2.0","method":"miner_pausegpu","params":{"index":0,"pause":false}}
```

Verify pause/resume affects the selected device, failed authorization cannot read
status or control mining, and read-only mode rejects control requests. API TCP
traffic is unencrypted; keep these tests on loopback. See
[API documentation](API_DOCUMENTATION.md) for additional methods.

CPU diagnostics can exercise job handling on a machine without a GPU, using the
OpenCL package:

```sh
./bin/firominer --cpu --list-devices
./bin/firominer --cpu --cp-devices 0 -M 0 --diff 0.00000001
```

CPU mode is a development aid, not a production CPU miner: it stops searching
after the first valid solution for each job and waits for new work. It allocates
a dataset in system memory. It cannot validate GPU kernels or GPU performance.

## Report results

Include the source commit and archive checksum, OS, driver version, GPU models
and memory, backend, sanitized command line, network/height/epoch, and test
duration. Record DAG/compile success, accepted/rejected/stale shares, hashrate,
temperatures, multi-device behavior, API pause/resume, reconnect/failover, and
Ctrl-C shutdown. Attach relevant sanitized logs and state which cases remain
untested.
