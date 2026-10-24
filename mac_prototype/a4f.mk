# Règles en plus du Makefile de btstack/port/libusb :
#   make -C <btstack>/port/libusb -f Makefile -f <ce dossier>/a4f.mk A4F_SRC=<ce dossier> a4f_a2dp_source
VPATH  += $(A4F_SRC)
CFLAGS += -I$(A4F_SRC)
# casque appairé, pour la commande b (reconnexion sans mode appairage)
A4F_REMOTE ?= 1C:77:54:71:14:B6
CFLAGS += -DREMOTE_ADDR_STRING=\"$(A4F_REMOTE)\"

a4f_a2dp_source: ${CORE_OBJ} ${COMMON_OBJ} ${CLASSIC_OBJ} ${SDP_CLIENT} ${SBC_CODEC_OBJ} ${AVDTP_OBJ} ${HXCMOD_PLAYER_OBJ} btstack_audio_generator.o avrcp.o avrcp_controller.o avrcp_target.o a4f_ring.o a4f_udp.o a4f_a2dp_source.c
	${CC} $^ ${CFLAGS} ${LDFLAGS} -o $@
