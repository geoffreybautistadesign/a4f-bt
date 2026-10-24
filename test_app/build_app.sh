#!/bin/sh
# Appli homebrew autonome "A4F BT Test" : le code de la sonde (../a4f_bt_probe/source/main.c,
# compilé avec -DA4F_PROBE_APP) dans son propre processus, hors d'un jeu.
# Prérequis : avoir lancé une fois ../build.sh (outils dans $WORK).
#   sh build_app.sh   ->  ../dist/IV0000-AFBT00001_00-AFBTPROBE0000000.pkg
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/a4f_ps4_build}
T="$WORK/OpenOrbis/PS4Toolchain"
BIN="$T/bin/macos"
export OO_PS4_TOOLCHAIN="$T"
TITLE="A4F BT Test"
TITLE_ID=AFBT00001   # 4 lettres + 5 chiffres obligatoire
CONTENT_ID=IV0000-AFBT00001_00-AFBTPROBE0000000
VERSION=1.00

B="$WORK/app_build"
rm -rf "$B" && mkdir -p "$B/sce_sys/about" "$B/sce_module"
CFLAGS="--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -O2 -D__FINAL__=1 -D__USE_PRINTF__ -DA4F_PROBE_APP \
  -isysroot $T -isystem $T/include -I$WORK/SDK/include -I$WORK/repo/common"
"$WORK/bin/clang" $CFLAGS -o "$B/main.o" "$HERE/../a4f_bt_probe/source/main.c"
"$WORK/bin/clang" $CFLAGS -o "$B/plugin_common.o" "$WORK/repo/common/plugin_common.c"
"$WORK/bin/ld.lld" -m elf_x86_64 -pie --script "$T/link.x" --eh-frame-hdr -L"$T/lib" -L"$WORK/SDK" \
  "$B/main.o" "$B/plugin_common.o" -lGoldHEN_Hook -lc -lkernel -lSceSysmodule "$T/lib/crt1.o" -o "$B/app.elf"

cd "$B"
"$BIN/create-fself-macos" -in=app.elf -out=app.oelf --eboot eboot.bin --paid 0x3800000000000011
cp "$T/samples/hello_world/sce_sys/about/right.sprx" sce_sys/about/
cp "$T/samples/hello_world/sce_sys/icon0.png" sce_sys/
cp "$T/samples/hello_world/sce_module/libc.prx" "$T/samples/hello_world/sce_module/libSceFios2.prx" sce_module/

P="$BIN/PkgTool.Core"
"$P" sfo_new sce_sys/param.sfo
"$P" sfo_setentry sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
"$P" sfo_setentry sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
"$P" sfo_setentry sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
"$P" sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value gd
"$P" sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
"$P" sfo_setentry sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$P" sfo_setentry sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
"$P" sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value "$TITLE"
"$P" sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
"$P" sfo_setentry sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value "$VERSION"

"$BIN/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" \
  --files "eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_module/libc.prx sce_module/libSceFios2.prx"
"$P" pkg_build pkg.gp4 .
mkdir -p "$HERE/../dist" && cp "$CONTENT_ID.pkg" "$HERE/../dist/"
echo "OK : $HERE/../dist/$CONTENT_ID.pkg"
