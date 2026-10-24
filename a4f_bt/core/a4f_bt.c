// A4F BT : connexion au casque et envoi du son en A2DP/SBC.
// Partie streaming reprise de btstack/example/a2dp_source_demo.c (minuterie de 10 ms qui
// encode le PCM disponible en trames SBC et les envoie quand le lien est libre).

#include "a4f_bt.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_audio_generator.h"
#include "btstack_run_loop_a4f.h"
#include "btstack_tlv_posix.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "hci_dump.h"
#include "hci_dump_posix_fs.h"

#include "a4f_ring.h"

#define TAG_LAST_DEVICE      (((uint32_t) 'A' << 24) | ('4' << 16) | ('F' << 8) | 'D')
#define AUDIO_TIMEOUT_MS     10
#define SBC_STORAGE_SIZE     1030
#define RETRY_MS             4000
#define INQUIRY_1280MS_UNITS 8      // ~10 s de recherche
#define COD_AUDIO_HEADSET    (0x200000 | 0x000400)   // service Audio + classe majeure Audio/Video

static const a4f_bt_config_t *cfg;

// ---------------------------------------------------------------- état

typedef enum { ST_OFF, ST_IDLE, ST_CONNECTING, ST_SCANNING, ST_CONNECTED } a4f_state_t;

static a4f_state_t state;
static bd_addr_t   device_addr;
static bool        have_device;     // dernier casque connu (clé d'appairage en mémoire)
static unsigned    attempt;
static btstack_timer_source_t retry_timer;
static btstack_packet_callback_registration_t hci_event_registration;

static const btstack_tlv_t *tlv_impl;
static btstack_tlv_posix_t  tlv_context;
static char tlv_path[160];

static char last_status[96];

static void status(const char *fmt, ...){
    char msg[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("A4F BT: %s\n", msg);
    if (strcmp(msg, last_status) == 0) return;   // pas de notification en double
    strcpy(last_status, msg);
    if (cfg->status) cfg->status(msg);
}

// ---------------------------------------------------------------- son

typedef struct {
    uint16_t a2dp_cid;
    uint8_t  local_seid;
    uint8_t  stream_opened;
    uint32_t time_audio_data_sent;
    uint32_t acc_num_missed_samples;
    uint32_t samples_ready;
    btstack_timer_source_t audio_timer;
    int      max_media_payload_size;
    uint32_t rtp_timestamp;
    uint8_t  sbc_storage[SBC_STORAGE_SIZE];
    uint16_t sbc_storage_count;
    uint8_t  sbc_ready_to_send;
} media_t;

static media_t media;

static uint8_t sbc_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | (AVDTP_SBC_48000 << 4) | AVDTP_SBC_STEREO,
    0xFF,
    2, 53
};
static uint8_t sbc_configuration_storage[4];

static const btstack_sbc_encoder_t *sbc_encoder;
static btstack_sbc_encoder_bluedroid_t sbc_encoder_state;
static int sample_rate = 48000;

static volatile bool tone_on = true;
static btstack_audio_generator_sine_t sine;
static a4f_ring_t pcm_ring;

static volatile uint32_t pulls;   // appels à produce_audio (journal de debug)
static volatile uint32_t packets; // paquets A2DP envoyés (journal de debug)

static void produce_audio(int16_t *pcm, int num_frames){
    pulls++;
    if (tone_on){
        btstack_audio_generator_generate(&sine.base, pcm, num_frames);
    } else {
        a4f_ring_pull(&pcm_ring, pcm, num_frames, sample_rate);
    }
}

static void send_media_packet(void){
    int frame_bytes = sbc_encoder->sbc_buffer_length(&sbc_encoder_state);
    uint8_t num_frames = media.sbc_storage_count / frame_bytes;
    media.sbc_storage[0] = num_frames;   // en-tête SBC : nombre de trames
    a2dp_source_stream_send_media_payload_rtp(media.a2dp_cid, media.local_seid, 0, media.rtp_timestamp,
                                              media.sbc_storage, media.sbc_storage_count + 1);
    media.rtp_timestamp += num_frames * sbc_encoder->num_audio_frames(&sbc_encoder_state);
    packets++;
    media.sbc_storage_count = 0;
    media.sbc_ready_to_send = 0;
}

