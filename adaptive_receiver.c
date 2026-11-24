#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <arpa/inet.h>

#define PORT 8080

typedef struct {
    uint32_t seq_num;
    double   gen_timestamp;
    float    signal_val;
} __attribute__((packed)) status_pkt_t;

// Lightweight Feedback ACK sent back to Transmitter
typedef struct {
    uint32_t ack_seq;
    double   system_delay_ms; // Queueing + Service delay observed at receiver
} __attribute__((packed)) ack_pkt_t;

double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + (ts.tv_nsec / 1e9);
}

ssize_t read_exact(int fd, void *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = recv(fd, (char *)buf + total, count - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

int main(int argc, char *argv[]) {
    // Optional arg 1: initial service time (ms).
    // If arg 2 is passed, service time shifts halfway through to test dynamic adaptation!
    int service_delay_ms = (argc > 1) ? atoi(argv[1]) : 50;
    int shifted_delay_ms = (argc > 2) ? atoi(argv[2]) : service_delay_ms;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    listen(server_fd, 1);

    printf("[Adaptive Receiver] Listening on port %d (Service: %d ms -> %d ms)...\n",
           PORT, service_delay_ms, shifted_delay_ms);
    int client_fd = accept(server_fd, NULL, NULL);
    printf("[Adaptive Receiver] Transmitter connected!\n\n");

    FILE *log_fp = fopen("aoi_adaptive.csv", "w");
    fprintf(log_fp, "seq_num,gen_time,recv_time,peak_aoi_ms,post_update_aoi_ms\n");

    status_pkt_t pkt;
    double last_gen_time = 0.0;
    double sum_peak_aoi = 0.0;
    double sum_post_aoi = 0.0;
    uint32_t count = 0;

    while (read_exact(client_fd, &pkt, sizeof(pkt)) == sizeof(pkt)) {
        // Simulate dynamic network/processing load shift after packet 50
        int current_service = (pkt.seq_num > 50) ? shifted_delay_ms : service_delay_ms;
        usleep(current_service * 1000);

        double recv_time = now_sec();
        if (last_gen_time == 0.0) last_gen_time = pkt.gen_timestamp;

        double peak_aoi_ms = (recv_time - last_gen_time) * 1000.0;
        if (pkt.gen_timestamp > last_gen_time) {
            last_gen_time = pkt.gen_timestamp;
        }
        double post_aoi_ms = (recv_time - last_gen_time) * 1000.0;

        sum_peak_aoi += peak_aoi_ms;
        sum_post_aoi += post_aoi_ms;
        count++;

        // Send feedback ACK with measured system delay back to transmitter
        ack_pkt_t ack = {
            .ack_seq = pkt.seq_num,
            .system_delay_ms = post_aoi_ms
        };
        if (send(client_fd, &ack, sizeof(ack), 0) <= 0) break;

        printf("Seq: %3u | Peak AoI: %7.2f ms | System Delay: %7.2f ms | Avg Peak: %7.2f ms\n",
               pkt.seq_num, peak_aoi_ms, post_aoi_ms, sum_peak_aoi / count);

        fprintf(log_fp, "%u,%.6f,%.6f,%.4f,%.4f\n",
                pkt.seq_num, pkt.gen_timestamp, recv_time, peak_aoi_ms, post_aoi_ms);
    }

    printf("\n--- Adaptive Summary (%u packets) ---\n", count);
    printf("Average Peak AoI : %.2f ms\n", sum_peak_aoi / count);
    printf("Average Post AoI : %.2f ms\n", sum_post_aoi / count);

    fclose(log_fp);
    close(client_fd);
    close(server_fd);
    return 0;
}
