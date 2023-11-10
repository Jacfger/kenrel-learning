#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char buf[1024];
    FILE *fp = fopen("/proc/version", "r");
    fgets(buf, 1024, fp);
    char str1[1024];
    char str2[1024];
    sscanf(buf, "%s %*s %s %*s", str1, str2);
    if (argc > 1) {
        if (strcmp("-r", argv[1]) == 0) {
            printf("%s\n", str1);
        }
        else if (strcmp("-s", argv[1]) == 0) {
            printf("%s\n", str2);
        }
        else {
            printf("Invalid option\n");
        }
    }
    else {
        printf("%s\n", str1);
    }
    return 0;
}
