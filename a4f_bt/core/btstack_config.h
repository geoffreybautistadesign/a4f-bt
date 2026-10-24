// Configuration BTstack d'A4F BT : Bluetooth classique + A2DP source seulement
// (pas de BLE, de mesh ni de SCO), même fichier pour le test Mac et le plugin PS4.
#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// Plateforme
#define HAVE_ASSERT
#define HAVE_MALLOC
#define HAVE_POSIX_FILE_IO

// Fonctions
#define ENABLE_CLASSIC
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO
#define ENABLE_PRINTF_HEXDUMP

// Tampons
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_INCOMING_PRE_BUFFER_SIZE 14
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

#endif
