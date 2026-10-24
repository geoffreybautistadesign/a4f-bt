// Transport HCI H2 (Bluetooth sur USB) pour BTstack, au-dessus d'une petite API USB
// fournie par la plateforme : libusb sur Mac, libSceUsbd sur PS4 (mêmes fonctions).
#pragma once

#include <stdint.h>
#include "hci_transport.h"

#define A4F_USB_OK       0
#define A4F_USB_TIMEOUT  1     // délai dépassé, rien reçu (normal)
#define A4F_USB_ERROR   -1

typedef struct {
    // Ouvre le dongle vid:pid, détache un éventuel pilote noyau, réclame l'interface 0.
    void *(*open_device)(uint16_t vid, uint16_t pid);
    void  (*close_device)(void *dev);
    // Requête de contrôle classe vers l'interface (bmRequestType 0x20) = commande HCI.
    int   (*control_out)(void *dev, uint8_t *data, uint16_t len, unsigned timeout_ms);
    // Transferts interrupt / bulk. *got = octets transférés. Renvoie A4F_USB_*.
    int   (*interrupt_in)(void *dev, uint8_t ep, uint8_t *buf, int len, int *got, unsigned timeout_ms);
    int   (*bulk)(void *dev, uint8_t ep, uint8_t *buf, int len, int *got, unsigned timeout_ms);
} a4f_usb_api_t;

void hci_transport_a4f_usb_set_api(const a4f_usb_api_t *api, uint16_t vid, uint16_t pid);
const hci_transport_t *hci_transport_a4f_usb_instance(void);
