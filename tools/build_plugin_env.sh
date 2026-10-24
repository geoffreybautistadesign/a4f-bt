#!/bin/sh
# Compile les plugins PS4 de ce dossier sur le Mac, sans Homebrew :
# LLVM (clang + ld.lld) vient du paquet Python "ziglang", le toolchain OpenOrbis
# et le GoldHEN SDK sont téléchargés dans $WORK (dossier sans espace, exigé par make).
#   sh build.sh [nom_du_plugin]      (défaut : a4f_bt_probe) -> dist/<nom>.prx
set -e
NAME=${1:-a4f_bt_probe}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/a4f_ps4_build}
mkdir -p "$WORK" && cd "$WORK"

[ -x zigenv/bin/python ] || { python3 -m venv zigenv && zigenv/bin/pip -q install ziglang; }
mkdir -p bin
for t in clang:clang clang++:clang++ ld.lld:ld.lld llvm-ar:ar; do
    printf '#!/bin/sh\nexec %s/zigenv/bin/python -m ziglang %s "$@"\n' "$WORK" "${t##*:}" > "bin/${t%%:*}"
    chmod +x "bin/${t%%:*}"
done
[ -d OpenOrbis ] || curl -sL https://github.com/illusion0001/OpenOrbis-PS4-Toolchain/releases/latest/download/toolchain.tar.gz | tar xz
chmod +x OpenOrbis/PS4Toolchain/bin/macos/create-fself-macos
[ -d SDK ] || git clone -q --depth 1 https://github.com/GoldHEN/GoldHEN_Plugins_SDK SDK
[ -d repo ] || { git clone -q --depth 1 https://github.com/GoldHEN/GoldHEN_Plugins_Repository repo; }

export OO_PS4_TOOLCHAIN="$WORK/OpenOrbis/PS4Toolchain" GOLDHEN_SDK="$WORK/SDK"
TOOLS="CC=$WORK/bin/clang CCX=$WORK/bin/clang++ LD=$WORK/bin/ld.lld AR=$WORK/bin/llvm-ar"
[ -f SDK/libGoldHEN_Hook.a ] || make -s -C SDK PRINTF=1 DEBUGFLAGS=1 $TOOLS

rm -rf "repo/plugin_src/$NAME" && cp -R "$HERE/$NAME" "repo/plugin_src/$NAME"
make -s -C "repo/plugin_src/$NAME" $TOOLS
mkdir -p "$HERE/dist" && cp "repo/bin/plugins/prx_final/$NAME.prx" "$HERE/dist/"
echo "OK : $HERE/dist/$NAME.prx"
