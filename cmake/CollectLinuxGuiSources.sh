#!/usr/bin/env bash
# Collect the exact distro source and notices for the deployed Linux GUI libraries.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 STAGE_DIRECTORY" >&2
    exit 2
fi

stage="$(cd "$1" && pwd -P)"
manifest="$stage/share/firominer-gui/bundled-libraries.txt"
sources="$stage/sources/debian"
licenses="$stage/share/firominer-gui/licenses"
[[ -s "$manifest" ]] || { echo "Missing or empty dependency manifest: $manifest" >&2; exit 1; }
mkdir -p "$sources" "$licenses"
printf 'Installed file\tBinary package\tBinary version\tSource package\tSource version\n' > "$sources/packages.tsv"

declare -A source_packages=()
while IFS= read -r library || [[ -n "$library" ]]; do
    [[ "$library" == /* && -f "$library" ]] || {
        echo "Invalid installed library in manifest: $library" >&2
        exit 1
    }
    owner=''
    # dpkg records either /lib or /usr/lib paths on usrmerged installations.
    for path in "$library" "$(readlink -f "$library")"; do
        if [[ "$path" == /usr/* ]]; then
            alternate="${path#/usr}"
        else
            alternate="/usr$path"
        fi
        for candidate in "$path" "$alternate"; do
            if result="$(dpkg-query -S "$candidate" 2>/dev/null)"; then
                [[ "$result" != *$'\n'* ]] || {
                    echo "More than one package owns $candidate" >&2
                    exit 1
                }
                owner="${result%%: /*}"
                break
            fi
        done
        [[ -z "$owner" ]] || break
    done
    [[ "$owner" =~ ^[a-z0-9][a-z0-9+.-]*(:[a-z0-9-]+)?$ ]] || {
        echo "Could not identify one Debian package owning $library" >&2
        exit 1
    }
    metadata="$(dpkg-query -W -f='${Version}\t${source:Package}\t${source:Version}' "$owner")"
    IFS=$'\t' read -r version source_package source_version <<< "$metadata"
    [[ -n "$version" && -n "$source_package" && -n "$source_version" ]] || {
        echo "Missing source metadata for $owner" >&2
        exit 1
    }
    printf '%s\t%s\t%s\t%s\t%s\n' "$library" "$owner" "$version" "$source_package" "$source_version" >> "$sources/packages.tsv"
    package="${owner%%:*}"
    cp -L "/usr/share/doc/$package/copyright" "$licenses/$package.copyright"
    source_packages["$source_package=$source_version"]=1
done < "$manifest"

printf '%s\n' "${!source_packages[@]}" | LC_ALL=C sort > "$sources/source-packages.txt"
while IFS= read -r source_specification; do
    (cd "$sources" && apt-get source --download-only --only-source "$source_specification")
done < "$sources/source-packages.txt"

cat > "$sources/README.txt" <<'EOF'
Corresponding source for bundled Linux GUI libraries

packages.tsv maps each original installed library/plugin to its Debian binary
and source package versions. source-packages.txt records the exact source
versions downloaded through Ubuntu's authenticated APT repositories. The .dsc,
upstream archives and Debian archives/patches here form complete source packages.
Notices are in share/firominer-gui/licenses/*.copyright at the package root.

The binaries were taken from Ubuntu 22.04 packages. Only their runtime library
search paths were changed with patchelf for this relocatable bundle; no library
source changes were applied. GUI libraries are isolated in lib/firominer-gui.
The host supplies glibc, its loader, the compiler runtime and graphics drivers.

To rebuild a library on Ubuntu 22.04, enable matching deb-src repositories and
install build-essential and dpkg-dev. For the source package/version listed in
source-packages.txt, install its dependencies with:
  sudo apt-get build-dep SOURCE_PACKAGE=SOURCE_VERSION
Extract the included source and build its Debian packages:
  dpkg-source -x PACKAGE.dsc
  cd EXTRACTED_SOURCE_DIRECTORY
  dpkg-buildpackage -b -uc -us
The included debian/rules and patches describe the distribution's build.

To use modified libraries, keep their SONAMEs compatible, copy their shared
libraries/plugins into the matching locations under lib/firominer-gui, and
retain the bundle's relative RUNPATHs (libraries: $ORIGIN; plugins located two
directories below them: $ORIGIN/../..). See docs/GUI.md for rebuilding the GUI.
The bundle does not prevent replacing or debugging modified Qt libraries.
EOF
