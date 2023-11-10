#include <dirent.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct processInfo {
    int pid;
    char username[33]; // username can be 32 characters, without null char
    unsigned long totalcputime;
    unsigned long virtualmemory;
    char commands[16];
};

uid_t getUid(unsigned int pid) {
    char tempbuffer[1024];
    sprintf(tempbuffer, "/proc/%u/status", pid);
    FILE *status = fopen(tempbuffer, "r");
    uid_t uid;
    char buffer[1024];
    while (fgets(buffer, 1024, status) != NULL) {
        char randomString[1024];
        unsigned int num = 0;
        sscanf(buffer, "%s%*u%u%*s", randomString, &num);
        if (strcmp(randomString, "Uid:") == 0) {
            uid = num;
        }
    }
    fclose(status);
    return uid;
}

void _printProcess(struct processInfo *process) {
    if (process == NULL) {
        printf("%7s %-20s %8s %10s %s\n", "PID", "USER", "TIME", "VIRT", "CMD"); //header
    } else {
        process->totalcputime /= sysconf(_SC_CLK_TCK);
        int hour = process->totalcputime / 3600.0;
        int minute = process->totalcputime / 60.0;
        int second = process->totalcputime;
        minute %= 60;
        second %= 60;
        printf("%7d %-20s %02d:%02d:%02d %10lu %s\n", process->pid, process->username, hour, minute, second, process->virtualmemory >> 10, process->commands);
    }
}

struct processInfo *getThings(int pid) {
    char tempbuffer[1024];
    sprintf(tempbuffer, "/proc/%d/stat", pid);
    FILE *stat = fopen(tempbuffer, "r");

    char buffer[1024];
    fgets(buffer, 1024, stat);

    int i;
    int j = 0;
    char *tokns[52];
    tokns[j++] = buffer;
    for (i = 0; i < 1024 || j < 52; i++) {
        /* code */
        if (buffer[i] == ' ') {
            tokns[j++] = &buffer[i + 1];
            buffer[i] = 0;
        }
    }
    fclose(stat);

    // get pid
    struct processInfo *infos = malloc(sizeof(struct processInfo));
    infos->pid = atoi(tokns[0]); // First blank is pid.
    // get totalcputime
    unsigned long _time = atol(tokns[13]) + atol(tokns[14]);
    infos->totalcputime = _time;
    // get virtual memory
    unsigned long _vsize = atol(tokns[22]);
    infos->virtualmemory = _vsize;
    // get username
    uid_t uid = getUid(pid);
    struct passwd *pws;
    pws = getpwuid(uid);
    strcpy(infos->username, pws->pw_name); // maximum length should be 32 with null character I think
    // get commands name

    sprintf(tempbuffer, "/proc/%d/comm", pid);
    FILE *comm = fopen(tempbuffer, "r");
    fgets(infos->commands, 16, comm);

    for (i = 0; i < 16; i++) {
        if (infos->commands[i] == '\n') {
            infos->commands[i] = 0;
        }
    }
    fclose(comm);
    infos->commands[15] = '\0';
    return infos;
}

int cmpPid(const void *p1, const void *p2) {
    return ((struct processInfo *)p1)->pid - ((struct processInfo *)p2)->pid;
}

int cmpVm(const void *p1, const void *p2) {
    signed long temp = ((struct processInfo *)p2)->virtualmemory - ((struct processInfo *)p1)->virtualmemory;
    if (temp > 0)
        return 1;
    else if (temp < 0)
        return -1;
    else
        return 0;
}

int cmpCpu(const void *p1, const void *p2) {
    signed long temp = ((struct processInfo *)p2)->totalcputime - ((struct processInfo *)p1)->totalcputime;
    if (temp > 0)
        return 1;
    else if (temp < 0)
        return -1;
    else
        return 0;
}

#define SORT_BY_PID 0
#define SORT_BY_VMET 1
#define SORT_BY_CPU 2

int printProcess(int sortFlags, const char *username) {
    DIR *d;
    struct dirent *dir;
    int count = 0;
    int (*callback)(const void *, const void *) = NULL;
    switch (sortFlags) {
    case SORT_BY_PID:
        callback = &cmpPid;
        break;
    case SORT_BY_CPU:
        callback = &cmpCpu;
        break;
    case SORT_BY_VMET:
        callback = &cmpVm;
        break;
    }
    /* The program assumes there's no change in amount of process during run time. */
    if (d = opendir("/proc/")) {
        while ((dir = readdir(d)) != NULL)
            if (atoi(dir->d_name) != 0) count += 1;
        rewinddir(d);
        struct processInfo *processes = malloc(sizeof(struct processInfo) * count);
        count = 0;
        while ((dir = readdir(d)) != NULL) {
            int pid = atoi(dir->d_name);
            if (pid != 0) {
                struct processInfo *temp = getThings(pid);
                if (username && strcmp(username, temp->username) != 0) {
                } else {
                    processes[count++] = *temp;
                }
                free(temp);
            }
        }
        qsort(processes, count, sizeof(struct processInfo), callback);
        int i;
        _printProcess(NULL);
        for (i = 0; i < count; i++) {
            _printProcess(&processes[i]);
        }
        free(processes);
    }
    closedir(d);
}

int main(int argc, char *argv[]) {
    int sortFlags = SORT_BY_PID;
    char *username = NULL;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp("-m", argv[i]) == 0) {
            sortFlags = SORT_BY_VMET;
        }
        if (strcmp("-p", argv[i]) == 0) {
            sortFlags = SORT_BY_CPU;
        }
        if (strcmp("-u", argv[i]) == 0) {
            username = argv[++i];
        }
    }
    if (username && getpwnam(username) == 0)
        printf("invalid user: %s\n", username);
    else
        printProcess(sortFlags, username);
    return 0;
}
