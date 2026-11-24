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
    int service_delay_ms = (argc > 1) ? atoi(argv[1]) : 50;

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

    printf("[Receiver] Listening on port %d (Service time = %d ms)...\n", PORT, service_delay_ms);
    int client_fd = accept(server_fd, NULL, NULL);
    printf("[Receiver] Transmitter connected!\n\n");

    FILE *log_fp = fopen("aoi_baseline.csv", "w");
    fprintf(log_fp, "seq_num,gen_time,recv_time,peak_aoi_ms,post_update_aoi_ms\n");

    status_pkt_t pkt;
    double last_gen_time = 0.0;
    double sum_peak_aoi = 0.0;
    double sum_post_aoi = 0.0;
    uint32_t count = 0;

    while (read_exact(client_fd, &pkt, sizeof(pkt)) == sizeof(pkt)) {
        usleep(service_delay_ms * 1000);

        double recv_time = now_sec();
        if (last_gen_time == 0.0) last_gen_time = pkt.gen_timestamp;

        double peak_aoi = recv_time - last_gen_time;
        if (pkt.gen_timestamp > last_gen_time) {
            last_gen_time = pkt.gen_timestamp;
        }
        double post_aoi = recv_time - last_gen_time;

        sum_peak_aoi += peak_aoi;
        sum_post_aoi += post_aoi;
        count++;

        printf("Seq: %3u | Peak AoI: %7.2f ms | System Delay (Post-AoI): %7.2f ms | Avg Peak: %7.2f ms\n",
               pkt.seq_num, peak_aoi * 1000.0, post_aoi * 1000.0, (sum_peak_aoi / count) * 1000.0);

        fprintf(log_fp, "%u,%.6f,%.6f,%.4f,%.4f\n",
                pkt.seq_num, pkt.gen_timestamp, recv_time, peak_aoi * 1000.0, post_aoi * 1000.0);
    }

    printf("\n--- Summary (%u packets) ---\n", count);
    printf("Average Peak AoI : %.2f ms\n", (sum_peak_aoi / count) * 1000.0);
    printf("Average Post AoI : %.2f ms\n", (sum_post_aoi / count) * 1000.0);

    fclose(log_fp);
    close(client_fd);
    close(server_fd);
    return 0;
}
