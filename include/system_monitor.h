#ifndef __SYSTEM_MONITOR_H
#define __SYSTEM_MONITOR_H

#include <stdio.h>

// /proc/stat 的 cpu 字段
struct cpu_stat {
    unsigned long user;
    unsigned long nice;
    unsigned long system;
    unsigned long idle;
    unsigned long iowait;
    unsigned long irq;
    unsigned long softirq;
    unsigned long steal;
};

int system_monitor_init();
void system_monitor_close();

int cpu_usage();
int memory_usage();
double system_uptime();


#endif