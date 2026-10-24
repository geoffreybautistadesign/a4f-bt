// Transport HCI USB simple, à transferts synchrones :
//  - deux threads lisent en boucle l'endpoint interrupt 0x81 (événements HCI) et
//    l'endpoint bulk 0x82 (données ACL, donc le son vers les AirPods passe dans l'autre sens) ;
//  - chaque paquet reçu est copié dans une file, puis remis à BTstack sur son propre
//    thread (btstack_run_loop_execute_on_main_thread) ;
//  - l'envoi (commande = contrôle, ACL = bulk 0x02) se fait directement depuis le
//    thread BTstack, puis on signale HCI_EVENT_TRANSPORT_PACKET_SENT.
// Endpoints fixes d'un dongle Bluetooth USB standard (CSR8510 : 0x81, 0x82, 0x02).

#include "hci_transport_a4f_usb.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "btstack_config.h"
#include "btstack_debug.h"
#include "btstack_run_loop.h"
#include "hci.h"

#define EP_EVENT_IN 0x81
#define EP_ACL_IN   0x82
#define EP_ACL_OUT  0x02
#define READ_TIMEOUT_MS 100     // les lecteurs vérifient régulièrement s'il faut s'arrêter
#define NUM_SLOTS 32

typedef struct {
    uint8_t  type;
    uint16_t len;
    uint8_t  data[HCI_INCOMING_PRE_BUFFER_SIZE + HCI_ACL_BUFFER_SIZE];  // BTstack peut écrire devant le paquet
} slot_t;

static const a4f_usb_api_t *usb;
static uint16_t usb_vid, usb_pid;
static void *dev;
static void (*packet_handler)(uint8_t packet_type, uint8_t *packet, uint16_t size);

static pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_space = PTHREAD_COND_INITIALIZER;
static slot_t   q_slots[NUM_SLOTS];
static unsigned q_head, q_tail;   // q_tail - q_head = nombre de paquets en attente
static bool     q_deliver_pending;
static btstack_context_callback_registration_t q_deliver_reg;

static volatile bool readers_running;
static pthread_t reader_evt, reader_acl;

static bool send_busy;
static btstack_context_callback_registration_t sent_reg;

// ---- remise des paquets sur le thread BTstack

static void deliver(void *context){
    (void) context;
    while (true){
        pthread_mutex_lock(&q_mutex);
        if (q_head == q_tail){
            q_deliver_pending = false;
            pthread_mutex_unlock(&q_mutex);
            return;
        }
        slot_t *s = &q_slots[q_head % NUM_SLOTS];
        pthread_mutex_unlock(&q_mutex);

        if (packet_handler) packet_handler(s->type, s->data + HCI_INCOMING_PRE_BUFFER_SIZE, s->len);

        pthread_mutex_lock(&q_mutex);
        q_head++;
        pthread_cond_signal(&q_space);
        pthread_mutex_unlock(&q_mutex);
    }
}

// Copie un paquet reçu dans la file (attend s'il n'y a plus de place).
static void enqueue(uint8_t type, const uint8_t *data, int len){
    pthread_mutex_lock(&q_mutex);
    while (readers_running && (q_tail - q_head) >= NUM_SLOTS){
        pthread_cond_wait(&q_space, &q_mutex);
    }
    if (!readers_running){
        pthread_mutex_unlock(&q_mutex);
        return;
    }
    slot_t *s = &q_slots[q_tail % NUM_SLOTS];
    s->type = type;
    s->len = (uint16_t) len;
    memcpy(s->data + HCI_INCOMING_PRE_BUFFER_SIZE, data, len);
    q_tail++;
    bool post = !q_deliver_pending;
    q_deliver_pending = true;
    pthread_mutex_unlock(&q_mutex);
    if (post) btstack_run_loop_execute_on_main_thread(&q_deliver_reg);
}