static void fill_sbc_buffer(void){
    unsigned frames_per_sbc = sbc_encoder->num_audio_frames(&sbc_encoder_state);
    uint16_t sbc_len = sbc_encoder->sbc_buffer_length(&sbc_encoder_state);
    while (media.samples_ready >= frames_per_sbc &&
           (media.max_media_payload_size - media.sbc_storage_count) >= sbc_len){
        int16_t pcm[256 * 2];
        produce_audio(pcm, frames_per_sbc);
        sbc_encoder->encode_signed_16(&sbc_encoder_state, pcm, &media.sbc_storage[1 + media.sbc_storage_count]);
        media.sbc_storage_count += sbc_len;
        media.samples_ready -= frames_per_sbc;
    }
}

#define MAX_BACKLOG_FRAMES (48000 / 5)   // 200 ms

static void try_send(void);

static void audio_timeout_handler(btstack_timer_source_t *timer){
    btstack_run_loop_set_timer(timer, AUDIO_TIMEOUT_MS);
    btstack_run_loop_add_timer(timer);
    uint32_t now = btstack_run_loop_get_time_ms();
    uint32_t period = media.time_audio_data_sent ? now - media.time_audio_data_sent : AUDIO_TIMEOUT_MS;
    uint32_t n = (period * sample_rate) / 1000;
    media.acc_num_missed_samples += (period * sample_rate) % 1000;
    while (media.acc_num_missed_samples >= 1000){
        n++;
        media.acc_num_missed_samples -= 1000;
    }
    media.time_audio_data_sent = now;
    media.samples_ready += n;
    // après un long blocage du thread, on ne rattrape pas plus que ça (le tampon PCM
    // jette déjà le surplus)
    if (media.samples_ready > MAX_BACKLOG_FRAMES) media.samples_ready = MAX_BACKLOG_FRAMES;

    try_send();
}

// Prépare le paquet suivant et demande à l'envoyer s'il est plein. Appelée par le timer
// et juste après chaque envoi : sur PS4 le thread est réveillé bien moins souvent que
// toutes les 10 ms (le jeu prend le CPU), donc un seul paquet par réveil ne suffisait
// pas (~16 paquets/s, un tiers du débit) ; on enchaîne les paquets tant qu'il y a du retard.
static void try_send(void){
    if (media.sbc_ready_to_send) return;
    fill_sbc_buffer();
    if ((media.sbc_storage_count + sbc_encoder->sbc_buffer_length(&sbc_encoder_state)) > media.max_media_payload_size){
        media.sbc_ready_to_send = 1;
        a2dp_source_stream_endpoint_request_can_send_now(media.a2dp_cid, media.local_seid);
    }
}

static void audio_start(void){
    btstack_audio_generator_sine_init(&sine, sample_rate, 2, 441);
    media.max_media_payload_size = btstack_min(a2dp_max_media_payload_size(media.a2dp_cid, media.local_seid), SBC_STORAGE_SIZE);
    media.sbc_storage_count = 0;
    media.sbc_ready_to_send = 0;
    media.samples_ready = 0;
    media.time_audio_data_sent = 0;
    media.acc_num_missed_samples = 0;
    btstack_run_loop_remove_timer(&media.audio_timer);
    btstack_run_loop_set_timer_handler(&media.audio_timer, audio_timeout_handler);
    btstack_run_loop_set_timer(&media.audio_timer, AUDIO_TIMEOUT_MS);
    btstack_run_loop_add_timer(&media.audio_timer);
}

static void audio_stop(void){
    btstack_run_loop_remove_timer(&media.audio_timer);
    media.sbc_storage_count = 0;
    media.sbc_ready_to_send = 0;
}

// ---------------------------------------------------------------- connexion

static void schedule_retry(uint32_t ms);

static void connect_to(const bd_addr_t addr){
    memcpy(device_addr, addr, 6);
    state = ST_CONNECTING;
    uint8_t r = a2dp_source_establish_stream(device_addr, &media.a2dp_cid);
    if (r != ERROR_CODE_SUCCESS){
        printf("A4F BT: connexion refusee tout de suite (0x%02x)\n", r);
        state = ST_IDLE;
        schedule_retry(RETRY_MS);
    }
}

// Alterne reconnexion au dernier casque et recherche d'un casque en mode appairage.
static void retry_handler(btstack_timer_source_t *timer){
    (void) timer;
    if (state != ST_IDLE) return;
    bool reconnect = have_device && (attempt++ % 2 == 0);
    if (reconnect){
        status("Connexion a %s...", bd_addr_to_str(device_addr));
        connect_to(device_addr);
    } else {
        status("Recherche d'un casque (mode appairage)...");
        state = ST_SCANNING;
        gap_inquiry_start(INQUIRY_1280MS_UNITS);
    }
}

