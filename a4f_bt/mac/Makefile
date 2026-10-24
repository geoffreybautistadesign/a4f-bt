# Test Mac du cœur A4F BT.
#   make BTSTACK=<btstack sans espace> A4F_BT=<ce dossier a4f_bt, sans espace> LIBUSB=<dossier libusb compilée>
BTSTACK ?= ../btstack
A4F_BT  ?= ..
LIBUSB  ?= ../libusb
OUT     := build

include $(A4F_BT)/btstack_sources.mk

CFLAGS := -O2 -g -std=gnu11 -Wall -Wno-unused-parameter $(BTSTACK_INC) -I$(LIBUSB)/include/libusb-1.0 -I$(A4F_BT)/mac
LIBS   := $(LIBUSB)/libusb-1.0.a -framework IOKit -framework CoreFoundation -framework Security -lobjc -lpthread -lm

OBJS := $(patsubst %.c,$(OUT)/bt/%.o,$(BTSTACK_SRC)) $(patsubst %.c,$(OUT)/a4f/%.o,$(A4F_BT_SRC)) $(OUT)/a4f/mac/main_mac.o

a4f_bt_mac: $(OBJS)
	$(CC) $^ $(LIBS) -o $@

$(OUT)/bt/%.o: $(BTSTACK)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/a4f/%.o: $(A4F_BT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT) a4f_bt_mac
