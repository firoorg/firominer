"""Check CLI parsing and defaults without starting a miner or connecting to a pool."""

import subprocess
import sys


def check(binary, arguments, success, *expected):
    result = subprocess.run(
        [binary, *arguments], capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=10,
    )
    output = result.stdout + result.stderr
    if (result.returncode == 0) != success or any(text not in output for text in expected):
        raise AssertionError(
            f"Arguments {arguments!r}: unexpected exit {result.returncode} or output:\n{output}"
        )


if __name__ == "__main__":
    binary, = sys.argv[1:]
    # The omitted temperature stop defaults to zero, outside its explicit-input range.
    check(binary, ["--help"], True, "minimal usage : firominer")
    check(binary, ["-H", "con"], True, "Connections specifications :")
    check(binary, ["--version"], True, "Dependencies: Boost ", "; JsonCpp ", "; CLI11 ", "; OpenSSL ")
    check(binary, ["--help", "--tstop", "50"], True, "minimal usage : firominer")
    check(binary, ["--help", "--tstop", "0"], False, "Error:", "--tstop")

    for value in ("0", "1", "2"):
        check(binary, ["--help", "--ergodicity", value], True, "minimal usage : firominer")
    for value in ("-1", "3"):
        check(binary, ["--help", "--ergodicity", value], False, "Error:", "--ergodicity")

    for value in ("mainnet", "testnet", "devnet", "regtest"):
        check(binary, ["--help", "--firopow-network", value], True, "minimal usage : firominer")
    for value in ("invalid", "MAINNET"):
        check(binary, ["--help", "--firopow-network", value], False, "Error:", "--firopow-network")
    if sys.platform == "win32":
        import ctypes
        import uuid

        kernel32 = ctypes.windll.kernel32
        kernel32.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p]
        kernel32.CreateEventW.restype = ctypes.c_void_p
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        name = "Local\\FirominerStop-test-" + uuid.uuid4().hex
        check(binary, ["--help", "--shutdown-event", name], False, "Error:", "--shutdown-event")
        event = kernel32.CreateEventW(None, True, False, name)
        if not event:
            raise ctypes.WinError()
        try:
            check(binary, ["--help", "--shutdown-event", name], True, "minimal usage : firominer")
        finally:
            kernel32.CloseHandle(event)
    print("CLI help, dependency versions, defaults, ranges and set membership passed")