static void schedule_retry(uint32_t ms){
    btstack_run_loop_remove_timer(&retry_timer);
    btstack_run_loop_set_timer_handler(&retry_timer, retry_handler);
    btstack_run_loop_set_timer(&retry_timer, ms);
    btstack_run_loop_add_timer(&retry_timer);
}

static void setup_storage(void){
    bd_addr_t local;
    gap_local_bd_addr(local);
    snprintf(tlv_path, sizeof(tlv_path), "%sbtstack_%s.tlv", cfg->data_dir, bd_addr_to_str_with_delimiter(local, '-'));
    tlv_impl = btstack_tlv_posix_init_instance(&tlv_context, tlv_path);
    btstack_tlv_set_instance(tlv_impl, &tlv_context);
    hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(tlv_impl, &tlv_context));
    have_device = tlv_impl->get_tag(&tlv_context, TAG_LAST_DEVICE, device_addr, 6) == 6;
    printf("A4F BT: cles dans %s, dernier casque %s\n", tlv_path, have_device ? bd_addr_to_str(device_addr) : "(aucun)");
}

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    (void) channel;
    (void) size;
    if (packet_type != HCI_EVENT_PACKET) return;
    bd_addr_t addr;

    switch (hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING){
                setup_storage();
                status("Dongle Bluetooth pret");
                state = ST_IDLE;
                attempt = 0;
                schedule_retry(0);
            }
            break;
        case HCI_EVENT_PIN_CODE_REQUEST:
            // appairage ancien (avant Bluetooth 2.1) : non géré
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            gap_pin_code_negative(addr);
            break;
        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            // pas d'écran pour comparer le code : on accepte (les AirPods sont en mode appairage)
            hci_event_user_confirmation_request_get_bd_addr(packet, addr);
            gap_ssp_confirmation_response(addr);
            break;
        case GAP_EVENT_INQUIRY_RESULT: {
            if (state != ST_SCANNING) break;
            gap_event_inquiry_result_get_bd_addr(packet, addr);
            uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
            printf("A4F BT: vu %s classe %06" PRIx32 "\n", bd_addr_to_str(addr), cod);
            if ((cod & COD_AUDIO_HEADSET) == COD_AUDIO_HEADSET){
                status("Casque trouve : %s", bd_addr_to_str(addr));
                gap_inquiry_stop();
                connect_to(addr);
            }
            break;
        }
        case GAP_EVENT_INQUIRY_COMPLETE:
            if (state == ST_SCANNING){
                state = ST_IDLE;
                schedule_retry(500);
            }
            break;
        default:
            break;
    }
}

static void a2dp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    (void) channel;
    (void) size;
    if (packet_type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;
    uint8_t st;
    bd_addr_t addr;

    switch (hci_event_a2dp_meta_get_subevent_code(packet)){
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
            st = a2dp_subevent_signaling_connection_established_get_status(packet);
            if (st != ERROR_CODE_SUCCESS){
                printf("A4F BT: connexion echouee (0x%02x)\n", st);
                media.a2dp_cid = 0;
                state = ST_IDLE;
                schedule_retry(RETRY_MS);
                break;
            }
            media.a2dp_cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
            break;

        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION: {
            if (a2dp_subevent_signaling_media_codec_sbc_configuration_get_a2dp_cid(packet) != media.a2dp_cid) break;
            sample_rate = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
            btstack_sbc_channel_mode_t mode;
            switch ((avdtp_channel_mode_t) a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet)){
                case AVDTP_CHANNEL_MODE_STEREO:       mode = SBC_CHANNEL_MODE_STEREO; break;
                case AVDTP_CHANNEL_MODE_DUAL_CHANNEL: mode = SBC_CHANNEL_MODE_DUAL_CHANNEL; break;
                case AVDTP_CHANNEL_MODE_MONO:         mode = SBC_CHANNEL_MODE_MONO; break;
                default:                              mode = SBC_CHANNEL_MODE_JOINT_STEREO; break;
            }
            printf("A4F BT: SBC %d Hz, bitpool max %d\n", sample_rate,
                   a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet));
            sbc_encoder = btstack_sbc_encoder_bluedroid_init_instance(&sbc_encoder_state);
            sbc_encoder->configure(&sbc_encoder_state, SBC_MODE_STANDARD,
                a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet),
                a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet),
                (btstack_sbc_allocation_method_t) (a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet) - 1),
                sample_rate,
                a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet),
                mode);
            break;
        }

        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            st = a2dp_subevent_stream_established_get_status(packet);
            if (st != ERROR_CODE_SUCCESS){
                printf("A4F BT: flux refuse (0x%02x)\n", st);
                break;   // la déconnexion qui suit relancera une tentative
            }
            a2dp_subevent_stream_established_get_bd_addr(packet, addr);
            memcpy(device_addr, addr, 6);
            have_device = true;
            tlv_impl->store_tag(&tlv_context, TAG_LAST_DEVICE, device_addr, 6);
            media.local_seid = a2dp_subevent_stream_established_get_local_seid(packet);
            media.stream_opened = 1;
            state = ST_CONNECTED;
            status("Connecte a %s (%d Hz)", bd_addr_to_str(addr), sample_rate);
            a2dp_source_start_stream(media.a2dp_cid, media.local_seid);
            break;

        case A2DP_SUBEVENT_STREAM_STARTED:
            audio_start();
            printf("A4F BT: lecture demarree\n");
            break;

        case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
            send_media_packet();
            try_send();
            break;

        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            // le casque a mis en pause (ex. sorti des oreilles) : on relance la lecture
            printf("A4F BT: pause demandee par le casque\n");
            audio_stop();
            a2dp_source_start_stream(media.a2dp_cid, media.local_seid);
            break;

        case A2DP_SUBEVENT_STREAM_RELEASED:
            media.stream_opened = 0;
            audio_stop();
            break;

        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            if (a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet) != media.a2dp_cid) break;
            media.a2dp_cid = 0;
            audio_stop();
            status("Casque deconnecte");
            state = ST_IDLE;
            schedule_retry(RETRY_MS);
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------- démarrage

