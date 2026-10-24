# Fichiers BTstack nécessaires à A4F BT (Bluetooth classique + A2DP source + SBC),
# partagés par le test Mac (mac/Makefile) et le plugin PS4 (plugin/Makefile).
# BTSTACK et A4F_BT doivent être des chemins sans espace (exigence de make).

BTSTACK_SRC := \
	src/btstack_memory.c src/btstack_linked_list.c src/btstack_memory_pool.c \
	src/btstack_run_loop.c src/btstack_util.c src/ad_parser.c \
	src/hci.c src/hci_cmd.c src/hci_dump.c src/hci_event.c \
	src/l2cap.c src/l2cap_signaling.c src/btstack_tlv.c \
	src/btstack_audio_generator.c src/btstack_ring_buffer.c \
	src/classic/sdp_util.c src/classic/sdp_server.c src/classic/sdp_client.c \
	src/classic/btstack_link_key_db_tlv.c \
	src/classic/avdtp.c src/classic/avdtp_util.c src/classic/avdtp_initiator.c \
	src/classic/avdtp_acceptor.c src/classic/avdtp_source.c \
	src/classic/a2dp.c src/classic/a2dp_source.c \
	src/classic/btstack_sbc_encoder_bluedroid.c src/classic/btstack_sbc_bluedroid.c \
	platform/posix/btstack_tlv_posix.c platform/posix/hci_dump_posix_fs.c \
	src/classic/btstack_sbc_plc.c src/classic/btstack_sbc_decoder_bluedroid.c \
	$(patsubst $(BTSTACK)/%,%,$(wildcard $(BTSTACK)/3rd-party/bluedroid/encoder/srce/*.c)) \
	$(patsubst $(BTSTACK)/%,%,$(wildcard $(BTSTACK)/3rd-party/bluedroid/decoder/srce/*.c))

A4F_BT_SRC := \
	core/a4f_bt.c core/a4f_ring.c core/btstack_run_loop_a4f.c core/hci_transport_a4f_usb.c

BTSTACK_INC := -I$(BTSTACK)/src -I$(BTSTACK)/src/classic -I$(BTSTACK)/platform/posix \
	-I$(BTSTACK)/3rd-party/bluedroid/encoder/include -I$(BTSTACK)/3rd-party/bluedroid/decoder/include -I$(BTSTACK)/3rd-party/micro-ecc -I$(BTSTACK)/3rd-party/yxml -I$(BTSTACK)/3rd-party/md5 \
	-I$(BTSTACK)/3rd-party/rijndael -I$(A4F_BT)/core
