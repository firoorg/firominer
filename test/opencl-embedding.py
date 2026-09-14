"""Check the generated OpenCL header against MSVC's per-literal size limit."""

import pathlib
import re
import subprocess
import sys
import tempfile


def check_embedding(cmake, generator, source, header):
    subprocess.run([
        cmake,
        f"-DTXT2STR_SOURCE_FILE={source}",
        "-DTXT2STR_VARIABLE_NAME=kernel",
        f"-DTXT2STR_HEADER_FILE={header}",
        "-P", generator,
    ], check=True)
    literals = re.findall(r'R"delim\((.*?)\)delim"', header.read_text(), re.DOTALL)
    assert literals, "No raw string literals found"
    assert max(map(len, literals)) <= 16380, "String literal exceeds MSVC C2026 limit"
    assert "".join(literals) == "\n\n" + source.read_text() + "\n\n", "Embedded text changed"


if __name__ == "__main__":
    cmake, generator, kernel = sys.argv[1:]
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        header = directory / "kernel.h"
        check_embedding(cmake, generator, pathlib.Path(kernel), header)
        source = directory / "fixture.cl"
        for contents in ("", "identifier" * 3300 + '\n"quoted"; \\continuation\n'):
            source.write_text(contents)
            check_embedding(cmake, generator, source, header)
    print("OpenCL embedding preserves all fixtures and respects the MSVC literal limit")
