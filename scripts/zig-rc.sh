#!/bin/sh
# Resource compiler for the Linux cross build.
#
# `zig rc` (resinator, bundled with the ziglang Python package) is a drop-in
# rc.exe, but it writes the binary .res format rather than the COFF object a
# linker needs. This wrapper compiles the resource script with `zig rc` and then
# converts the result with tools/res_to_coff.py, so the cross build embeds the
# same manifest, icon and version block as the MSVC build.
#
# CMake invokes it as:
#   zig-rc.sh <defines> <includes> <flags> /fo<object> <source.rc>
# (the output may also arrive as `/fo <object>`), with the source last.
#
# Note: the argument list is passed on with word splitting, as in zig-cxx.sh, so a
# build tree whose path contains spaces is not supported by the cross build.

set -e

root="$(cd "$(dirname "$0")/.." && pwd)"

# A relative temporary name: `zig rc` reads an argument starting with a slash as a
# Windows path (\foo) rather than a POSIX one, so the intermediate file has to live
# in the current directory (the build tree).
intermediate=".azy_skin_rc_$$.res"
trap 'rm -f "$intermediate"' EXIT INT TERM

options=""
object=""
source_file=""
want_object=0
index=0
count=$#

for arg in "$@"; do
    index=$((index + 1))
    if [ "$want_object" = 1 ]; then
        object="$arg"
        want_object=0
        continue
    fi
    case "$arg" in
        /fo|-fo)
            want_object=1
            continue
            ;;
        /fo*|-fo*)
            object="${arg#/fo}"
            object="${object#-fo}"
            continue
            ;;
    esac
    if [ "$index" -eq "$count" ]; then
        source_file="$arg"  # CMake puts the .rc file last
    else
        options="$options $arg"
    fi
done

if [ -z "$object" ] || [ -z "$source_file" ]; then
    echo "zig-rc.sh: expected /fo<output> and a source file; invoked as: $*" >&2
    exit 2
fi

# `/fo <path>` must come before the input file: zig rc stops option parsing at the
# first non-option argument.
# shellcheck disable=SC2086  # word splitting of the collected options is intended
python3 -m ziglang rc $options /fo "$intermediate" "$source_file"

python3 "$root/tools/res_to_coff.py" "$intermediate" "$object"
