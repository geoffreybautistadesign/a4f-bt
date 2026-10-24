# Plugin GoldHEN a4f_bt.prx (BTstack + cœur A4F BT). Tous les chemins sans espace.
#   make WORK=<dossier outils de ../../build.sh> BTSTACK=<btstack> A4F_BT=<ce dossier a4f_bt>
WORK    ?= /tmp/a4f_ps4_build
BTSTACK ?= ../../btstack
A4F_BT  ?= ..
OUT     := build

TOOLCHAIN := $(WORK)/OpenOrbis/PS4Toolchain
GH_SDK    := $(WORK)/SDK
GH_COMMON := $(WORK)/repo/common
CC        := $(WORK)/bin/clang
LD        := $(WORK)/bin/ld.lld
FSELF     := OO_PS4_TOOLCHAIN=$(TOOLCHAIN) $(TOOLCHAIN)/bin/macos/create-fself-macos

include $(A4F_BT)/btstack_sources.mk

CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -O2 -std=gnu11 \
	-D__FINAL__=1 -D__USE_PRINTF__ -Wall -Wno-unused-parameter \
	-isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -I$(GH_SDK)/include -I$(GH_COMMON) -I$(A4F_BT)/plugin/source $(BTSTACK_INC)
LDFLAGS := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x -e _init --eh-frame-hdr \
	-L$(TOOLCHAIN)/lib -L$(GH_SDK) -lSceLibcInternal -lGoldHEN_Hook -lkernel -lSceSysmodule -lSceAudioOut

OBJS := $(patsubst %.c,$(OUT)/bt/%.o,$(BTSTACK_SRC)) $(patsubst %.c,$(OUT)/a4f/%.o,$(A4F_BT_SRC)) \
	$(OUT)/a4f/plugin/source/main.o $(OUT)/a4f/plugin/source/convert.o $(OUT)/plugin_common.o

a4f_bt.prx: $(OBJS)
	$(LD) $(GH_SDK)/build/crtprx.o $(OBJS) -o $(OUT)/a4f_bt.elf $(LDFLAGS)
	$(FSELF) -in=$(OUT)/a4f_bt.elf -out=$(OUT)/a4f_bt.oelf --lib=$@ --paid 0x3800000000000011

$(OUT)/bt/%.o: $(BTSTACK)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

$(OUT)/a4f/%.o: $(A4F_BT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

$(OUT)/plugin_common.o: $(GH_COMMON)/plugin_common.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(OUT) a4f_bt.prx
