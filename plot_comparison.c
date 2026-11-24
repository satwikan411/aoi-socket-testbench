#include <stdio.h>
#include <stdlib.h>

int main() {
    FILE *gp = popen("gnuplot", "w");
    if (!gp) {
        perror("popen gnuplot failed");
        return 1;
    }

    fprintf(gp, "set datafile separator ','\n");
    fprintf(gp, "set terminal pngcairo size 1100,650 enhanced font 'Sans,11'\n");
    fprintf(gp, "set output 'aoi_final_comparison.png'\n");
    fprintf(gp, "set title 'Age of Information (AoI) Comparison: Static Sampling vs. Closed-Loop Adaptive Control'\n");
    fprintf(gp, "set xlabel 'Packet Sequence Number'\n");
    fprintf(gp, "set ylabel 'Peak Age of Information (ms) [Log Scale]'\n");
    fprintf(gp, "set logscale y\n");
    fprintf(gp, "set grid ytics xtics\n");
    fprintf(gp, "set key top left box opaque\n");

    fprintf(gp, "plot 'aoi_10ms.csv' every ::1 using 1:4 with lines lw 2 lc rgb '#D9534F' title 'Static Over-Sampling (10 ms - Bufferbloat)', \\\n");
    fprintf(gp, "     'aoi_200ms.csv' every ::1 using 1:4 with lines lw 2 lc rgb '#F0AD4E' title 'Static Under-Sampling (200 ms - Starvation)', \\\n");
    fprintf(gp, "     'aoi_55ms.csv' every ::1 using 1:4 with lines lw 2 lc rgb '#5BC0DE' title 'Static Oracle-Tuned (55 ms)', \\\n");
    fprintf(gp, "     'aoi_visual.csv' every ::1 using 1:4 with linespoints lw 2 pt 7 ps 0.5 lc rgb '#5CB85C' title 'Closed-Loop Adaptive Controller (50->120 ms Shift)'\n");

    fflush(gp);
    pclose(gp);

    printf("[Success] Generated combined benchmark plot: aoi_final_comparison.png\n");
    return 0;
}
