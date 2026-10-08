#!/usr/bin/env python3
"""Check catalogs against Qt's extraction, including every plural and placeholder."""
import collections
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def messages(path):
    return {
        (context.findtext("name"), message.findtext("source")): message
        for context in ET.parse(path).getroot().findall("context")
        for message in context.findall("message")
        if message.find("translation").get("type") not in ("vanished", "obsolete")
    }


def placeholders(text):
    return collections.Counter(re.findall(r"%(?:L?[1-9][0-9]*|L?n)", text))


def main():
    lupdate, source_dir = sys.argv[1:]
    source_dir = Path(source_dir)
    with tempfile.TemporaryDirectory() as directory:
        extracted = Path(directory) / "sources.ts"
        subprocess.run([lupdate, *[str(source_dir / name) for name in (
            "main.cpp", "mainwindow.cpp", "mainwindow.h", "minercontroller.cpp", "minercontroller.h")],
            "-no-obsolete", "-ts", str(extracted)], check=True)
        sources = messages(extracted)

    for language, plurals in (("en", 2), ("zh_CN", 1), ("ar", 6), ("ru", 3), ("es", 2),
                             ("tr", 1), ("ja", 1), ("ko", 1), ("pt", 2), ("uk", 3), ("id", 1), ("ms", 1)):
        catalog = messages(source_dir / "translations" / f"firominer_{language}.ts")
        required = {key: value for key, value in sources.items()
                    if language != "en" or value.get("numerus") == "yes"}
        assert not required.keys() - catalog.keys(), (language, "missing messages", required.keys() - catalog.keys())
        for key, source in required.items():
            translation = catalog[key].find("translation")
            assert translation.get("type") != "unfinished", (language, key, "unfinished")
            forms = translation.findall("numerusform") if source.get("numerus") == "yes" else [translation]
            assert len(forms) == (plurals if source.get("numerus") == "yes" else 1), (language, key, "plural forms")
            for form in forms:
                text = "".join(form.itertext())
                assert text.strip(), (language, key, "empty translation")
                assert placeholders(text) == placeholders(key[1]), (language, key, "placeholders", text)
        print(f"{language}: {len(required)} messages complete, placeholders and plurals valid")


if __name__ == "__main__":
    main()