static uint8_t sdp_a2dp_source_buffer[150];

int a4f_bt_run(const a4f_bt_config_t *config){
    cfg = config;
    state = ST_OFF;
    a4f_ring_init(&pcm_ring);

    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_a4f_get_instance());
    if (cfg->dump_path){
        hci_dump_posix_fs_open(cfg->dump_path, HCI_DUMP_PACKETLOGGER);
        hci_dump_init(hci_dump_posix_fs_get_instance());
    }
    hci_transport_a4f_usb_set_api(cfg->usb, cfg->vid, cfg->pid);
    hci_init(hci_transport_a4f_usb_instance(), NULL);

    hci_set_master_slave_policy(0);
    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
    l2cap_init();
    a2dp_source_init();
    a2dp_source_register_packet_handler(&a2dp_packet_handler);

    avdtp_stream_endpoint_t *ep = a2dp_source_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC,
        sbc_capabilities, sizeof(sbc_capabilities), sbc_configuration_storage, sizeof(sbc_configuration_storage));
    if (!ep){
        status("Erreur interne : endpoint A2DP");
        return 1;
    }
    // 48 kHz demandé dès la connexion : les AirPods refusent de changer après coup
    avdtp_set_preferred_sampling_frequency(ep, 48000);
    media.local_seid = avdtp_local_seid(ep);
    avdtp_source_register_delay_reporting_category(media.local_seid);

    sdp_init();
    memset(sdp_a2dp_source_buffer, 0, sizeof(sdp_a2dp_source_buffer));
    a2dp_source_create_sdp_record(sdp_a2dp_source_buffer, sdp_create_service_record_handle(), AVDTP_SOURCE_FEATURE_MASK_PLAYER, NULL, NULL);
    sdp_register_service(sdp_a2dp_source_buffer);

    gap_set_local_name("PS4 A4F 00:00:00:00:00:00");
    gap_set_class_of_device(0x200408);

    hci_event_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_registration);

    if (hci_power_control(HCI_POWER_ON) != 0){
        status("Dongle Bluetooth introuvable");
        return 1;
    }
    btstack_run_loop_execute();

    hci_power_control(HCI_POWER_OFF);
    if (tlv_impl) btstack_tlv_posix_deinit(&tlv_context);
    return 0;
}

void a4f_bt_stop(void){
    btstack_run_loop_trigger_exit();
}

void a4f_bt_set_tone(bool on){
    tone_on = on;
}

uint32_t a4f_bt_push_pcm(const int16_t *pcm, uint32_t frames){
    return a4f_ring_push(&pcm_ring, pcm, frames);
}

void a4f_bt_debug(char *buf, int len){
    snprintf(buf, len, "etat %d, %d Hz, tonalite %d, paquets %u, lectures %u, tampon %u ms, sous-alim %u, jetees %u, debordements %u, statut '%s'",
             (int) state, sample_rate, (int) tone_on, (unsigned) packets, (unsigned) pulls, a4f_ring_fill(&pcm_ring) / 48,
             (unsigned) pcm_ring.underruns, (unsigned) pcm_ring.dropped,
             (unsigned) __atomic_load_n(&pcm_ring.overflows, __ATOMIC_RELAXED), last_status);
}