static void *reader_thread(void *arg){
    bool is_acl = arg != NULL;
    static uint8_t buf_evt[HCI_ACL_BUFFER_SIZE], buf_acl[HCI_ACL_BUFFER_SIZE];
    uint8_t *buf = is_acl ? buf_acl : buf_evt;
    int errors = 0;
    while (readers_running){
        int got = 0;
        int r = is_acl ? usb->bulk(dev, EP_ACL_IN, buf, HCI_ACL_BUFFER_SIZE, &got, READ_TIMEOUT_MS)
                       : usb->interrupt_in(dev, EP_EVENT_IN, buf, HCI_ACL_BUFFER_SIZE, &got, READ_TIMEOUT_MS);
        if (r == A4F_USB_OK && got > 0){
            errors = 0;
            enqueue(is_acl ? HCI_ACL_DATA_PACKET : HCI_EVENT_PACKET, buf, got);
        } else if (r == A4F_USB_ERROR){
            // dongle débranché ou erreur passagère : on ralentit pour ne pas tourner à vide
            if (++errors == 10) log_error("USB: erreurs de lecture en serie sur %s", is_acl ? "ACL" : "evenements");
            struct timespec ts = { 0, 20 * 1000000 };
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

// ---- interface hci_transport_t

static void transport_init(const void *config){
    (void) config;
    pthread_mutex_init(&q_mutex, NULL);
    pthread_cond_init(&q_space, NULL);
    q_deliver_reg.callback = &deliver;
}

static int transport_open(void){
    if (!usb) return -1;
    dev = usb->open_device(usb_vid, usb_pid);
    if (!dev){
        log_error("USB: dongle %04x:%04x introuvable", usb_vid, usb_pid);
        return -1;
    }
    q_head = q_tail = 0;
    q_deliver_pending = false;
    send_busy = false;
    readers_running = true;
    pthread_create(&reader_evt, NULL, reader_thread, NULL);
    pthread_create(&reader_acl, NULL, reader_thread, (void *) 1);
    log_info("USB: dongle ouvert");
    return 0;
}

static int transport_close(void){
    if (!dev) return 0;
    pthread_mutex_lock(&q_mutex);
    readers_running = false;
    pthread_cond_broadcast(&q_space);
    pthread_mutex_unlock(&q_mutex);
    pthread_join(reader_evt, NULL);
    pthread_join(reader_acl, NULL);
    usb->close_device(dev);
    dev = NULL;
    return 0;
}

static void transport_register_packet_handler(void (*handler)(uint8_t, uint8_t *, uint16_t)){
    packet_handler = handler;
}

static int transport_can_send_packet_now(uint8_t packet_type){
    (void) packet_type;
    return dev != NULL && !send_busy;
}

static void emit_packet_sent(void *context){
    (void) context;
    send_busy = false;
    static const uint8_t event[] = { HCI_EVENT_TRANSPORT_PACKET_SENT, 0 };
    if (packet_handler) packet_handler(HCI_EVENT_PACKET, (uint8_t *) event, sizeof(event));
}

static int transport_send_packet(uint8_t packet_type, uint8_t *packet, int size){
    if (!dev) return -1;
    int r;
    switch (packet_type){
        case HCI_COMMAND_DATA_PACKET:
            r = usb->control_out(dev, packet, (uint16_t) size, 1000);
            break;
        case HCI_ACL_DATA_PACKET: {
            int got = 0;
            r = usb->bulk(dev, EP_ACL_OUT, packet, size, &got, 1000);
            break;
        }
        default:
            log_error("USB: type de paquet %u non gere", packet_type);
            return -1;
    }
    if (r != A4F_USB_OK){
        log_error("USB: envoi type %u echoue (%d)", packet_type, r);
        return -1;
    }
    // signalé depuis la boucle, pas en plein milieu de l'appel de hci.c
    send_busy = true;
    sent_reg.callback = &emit_packet_sent;
    btstack_run_loop_execute_on_main_thread(&sent_reg);
    return 0;
}

static const hci_transport_t transport = {
    "a4f_usb",
    &transport_init,
    &transport_open,
    &transport_close,
    &transport_register_packet_handler,
    &transport_can_send_packet_now,
    &transport_send_packet,
    NULL,   // set_baudrate
    NULL,   // reset_link
    NULL,   // set_sco_config (pas de SCO : on ne fait que de l'A2DP)
};

void hci_transport_a4f_usb_set_api(const a4f_usb_api_t *api, uint16_t vid, uint16_t pid){
    usb = api;
    usb_vid = vid;
    usb_pid = pid;
}

const hci_transport_t *hci_transport_a4f_usb_instance(void){
    return &transport;
}
