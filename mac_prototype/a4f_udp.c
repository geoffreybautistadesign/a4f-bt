#include "a4f_udp.h"
#include "a4f_protocol.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_FRAMES 2048
#define MAX_PORTS  8

static a4f_ring_t *udp_ring;
static int udp_sock = -1;
static _Atomic uint32_t stat_packets, stat_lost;

static void *udp_thread(void *arg){
    (void) arg;
    static uint8_t pkt[sizeof(A4fAudioHeader) + MAX_FRAMES * 4];
    uint32_t next_seq[MAX_PORTS] = {0};
    int seen[MAX_PORTS] = {0};
    while (1){
        ssize_t n = recv(udp_sock, pkt, sizeof(pkt), 0);
        if (n < (ssize_t) sizeof(A4fAudioHeader)) continue;
        A4fAudioHeader h;
        memcpy(&h, pkt, sizeof(h));
        if (h.magic != A4F_MAGIC_AUDIO || h.version != A4F_VERSION) continue;
        if (h.frames > MAX_FRAMES || (size_t) n != sizeof(h) + (size_t) h.frames * 4) continue;
        // TODO PS4 : mixer les ports quand un jeu en ouvre plusieurs ; ici on ne garde que le port 0.
        if (h.port != 0) continue;
        if (seen[0] && h.seq != next_seq[0]) atomic_fetch_add(&stat_lost, h.seq - next_seq[0]);
        seen[0] = 1;
        next_seq[0] = h.seq + 1;
        atomic_fetch_add(&stat_packets, 1);
        a4f_ring_push(udp_ring, (const int16_t *)(pkt + sizeof(h)), h.frames);
    }
    return NULL;
}

int a4f_udp_start(a4f_ring_t *ring, uint16_t port){
    udp_ring = ring;
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(udp_sock, (struct sockaddr *) &a, sizeof(a)) < 0){
        close(udp_sock);
        return -1;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, udp_thread, NULL) != 0) return -1;
    pthread_detach(t);
    return 0;
}

void a4f_udp_get_stats(a4f_udp_stats_t *out){
    out->packets = atomic_load(&stat_packets);
    out->lost    = atomic_load(&stat_lost);
}
