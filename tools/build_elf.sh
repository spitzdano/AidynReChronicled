#!/usr/bin/env bash
# tools/build_elf.sh: assemble and link splat's output into build/aidyn.us.elf,
# make build/aidyn.us.z64 from it, and compare that with baserom.z64.
#
# The object list comes from splat's linker script, so nothing here is
# game-specific: per-game flags live in tools/build.conf.
#   .s   -> ${CROSS}as $ASFLAGS
#   .bin -> ${CROSS}objcopy -I binary -O elf32-tradbigmips
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
. tools/toolkit.env
. tools/build.conf

tools/check_rom.sh
eval "$("$TOOLKIT/.venv/bin/python" -I tools/splat_paths.py "$YAML")"
# Now set: LD_SCRIPT BUILD_PATH ELF UNDEF_FUNCS UNDEF_SYMS TARGET (all relative to the project root)
[[ -f $LD_SCRIPT ]] || { echo "$LD_SCRIPT missing: run tools/split.sh first" >&2; exit 1; }
for t in as ld objcopy; do
    command -v "${CROSS}$t" >/dev/null || { echo "${CROSS}$t not found (toolkit install.sh)" >&2; exit 1; }
done

mapfile -t objs < <(grep -oE '[^[:space:](]+\.o\(' "$LD_SCRIPT" | sed 's/($//' | sort -u)
(( ${#objs[@]} )) || { echo "no objects referenced in $LD_SCRIPT" >&2; exit 1; }

source_for() {  # build/<path>.o -> the file splat wrote
    local rel=${1#"$BUILD_PATH"/}; rel=${rel%.o}
    case $rel in
        *.s|*.bin|*.c) echo "$rel"; return ;;
    esac
    # o_as_suffix: True drops the source extension; it must be unambiguous.
    local found=() ext
    for ext in s bin c; do [[ -f $rel.$ext ]] && found+=("$rel.$ext"); done
    if (( ${#found[@]} != 1 )); then
        echo "cannot tell which source builds $1 (candidates: ${found[*]:-none})" >&2
        return 1
    fi
    echo "${found[0]}"
}

n_as=0 n_bin=0
for obj in "${objs[@]}"; do
    src=$(source_for "$obj")
    [[ -f $src ]] || { echo "missing source $src for $obj" >&2; exit 1; }
    mkdir -p "$(dirname "$obj")"
    [[ $obj -nt $src && $obj -nt tools/build.conf ]] && continue
    case $src in
        *.s)   "${CROSS}as" "${ASFLAGS[@]}" -o "$obj" "$src"; n_as=$((n_as + 1)) ;;
        *.bin) "${CROSS}objcopy" -I binary -O elf32-tradbigmips -B mips "$src" "$obj"; n_bin=$((n_bin + 1)) ;;
        *.c)   echo "C sources ($src) are not supported by build_elf.sh" >&2; exit 1 ;;
    esac
done
echo "build_elf.sh: assembled $n_as, converted $n_bin (of ${#objs[@]} objects; the rest were up to date)"

ld_scripts=()
for f in "$UNDEF_SYMS" "$UNDEF_FUNCS" "${LD_EXTRA_SCRIPTS[@]}"; do
    [[ -f $f ]] && ld_scripts+=("$f")  # symbol files are positional: a second -T would replace the main script
done
mkdir -p "$(dirname "$ELF")"
"${CROSS}ld" "${LDFLAGS[@]}" -T "$LD_SCRIPT" "${ld_scripts[@]}" -Map "${ELF%.elf}.map" -o "$ELF"

rom=build/aidyn.us.z64
"${CROSS}objcopy" -O binary "$ELF" "$rom.tmp"
want=$(stat -c %s "$TARGET") have=$(stat -c %s "$rom.tmp")
if (( have > want )); then
    echo "built ROM is $have bytes, larger than the base ROM ($want)" >&2
    exit 1
fi
if (( have < want )); then
    "$TOOLKIT/.venv/bin/python" -I -c 'import sys; open(sys.argv[1], "ab").write(bytes([int(sys.argv[3], 0)]) * int(sys.argv[2]))' \
        "$rom.tmp" $((want - have)) "$PAD_BYTE"
fi
mv "$rom.tmp" "$rom"

if cmp -s "$TARGET" "$rom"; then
    echo "build_elf.sh: $rom matches baserom.z64 byte for byte."
else
    "$TOOLKIT/.venv/bin/python" -I tools/romdiff.py "$YAML" "$TARGET" "$rom"
    exit 1
fi
