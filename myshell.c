#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_CMDLINE_LEN 256

void show_prompt();
int get_cmd_line(char *cmdline);
void process_cmd(char *cmdline);

int main()
{
    char cmdline[MAX_CMDLINE_LEN];

    while (1)
    {
        show_prompt();
        if (get_cmd_line(cmdline) == -1)
            continue; /* empty line handling */

        process_cmd(cmdline);
    }
    return 0;
}

void process_cmd(char *cmdline)
{
    // TODO
    int count = 0;
    char *ptr = cmdline;
    while ((ptr = strchr(ptr, ' ')) != NULL)
    {
        count++;
        ptr++;
    }

    char *tokens[count + 2];
    tokens[0] = strtok(cmdline, " ");
    int i = 1;
    for (; i < count + 1; i++)
    {
        tokens[i] = strtok(NULL, " ");
    }
    tokens[count + 1] = NULL;
    if (fork() == 0)
    {
        execvp(tokens[0], tokens);
    }
    else
    {
        wait(0);
    }
}

void show_prompt()
{
    printf("myshell> ");
}

int get_cmd_line(char *cmdline)
{
    int i;
    int n;
    if (!fgets(cmdline, MAX_CMDLINE_LEN, stdin))
        return -1;
    // Ignore the newline character
    n = strlen(cmdline);
    cmdline[--n] = '\0';
    i = 0;
    while (i < n && cmdline[i] == ' ')
    {
        ++i;
    }
    if (i == n)
    {
        // Empty command
        return -1;
    }
    return 0;
}
