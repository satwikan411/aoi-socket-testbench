#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <arpa/inet.h>

#define PORT 8080
#define WIN_SIZE 10

typedef struct {
    uint32_t seq_num;
    double   gen_timestamp;
    float    signal_val;
} __attribute__((packed)) status_pkt_t;

typedef struct {
    uint32_t ack_seq;
    double   system_delay_ms;
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

// BBR-style sliding window minimum to track shifting link baseline delay
double get_window_min(double *history, int count) {
    double min_val = history[0];
    for (int i = 1; i < count; i++) {
        if (history[i] < min_val) min_val = history[i];
    }
    return min_val;
}

int main(int argc, char *argv[]) {
    double interval_ms = (argc > 1) ? atof(argv[1]) : 10.0;
    int total_packets  = (argc > 2) ? atoi(argv[2]) : 100;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    printf("[Adaptive Transmitter] Starting at %.1f ms interval (Windowed BBR-style AoI Controller)...\n\n",
           interval_ms);

    FILE *rate_log = fopen("rate_adaptation.csv", "w");
    fprintf(rate_log, "seq_num,system_delay_ms,queue_delay_ms,adapted_interval_ms\n");

    double delay_win[WIN_SIZE];
    int win_count = 0;
    int win_idx = 0;
    const double QUEUE_THRESHOLD_MS = 8.0;

    for (uint32_t i = 1; i <= (uint32_t)total_packets; i++) {
        status_pkt_t pkt;
        pkt.seq_num = i;
        pkt.gen_timestamp = now_sec();
        pkt.signal_val = sinf(i * 0.1f);

        if (send(sock, &pkt, sizeof(pkt), 0) <= 0) break;

        ack_pkt_t ack;
        if (read_exact(sock, &ack, sizeof(ack)) != sizeof(ack)) break;

        // Update sliding window of recent system delays
        delay_win[win_idx] = ack.system_delay_ms;
        win_idx = (win_idx + 1) % WIN_SIZE;
        if (win_count < WIN_SIZE) win_count++;

        double base_delay = get_window_min(delay_win, win_count);
        double est_queue_delay = ack.system_delay_ms - base_delay;

        // Closed-loop AIMD rate adaptation
        if (est_queue_delay > QUEUE_THRESHOLD_MS) {
            interval_ms = interval_ms * 1.25 + 2.0;
        } else {
            // Probe aggressively down toward the observed service delay
            interval_ms -= 5.0;
            if (interval_ms < base_delay) interval_ms = base_delay;
        }
        if (interval_ms > 500.0) interval_ms = 500.0;

        printf("Ack: %3u | System Delay: %6.2f ms | Base: %6.2f ms | Queue: %5.2f ms | Interval: %6.2f ms\n",
               ack.ack_seq, ack.system_delay_ms, base_delay, est_queue_delay, interval_ms);

        fprintf(rate_log, "%u,%.4f,%.4f,%.4f\n",
                ack.ack_seq, ack.system_delay_ms, est_queue_delay, interval_ms);

        double sleep_ms = interval_ms - ack.system_delay_ms;
        if (sleep_ms > 0) {
            usleep((useconds_t)(sleep_ms * 1000.0));
        }
    }

    fclose(rate_log);
    close(sock);
    printf("\n[Adaptive Transmitter] Finished. Rate trajectory saved to rate_adaptation.csv\n");
    return 0;
}
