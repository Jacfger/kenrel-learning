#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void printMeminfo() {
    FILE *_meminfo = fopen("/proc/meminfo", "r");
    unsigned long memtotal;
    unsigned long memused;
    char buffer[1024];
    while (fgets(buffer, 1024, _meminfo) != NULL) {
        char randomString[1024];
        unsigned long _mem = 0;
        sscanf(buffer, "%s%lu %*s", randomString, &_mem);
        if (strcmp(randomString, "MemTotal:") == 0) {
            memtotal = _mem;
        } else if (strcmp(randomString, "MemFree:") == 0) {
            memused = memtotal - _mem;
        }
    }
    printf("Memory: %lu kB / %lu kB (%.1f%%)\n", memused, memtotal, ((double)memused / memtotal) * 100);
    fclose(_meminfo);
}

void printTime(float time) {
    int day = time / 86400.0;
    int hour = time / 3600.0;
    int minute = time / 60.0;
    int second = time;
    hour %= 24;
    minute %= 60;
    second %= 60;
    printf("Uptime: %d Day(s), %d:%02d:%02d\n", day, hour, minute,
           second); // should I how about when hour is more than two digit?
}

/* Return 1 if there isn't any suffix for the string cpu */
int isCpu(const char *str, int *id) {
    int i;
    const char *literal = "cpu";
    for (i = 0; i < 3; i++) {
        if (str[i] != literal[i])
            return 0;
    }
    if (str[3] == 0 || str[3] == ' ')
        return 0;
    if (id != NULL)
        scanf(&str[3], "%d %*s", id);
    return 1;
}

int countCpu() {
    FILE *stat = fopen("/proc/stat", "r");
    char buffer[1024];
    int count = 0;
    while (fgets(buffer, 1024, stat) != NULL) {
        char randomString[1024];
        if (isCpu(buffer, NULL) != 0) count += 1;
    }
    fclose(stat);
    return count;
}

void getCpuUsage(long *usedTime) {
    FILE *stat = fopen("/proc/stat", "r");
    char buffer[1024];
    int coreCount = 0;
    while (fgets(buffer, 1024, stat) != NULL) {
        char randomString[1024];
        int _mem, user, lowUser, kernel;
        int ret = sscanf(buffer, "%s %d %d %d %*d", randomString, &user, &lowUser, &kernel);
        if (isCpu(randomString, NULL))
            usedTime[coreCount++] = user + kernel + lowUser;
    }
    fclose(stat);
}

void printUsage(int i, double utilization) { printf("CPU%d:%5.1f%%  ", i, utilization); }

void printCpuUsage(int coreCounts, int period) {
    static long *states = NULL;
    if (states) {
        long *tempState = malloc(sizeof(long) * coreCounts);
        memcpy(tempState, states, sizeof(long) * coreCounts);
        getCpuUsage(states);
        int i = 0;
        for (i = 0; i < coreCounts; i++) {
            long ticks = sysconf(_SC_CLK_TCK);
            float utilization = (float)(states[i] - tempState[i]) / (period * ticks) * 100;
            printUsage(i, utilization);
        }
        printf("\n");
        free(tempState);
    } else {
        states = malloc(sizeof(long) * coreCounts);
        getCpuUsage(states);
        int i = 0;
        for (i = 0; i < coreCounts; i++) {
            printUsage(i, 0);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]) {
    int coreCounts = countCpu();
    char buffer[1024];
    float uptime = 0;
    float timer = 0;
    long *_states = malloc(sizeof(long) * coreCounts);
    int updatePeriod = 3;
    if (argc == 2) {
        updatePeriod = atoi(argv[1]);
        if (updatePeriod <= 0) {
            printf("You don't have infinite power, enter a positive integer\n");
            exit(1);
        }
    }
    while (1) {
        FILE *_uptime = fopen("/proc/uptime", "r");
        fgets(buffer, 1024, _uptime);
        sscanf(buffer, "%f %*s", &uptime);

        if (uptime - timer >= updatePeriod) {
            printTime(uptime);
            printMeminfo();
            printCpuUsage(coreCounts, updatePeriod);
            printf("\n");

            timer = uptime;
        }
        fclose(_uptime);
    }

    return 0;
}
