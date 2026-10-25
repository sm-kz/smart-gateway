#include "system_monitor.h"
#include <unistd.h>
#include <string.h>
#include <sys/timerfd.h>
#include <pthread.h>

#define MAX_ERR_TIMES   10

int __cpu_usage;

static int cpu_read(struct cpu_stat *stat) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("open file /proc/stat");
        return -1;
    }

    if (fscanf(fp, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &stat->user, &stat->nice, &stat->system, &stat->idle,
               &stat->iowait, &stat->irq, &stat->softirq, &stat->steal) != 8) {
        perror("fscanf");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

inline static unsigned long long cpu_total(struct cpu_stat *stat) {
    return stat->user + stat->nice + stat->system + stat->idle +
           stat->iowait + stat->irq + stat->softirq + stat->steal;
}

static void update_cpu_usage(struct cpu_stat *from, struct cpu_stat *to) {
    unsigned long long total = cpu_total(to) - cpu_total(from);
    unsigned long long idle = (to->idle + to->iowait) - (from->idle + from->iowait);

    if (total == 0)
        __cpu_usage = 0;
    else
        __cpu_usage = 100 * (total - idle) / total;
}

int cpu_usage() {
    return __cpu_usage;
}

int memory_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("open file /proc/meminfo");
        return -1;
    }

    char key[64];
    unsigned long val;
    char unit[16];

    unsigned long mem_total, mem_avai;
    int total_found = 0, avai_found = 0;

    while (fscanf(fp, "%63s %lu %15s", key, &val, unit) == 3) {
        if (!strcmp(key, "MemTotal:")) {
            mem_total = val;
            total_found = 1;
        }

        if (!strcmp(key, "MemAvailable:")) {
            mem_avai = val;
            avai_found = 1;
        }

        if (total_found && avai_found) {
            fclose(fp);

            if (mem_total == 0)
                return 0;

            return 100 * (mem_total - mem_avai) / mem_total;
        }
    }

    fclose(fp);
    return -1;
}

double system_uptime() {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) {
        perror("open file /proc/uptime");
        return -1.0;
    }

    double seconds;
    if (fscanf(fp, "%lf", &seconds) != 1) {
        perror("fscanf");
        fclose(fp);
        return -1.0;
    }

    fclose(fp);
    return seconds;
}

pthread_t monitor_thread;
void *monitor_routine(void *arg) {
    static struct cpu_stat from, to;
    int err_cnt = 0;

    if (cpu_read(&from))
        return NULL;

    while (1) {
        pthread_testcancel();

        sleep(1);
        if (cpu_read(&to)) {
            ++err_cnt;
            if (err_cnt >= MAX_ERR_TIMES) {
                fprintf(stderr, "system monitor aborted: /proc/stat may be unavailable\r\n");
                return NULL;
            }
            continue;
        }

        err_cnt = 0;

        update_cpu_usage(&from, &to);
        from = to;
    }

    return NULL;
}

int system_monitor_init() {
    if (pthread_create(&monitor_thread, NULL, monitor_routine, NULL)) {
        fprintf(stderr, "%s: pthread_create failed\r\n", __func__);
        return -1;
    }

    return 0;
}

void system_monitor_close() {
    pthread_cancel(monitor_thread);
    pthread_join(monitor_thread, NULL);
}
