#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <arpa/inet.h>

#define PORT 8080

typedef struct {
    uint32_t seq_num;
    double   gen_timestamp;
    float    signal_val;
} __attribute__((packed)) status_pkt_t;

double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + (ts.tv_nsec / 1e9);
}

int main(int argc, char *argv[]) {
    int sample_interval_ms = (argc > 1) ? atoi(argv[1]) : 20;
    int total_packets      = (argc > 2) ? atoi(argv[2]) : 100;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed (Start receiver first)");
        return 1;
    }

    printf("[Transmitter] Streaming %d updates every %d ms...\n", total_packets, sample_interval_ms);

    for (uint32_t i = 1; i <= (uint32_t)total_packets; i++) {
        status_pkt_t pkt;
        pkt.seq_num = i;
        pkt.gen_timestamp = now_sec();
        pkt.signal_val = sinf(i * 0.1f);

        if (send(sock, &pkt, sizeof(pkt), 0) <= 0) {
            perror("send");
            break;
        }
        usleep(sample_interval_ms * 1000);
    }

    close(sock);
    printf("[Transmitter] Finished transmission.\n");
    return 0;
}
