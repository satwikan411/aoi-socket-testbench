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

// Render a horizontal ASCII bar representing Peak AoI (scaled: 1 '#' = 10 ms, max 50 chars)
void print_ascii_bar(uint32_t seq, double peak_aoi_ms, double post_aoi_ms) {
    int bar_len = (int)(peak_aoi_ms / 10.0);
    if (bar_len > 50) bar_len = 50;
    if (bar_len < 1)  bar_len = 1;

    // Color code: Green (<150ms), Yellow (150-300ms), Red (>300ms)
    const char *color = "\033[32m";
    if (peak_aoi_ms > 300.0) color = "\033[31m";
    else if (peak_aoi_ms > 150.0) color = "\033[33m";

    printf("Seq %3u | Peak: %6.1f ms | Delay: %5.1f ms | %s",
           seq, peak_aoi_ms, post_aoi_ms, color);
    for (int i = 0; i < bar_len; i++) putchar('#');
    printf("\033[0m\n");
}

// Invoke gnuplot directly from C via POSIX pipe to generate PNG charts
void generate_gnuplot_charts() {
    FILE *gp = popen("gnuplot", "w");
    if (!gp) {
        perror("popen gnuplot failed");
        return;
    }

    fprintf(gp, "set datafile separator ','\n");
    fprintf(gp, "set terminal pngcairo size 1000,600 enhanced font 'Sans,11'\n");
    fprintf(gp, "set output 'aoi_live_run.png'\n");
    fprintf(gp, "set title 'Real-Time Age of Information (AoI) Trajectory (Adaptive C Testbench)'\n");
    fprintf(gp, "set xlabel 'Packet Sequence Number'\n");
    fprintf(gp, "set ylabel 'Age / Delay (ms)'\n");
    fprintf(gp, "set grid\n");
    fprintf(gp, "set key top left\n");
    fprintf(gp, "plot 'aoi_visual.csv' every ::1 using 1:4 with linespoints lw 2 pt 7 ps 0.6 lc rgb '#D9534F' title 'Peak AoI (ms)', \\\n");
    fprintf(gp, "     'aoi_visual.csv' every ::1 using 1:5 with lines lw 2 lc rgb '#0275D8' title 'System Delay / Post-Update AoI (ms)'\n");
    fflush(gp);
    pclose(gp);

    printf("[Gnuplot] Saved high-resolution chart to: aoi_live_run.png\n");
}

int main(int argc, char *argv[]) {
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

    printf("[Visual Receiver] Listening on port %d (Service: %d ms -> %d ms)...\n",
           PORT, service_delay_ms, shifted_delay_ms);
    int client_fd = accept(server_fd, NULL, NULL);

    // Clear screen and print header
    printf("\033[2J\033[H");
    printf("=== LIVE AGE OF INFORMATION (AoI) MONITOR (1 '#' = 10 ms) ===\n\n");

    FILE *log_fp = fopen("aoi_visual.csv", "w");
    fprintf(log_fp, "seq_num,gen_time,recv_time,peak_aoi_ms,post_update_aoi_ms\n");

    status_pkt_t pkt;
    double last_gen_time = 0.0;
    double sum_peak_aoi = 0.0;
    double sum_post_aoi = 0.0;
    uint32_t count = 0;

    while (read_exact(client_fd, &pkt, sizeof(pkt)) == sizeof(pkt)) {
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

        ack_pkt_t ack = {
            .ack_seq = pkt.seq_num,
            .system_delay_ms = post_aoi_ms
        };
        if (send(client_fd, &ack, sizeof(ack), 0) <= 0) break;

        print_ascii_bar(pkt.seq_num, peak_aoi_ms, post_aoi_ms);

        fprintf(log_fp, "%u,%.6f,%.6f,%.4f,%.4f\n",
                pkt.seq_num, pkt.gen_timestamp, recv_time, peak_aoi_ms, post_aoi_ms);
    }

    fclose(log_fp);
    close(client_fd);
    close(server_fd);

    printf("\n--- Summary (%u packets) ---\n", count);
    printf("Average Peak AoI : %.2f ms\n", sum_peak_aoi / count);
    printf("Average Post AoI : %.2f ms\n", sum_post_aoi / count);

    generate_gnuplot_charts();
    return 0;
}
