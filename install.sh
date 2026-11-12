#!/usr/bin/env bash
# Installs ds_fixes.so into Don't Starve's bin/shims/.
#   --game-dir DIR  game directory (contains bin/), if not found automatically
#   --prebuilt      use prebuilt/ instead of compiling
#   --uninstall     remove it
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
game_dir="" prebuilt=0 uninstall=0
while [ $# -gt 0 ]; do
    case $1 in
        --game-dir) game_dir=${2:?}; shift ;;
        --prebuilt) prebuilt=1 ;;
        --uninstall) uninstall=1 ;;
        *) sed -n '2,5s/^# \{0,1\}//p' "$0"; exit 2 ;;
    esac
    shift
done
die() { echo "error: $*" >&2; exit 1; }

# Game directories from every Steam library (native, Flatpak, Snap).
find_game() {
    local root lib acf dir
    for root in ~/.local/share/Steam ~/.steam/steam ~/.var/app/com.valvesoftware.Steam/.local/share/Steam \
                ~/snap/steam/common/.local/share/Steam; do
        [ -f "$root/steamapps/libraryfolders.vdf" ] || continue
        sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"$/\1/p' "$root/steamapps/libraryfolders.vdf" |
        while IFS= read -r lib; do
            acf="$lib/steamapps/appmanifest_219740.acf"
            [ -f "$acf" ] || continue
            dir=$(sed -n 's/^[[:space:]]*"installdir"[[:space:]]*"\(.*\)"$/\1/p' "$acf")
            [ -x "$lib/steamapps/common/$dir/bin/dontstarve_steam" ] && readlink -f "$lib/steamapps/common/$dir"
        done
    done | sort -u
}

if [ -z "$game_dir" ]; then
    mapfile -t found < <(find_game)
    [ "${#found[@]}" -gt 0 ] || die "game not found, use --game-dir DIR"
    [ "${#found[@]}" -eq 1 ] || die "several installs, choose one with --game-dir:$(printf '\n  %s' "${found[@]}")"
    game_dir=${found[0]}
fi
[ -x "$game_dir/bin/dontstarve_steam" ] || die "no bin/dontstarve_steam in $game_dir"
target="$game_dir/bin/shims/ds_fixes.so"

if [ "$uninstall" = 1 ]; then
    rm -f "$target"
    rmdir "$game_dir/bin/shims" 2>/dev/null || true
    echo "Removed $target. Also clear the game's launch options."
    exit 0
fi

lib="$here/prebuilt/ds_fixes.so"
if [ "$prebuilt" = 0 ] && command -v make >/dev/null && command -v "${CC:-cc}" >/dev/null; then
    make -s -C "$here" && lib="$here/build/ds_fixes.so"
fi
[ -f "$lib" ] || die "$lib missing"
install -D -m 755 "$lib" "$target"

cat <<'EOF'
Installed. Steam -> Don't Starve -> Properties -> Launch Options:

    LD_PRELOAD='$ORIGIN/shims/ds_fixes.so':"$LD_PRELOAD" %command%
EOF
