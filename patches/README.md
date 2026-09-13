# Solo coinbase messages

Stock Firo owns the complete coinbase in its `getblocktemplate` / `pprpcsb` path
and has no message parameter. The miner's `--coinbase-message` option therefore
requires this companion daemon patch. Ordinary solo mining with an empty message
works with the stock daemon. Stratum pools choose their own coinbase and cannot
accept a miner-selected tag.

`firo-coinbase-message.patch` targets
[firoorg/firo at 136cd9d45348ab1ad0fdc87541ec95d80bb180c8](https://github.com/firoorg/firo/tree/136cd9d45348ab1ad0fdc87541ec95d80bb180c8)
(Firo 0.14.15.3 source). Check applicability in your daemon checkout before
applying; build and restart that daemon using its normal build instructions:

```sh
git apply --check /path/to/firominer/patches/firo-coinbase-message.patch
git apply /path/to/firominer/patches/firo-coinbase-message.patch
```

The extension accepts `getblocktemplate({"coinbase_message":"your text"}, reward_address)`.
It appends a script data push to the existing coinbase input, preserving its
height/OP_RETURN prefix, special transaction payload, reward and mandatory payees.
Messages are limited to 80 UTF-8 bytes and the complete scriptSig to 100 bytes.
An empty message leaves the original coinbase untouched. The RPC result echoes
`coinbase_message`; the miner refuses work when a requested message is not
confirmed, so an unpatched daemon cannot silently ignore the option.

The shared base template remains unchanged. Cached work is reused only for the
same coinbase, version and target; different addresses and messages retain their
own cached blocks for `pprpcsb`. Existing template refresh and stale-tip rules
still apply. The patch also locks the cache while copying a submitted job.

The patch includes a functional regression for acknowledgement, byte bounds,
different messages/reward addresses, empty-message reset and outstanding jobs.
After building the patched daemon and CLI, run from the Firo checkout:

```sh
FIROD="$PWD/build/bin/firod" FIROCLI="$PWD/build/bin/firo-cli" \
  python3 qa/rpc-tests/getblocktemplate_coinbase.py
```

A smaller check uses the actual Firo script implementation without running a
daemon. Supply the include paths for dependencies from your Firo build as needed:

```sh
c++ -std=c++20 -I/path/to/firo/src \
  /path/to/firominer/patches/test_coinbase_script.cpp -o /tmp/coinbase-script-check
/tmp/coinbase-script-check
```

The patch applicability and script check were verified locally. The daemon
functional regression is supplied but has not been run against a rebuilt patched
daemon in this workspace.
