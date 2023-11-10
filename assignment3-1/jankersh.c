#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if 0 // Enable debugging output
	#define __DEBUG
	#define _debugf2(f, line, ...)                   \
		fprintf(f, "DEBUG " #line ": " __VA_ARGS__); \
		fprintf(f, "\n")
	#define trace() printf("TRACE %d\n", __LINE__);
#else
	#define _debugf2(...)
	#define trace()
#endif
#define _debugf(line, ...) _debugf2(stdout, line, __VA_ARGS__)
#define debugf(...)		   _debugf(__LINE__, __VA_ARGS__);

#define _errorf(line, ...) _debugf2(stderr, line, __VA_ARGS__)
#define errorf(...)		   _errorf(__LINE__, __VA_ARGS__)

#define FILE_DESCRIPTOR_MAXSIZE 4
#define MAX_BG_JOBS				1024
#define MAX_PIPES				1024


char CURR_PATH[4096];
char PREV_PATH[4096];
int childExitStatus = 0;
int interrupted = 0;
int _shellGroup = 0;
int nJobs = 1;

enum job_status {
	PENDING = 0,
	RUNNING,
	HIATUS,
	DONE
};
struct Job {
	TAILQ_ENTRY(Job)
	traverser;
	int isBackground;
	char **tokenized;
	int ntok;
	char *cmd;
	enum job_status status;
	pid_t groupId;
	int nPipe;
	int finishCount;
	int jobId;
};

int execJob2(struct Job *);

TAILQ_HEAD(JobListHead, Job);
struct JobListHead *joblist_head = NULL;
struct Job currFgJob = {.traverser = NULL, .isBackground = 0, .tokenized = NULL, .ntok = 0, .cmd = 0, .status = DONE};


void JOBLIST_INIT() {
	joblist_head = malloc(sizeof(struct JobListHead));
	TAILQ_INIT(joblist_head);
}
void JOBLIST_DEINIT() { free(joblist_head); }

struct Job *findJobByGrpId(pid_t gpid) {
	struct Job *var;
	TAILQ_FOREACH(var, joblist_head, traverser) {
		if (var->groupId == gpid)
			return var;
	}
#ifdef __DEBUG
	char *__debug_message = "Job is not found\n";
	write(STDERR_FILENO, __debug_message, sizeof(__debug_message));
#endif
	return NULL;
}

struct Job *findJobByJobId(int jobId) {
	struct Job *var;
	TAILQ_FOREACH(var, joblist_head, traverser) {
		if (var->jobId == jobId)
			return var;
	}
#ifdef __DEBUG
	char *__debug_message = "Job is not found\n";
	write(STDERR_FILENO, __debug_message, sizeof(__debug_message));
#endif
	return NULL;
}

void removeJob(struct Job *j) {
	if (j != NULL) {
		free(j->tokenized);
		free(j->cmd);
		TAILQ_REMOVE(joblist_head, j, traverser);
		free(j);
	}
}

void theReaper(int);
void SHELL_INIT() {
	JOBLIST_INIT();
	_shellGroup = getpid();
	struct sigaction act;
	act.sa_handler = theReaper;
	sigemptyset(&act.sa_mask);
	// act.sa_flags = SA_NOCLDSTOP | SA_RESTART; // The reaper wouldn't be called when the
	act.sa_flags = SA_RESTART; // The reaper wouldn't be called when the
	if (sigaction(SIGCHLD, &act, 0) == -1) {
		perror("sigaction");
		exit(1);
	}
}

struct Launch {
	TAILQ_ENTRY(Launch)
	traverser;
	char *cmds;
	int status;
};

TAILQ_HEAD(LaunchSequence, Launch);
struct LaunchSequence *launchHead;

void theReaper(int signal) {
	int ret;
#ifdef __DEBUG
	char _buf[] = "A job Ended\n";
	char _buf2[] = "handler is called\n";
	char _textBuf[512];
#endif
	struct Job *var;
	TAILQ_FOREACH(var, joblist_head, traverser) {
		if (var->status == RUNNING) {
			int status_code;
			while ((ret = waitpid(-var->groupId, &status_code, WNOHANG | WUNTRACED)) > 0) {
				if (WIFSTOPPED(status_code))
					var->status = HIATUS;
				else if (++var->finishCount == var->nPipe)
					var->status = DONE;
#ifdef __DEBUG
				sprintf(_textBuf, "One job in process group %d had finished. Job Count %d/%d\n", var->groupId, var->finishCount, var->nPipe);
				write(STDOUT_FILENO, _textBuf, strlen(_textBuf));
				write(STDOUT_FILENO, _buf2, sizeof(_buf2));
#endif
			}
		}
	}
}

void printTokenized(char **tokenized, int length) {
	int i = 0;
	for (; i < length; i++) {
		if (tokenized[i][0] == '\0') {
			debugf("NULL\n");
		} else {
			debugf("%s\n", tokenized[i]);
		}
	}
}

unsigned int tokenize(char *str, char **tokenize, char *separators) {
	char *token;
	unsigned int i = 0;
	for (token = strtok(str, separators); token != NULL; token = strtok(NULL, separators)) {
		tokenize[i++] = token;
	}
	return i;
}

unsigned int tokenize2(char *str, char **tokenize) {
	unsigned int i = 0;
	int tokenIndex = 0;
#define start  0
#define normal 1
#define space  2
#define dquote 3
#define quote  4
	int __state = start;
	char *copyString = malloc(MAX_BG_JOBS * sizeof(char));
	char *currentToken = copyString;
	int csIndex = 0;
	// strcpy(copyString, str);
	tokenize[tokenIndex++] = currentToken;
	for (; str[i] != '\0'; i++) {
		volatile char c = str[i];
		if (str[i] == ' ' || str[i] == '\t') {
			switch (__state) {
				case normal:
					copyString[csIndex++] = '\0';
					__state = space;
				case start: // this happen when there's preceding space.
					break;
				default:
					copyString[csIndex++] = str[i];
					break;
			}
		} else if (str[i] == '"') {
			switch (__state) {
				case dquote:
					__state = normal;
					break;
				case space:
					currentToken = copyString + csIndex;
					tokenize[tokenIndex++] = currentToken;
				case normal:
					__state = dquote;
					break;
				default:
					copyString[csIndex++] = str[i];
					break;
			}
		} else if (str[i] == '\'') {
			switch (__state) {
				case quote:
					__state = normal;
					break;
				case space:
					currentToken = copyString + csIndex;
					tokenize[tokenIndex++] = currentToken;
				case normal:
					__state = quote;
					break;
				default:
					copyString[csIndex++] = str[i];
					break;
			}
		} else if (str[i] == '$') {
			if (__state == space) {
				currentToken = copyString + csIndex;
				tokenize[tokenIndex++] = currentToken;
				__state = normal;
			}
			if (strncmp("$$", &str[i], 2) == 0 && __state != quote) {
				sprintf(copyString + csIndex, "%d", getpid());
				csIndex += strnlen(copyString + csIndex, 10);
				i++;
			} else if (strncmp("$?", &str[i], 2) == 0 && __state != quote) {
				sprintf(copyString + csIndex, "%d", childExitStatus);
				csIndex += strnlen(copyString + csIndex, 5);
				i++;
			} else {
				copyString[csIndex++] = str[i];
			}
		} else if (str[i] == '&') {
			switch (__state) {
				case start: // there could be no files
				case space:
				case normal:
					if (strncmp("&>", &str[i], 2) == 0) {
						if (__state != start) {
							copyString[csIndex++] = '\0';
							currentToken = copyString + csIndex;
							tokenize[tokenIndex++] = currentToken;
						}
						copyString[csIndex++] = '&';
						copyString[csIndex++] = '>';
						str[i + 1] = ' '; // This should force the function to add token
						__state = normal;
					} else {
						if (__state != start) {
							copyString[csIndex++] = '\0';
							currentToken = copyString + csIndex;
							tokenize[tokenIndex++] = currentToken;
						}
						copyString[csIndex++] = '&';
						str[i--] = ' ';
						__state = normal;
					}
					break;
				case dquote:
				case quote:
					copyString[csIndex++] = str[i];
					break;
			}
		} else if (str[i] == '>') {
			switch (__state) {
				case start: // there could be no files
				case space:
				case normal:
					if (strncmp(">>", &str[i], 2) == 0) {
						if (__state != start) {
							copyString[csIndex++] = '\0';
							currentToken = copyString + csIndex;
							tokenize[tokenIndex++] = currentToken;
						}
						copyString[csIndex++] = '>';
						copyString[csIndex++] = '>';
						str[i + 1] = ' '; // This should force the function to add token
						__state = normal;
					} else {
						if (__state != start) {
							copyString[csIndex++] = '\0';
							currentToken = copyString + csIndex;
							tokenize[tokenIndex++] = currentToken;
						}
						copyString[csIndex++] = '>';
						str[i--] = ' ';
						__state = normal;
					}
					break;
				case dquote:
				case quote:
					copyString[csIndex++] = str[i];
					break;
			}
		} else if (str[i] == '<') {
			switch (__state) {
				case start: // there could be no files
				case space:
				case normal:
					if (__state != start) {
						copyString[csIndex++] = '\0';
						currentToken = copyString + csIndex;
						tokenize[tokenIndex++] = currentToken;
					}
					copyString[csIndex++] = '<';
					str[i--] = ' ';
					__state = normal;
					break;
				case dquote:
				case quote:
					copyString[csIndex++] = str[i];
					break;
			}
		} else if (str[i] == '|') {
			switch (__state) {
				case start: // printf("Syntax error near '|'"); break;
				case space:
				case normal:
					if (__state != start) {
						copyString[csIndex++] = '\0';
						currentToken = copyString + csIndex;
						tokenize[tokenIndex++] = currentToken;
					}
					copyString[csIndex++] = '|';
					str[i--] = ' ';
					__state = normal; // This should force the program to add token
					break;
				case dquote:
				case quote:
					copyString[csIndex++] = str[i];
					break;
			}
		} else {
			if (str[i] != '\n') {
				copyString[csIndex++] = str[i];
				switch (__state) {
					case space:
						currentToken = copyString + csIndex - 1;
						tokenize[tokenIndex++] = currentToken;
					case start:
						__state = normal;
						break;
					default:
						break;
				}
				// if (__state == space) {
				// 	currentToken = copyString + csIndex - 1;
				// 	tokenize[tokenIndex++] = currentToken;
				// 	__state = normal;
				// }
			} else {
				copyString[csIndex++] = '\0';
			}
		}
	}
	return tokenIndex;
}

void printJobInfo(struct Job *var) {
	printf("[%d] ", var->jobId);
	switch (var->status) {
		case PENDING:
			printf("%10s", "PENDING");
			break;
		case RUNNING:
			printf("%10s", "RUNNING");
			break;
		case HIATUS:
			printf("%10s", "HIATUS");
			break;
		case DONE:
			printf("%10s", "DONE");
			break;
	}
	printf("          ");
	int i;
	for (i = 0; i < var->ntok; i++) {
		printf("%s", var->tokenized[i]);
		if (i != var->ntok - 1)
			printf(" ");
		else
			printf("\n");
	}
}

void printJobList() {
	struct Job *var;
	TAILQ_FOREACH(var, joblist_head, traverser) {
		if (var->status != DONE) {
			printJobInfo(var);
		}
	}
}

void cleanJobList() {
	struct Job *var, *prev = NULL;
	int jobCounts = 0;
	TAILQ_FOREACH(var, joblist_head, traverser) {
		if (var->status == DONE && var->isBackground == 1) {
			printJobInfo(var);
		}
		if (prev != NULL && prev->status == DONE) {
			removeJob(prev);
		}
		prev = var;
	}
	if (prev != NULL && prev->status == DONE) {
		removeJob(prev);
	}

	if (TAILQ_EMPTY(joblist_head)) {
		nJobs = 1;
	}
}

struct Job *currentJob() {
	struct Job *var;
	TAILQ_FOREACH_REVERSE(var, joblist_head, JobListHead, traverser) {
		if (var->status == HIATUS) {
			return var;
		}
	}
	TAILQ_FOREACH_REVERSE(var, joblist_head, JobListHead, traverser) {
		if (var->status == HIATUS) {
			return var;
		}
	}
	if (TAILQ_EMPTY(joblist_head)) {
		return NULL;
	} else {
		return TAILQ_LAST(joblist_head, JobListHead);
	}
}


int inBuiltCommands(char **tokenized, int length) {
	if (strcmp(tokenized[0], "cd") == 0) {
		if (length == 1) {
			if (chdir(getpwuid(getuid())->pw_dir) != 0) {
				fprintf(stderr, "cd: %s: %s\n", strerror(errno), tokenized[1]);
				return -1;
			}
			strncpy(PREV_PATH, CURR_PATH, 4096);
			return 0;
		} else if (length > 2) {
			printf("cd: Too many arguments");
			return -1;
		} else {
			if (strcmp(tokenized[1], "-") == 0) {
				tokenized[1] = PREV_PATH;
				printf("%s\n", PREV_PATH);
				return -1;
			}
			if (chdir(tokenized[1]) != 0) {
				fprintf(stderr, "cd: %s: %s\n", strerror(errno), tokenized[1]);
				return -1;
			}
			if (getcwd(CURR_PATH, 4096) == NULL) {
				perror("Get current directory failed");
				return -1;
			}
		}
		return 0;
	} else if (strcmp(tokenized[0], "jobs") == 0) {
		printJobList();
		return 0;
	} else if (strcmp(tokenized[0], "exit") == 0) {
		exit(0);
	} else if (strcmp(tokenized[0], "bg") == 0) {
		struct Job *job = NULL;
		if (tokenized[1] == NULL) {
			job = currentJob();
			if (job->status == RUNNING) {
				fprintf(stderr, "jankersh: bg: job is running in background\n");
				return -1;
			} else if (job->status == PENDING) {
				fprintf(stderr, "jankersh: bg: congrat, you've seen a bug\n");
				return -1;
			} else if (job->status == DONE) {
				fprintf(stderr, "jankersh: bg: job has terminated\n");
				return -1;
			}
		} else if (tokenized[1][0] == '%') {
			int jobNumber = atoi(&tokenized[1][1]);
			job = findJobByJobId(jobNumber);
			if (job == NULL) {
				fprintf(stderr, "jankersh: bg: %%%d: no such job\n", jobNumber);
				return -1;
			}
		} else {
			fprintf(stderr, "jankersh: bg: bro, type better lol");
			return -1;
		}
		job->isBackground = 1;
		execJob2(job);
		return 0;
	} else if (strcmp(tokenized[0], "fg") == 0) {
		struct Job *job = NULL;
		if (tokenized[1] == NULL) {
			job = currentJob();
			if (job->status == PENDING) {
				fprintf(stderr, "jankersh: fg: congrat, you've seen a bug\n");
				return -1;
			} else if (job->status == DONE) {
				fprintf(stderr, "jankersh: fg: job has terminated\n");
				return -1;
			}
		} else if (tokenized[1][0] == '%') {
			int jobNumber = atoi(&tokenized[1][1]);
			job = findJobByJobId(jobNumber);
			if (job == NULL) {
				fprintf(stderr, "jankersh: fg: %%%d: no such job\n", jobNumber);
				return -1;
			}
		} else {
			fprintf(stderr, "jankersh: fg: bro, type better lol");
			return -1;
		}
		job->isBackground = 0;
		execJob2(job);
		return 0;
	}
	return 1;
}

int commandLength(char **tokenized, int length) {
	int i;
	int _len = 0;
	for (i = 0; i < length; i++) {
		_len += strlen(tokenized[i]) + 1;
	}
	debugf("Command length: %d\n", _len);
	return _len;
}

char *cpyCmdTok(char **tokenized, int length, char **newTokenized) {
	char *__copy = malloc(commandLength(tokenized, length));
	int i;
	int _len = 0;
	for (i = 0; i < length; i++) {
		newTokenized[i] = __copy + _len;
		strcpy(__copy + _len, tokenized[i]);
		if (i != length - 1) {
			_len += strlen(tokenized[i]) + 1;
			*(__copy + _len - 1) = '\0';
		}
	}
	debugf("Copied command: %s", __copy);
	return __copy;
}

struct jobDescriptor {
	int fileDescriptor[4];
	int background;
	char **tokens;
	int ntok;
};

void setFileDescriptor(int descriptor[]) {
	int i;
	for (i = 0; i < 3; i++) {
		if (descriptor[i] > 0) {
			dup2(descriptor[i], i);
		}
	}
}

void closeFileDescriptor(int descriptor[]) {
	int i;
	for (i = 0; i < 3; i++) {
		if (descriptor[i] >= 0) {
			close(descriptor[i]);
		}
	}
}

sigset_t *blockSig(int SIG) {
	sigset_t block, *prev = malloc(sizeof(sigset_t));
	sigemptyset(&block);
	sigaddset(&block, SIG);
	sigprocmask(SIG_BLOCK, &block, prev);
	return prev;
}

void suspendSig() {
	sigset_t block;
	sigemptyset(&block);
	sigsuspend(&block);
}

int execJob2(struct Job *job) {
	if (strcmp(*job->tokenized, "") == 0) {
		job->status = DONE;
		job->isBackground = 0;
		return 0;
	}
	pid_t new_gpid = 0;
	if (job->status == PENDING) {
		job->status = RUNNING;
		char **processTok = malloc(sizeof(char *) * (job->ntok + 1));
		char **currTokens = processTok;
		int currCmdEnd = 0;
		job->cmd;
		int pipes[MAX_PIPES][2] = {
			[0 ... MAX_PIPES - 1] = {0, 0}};
		int pipSegI = 0;

		struct jobDescriptor descTable[MAX_PIPES] = {
			[0 ... MAX_PIPES - 1].fileDescriptor = {
				[0 ... 3] = -1},
			[0 ... MAX_PIPES - 1].background = 0,
			[0 ... MAX_PIPES - 1].ntok = 0,
			[0 ... MAX_PIPES - 1].tokens = NULL};

		char **cmdStartInd[MAX_PIPES] = {
			[0 ... MAX_PIPES - 1] = NULL,
			[0] = processTok};

		int i;
		for (i = 0; i < job->ntok; i++) {
			int fd;
			if (strcmp(job->tokenized[i], "<") == 0) {
				if ((fd = open(job->tokenized[++i], O_RDONLY, 0666)) < 0) {
					perror("Error when reading file");
					return 1;
				}
				descTable[pipSegI].fileDescriptor[STDIN_FILENO] = fd;
			} else if (strcmp(job->tokenized[i], "&>") == 0) {
				if ((fd = open(job->tokenized[++i], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
					perror("Error when writing file");
					return 1;
				}
				descTable[pipSegI].fileDescriptor[STDOUT_FILENO] = fd;
				descTable[pipSegI].fileDescriptor[STDERR_FILENO] = fd;
			} else if (strcmp(job->tokenized[i], ">>") == 0) {
				if ((fd = open(job->tokenized[++i], O_WRONLY | O_APPEND | O_CREAT, 0666)) < 0) {
					perror("Error when writing file");
					return 1;
				}
				descTable[pipSegI].fileDescriptor[STDOUT_FILENO] = fd;
			} else if (strcmp(job->tokenized[i], ">") == 0) {
				if ((fd = open(job->tokenized[++i], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
					perror("Error when writing file");
					return 1;
				}
				descTable[pipSegI].fileDescriptor[STDOUT_FILENO] = fd;
			} else if (strcmp(job->tokenized[i], "|") == 0) {
				processTok[currCmdEnd++] = NULL;
				pipSegI++;
			} else {
				if (cmdStartInd[pipSegI] == NULL) {
					cmdStartInd[pipSegI] = processTok + currCmdEnd;
				}
				processTok[currCmdEnd++] = job->tokenized[i];
			}
		}
		processTok[currCmdEnd] = 0;

		for (i = 0; i < pipSegI + 1; i++) {
			if (i != pipSegI && pipSegI != 0)
				pipe(pipes[i]);
			pid_t __children;
			if (i == 0) {
				new_gpid = fork();
				__children = new_gpid;
				setpgid(__children, new_gpid);
			} else {
				__children = fork();
				setpgid(__children, new_gpid);
			}
			if (__children == 0) {
				setpgid(0, new_gpid);
				setFileDescriptor(descTable[i].fileDescriptor);
				int j;
				for (j = 0; j < i - 1; j++) {
					close(pipes[j][STDOUT_FILENO]);
					errorf("janksh: %s: %s %d\n", strerror(errno), cmdStartInd[j][0], pipes[j][STDOUT_FILENO]);
					close(pipes[j][STDIN_FILENO]);
					errorf("janksh: %s: %s %d\n", strerror(errno), cmdStartInd[j][0], pipes[j][STDIN_FILENO]);
				}
				if (i != 0) {
					dup2(pipes[i - 1][STDIN_FILENO], STDIN_FILENO);
					errorf("janksh: %s: %s\n", strerror(errno), cmdStartInd[i][0]);
					close(pipes[i - 1][STDOUT_FILENO]);
					errorf("janksh: %s: %s\n", strerror(errno), cmdStartInd[i][0]);
				}
				if (i != pipSegI) {
					dup2(pipes[i][STDOUT_FILENO], STDOUT_FILENO);
					errorf("janksh: %s: %s\n", strerror(errno), cmdStartInd[i][0]);
					close(pipes[i][STDIN_FILENO]);
					errorf("janksh: %s: %s\n", strerror(errno), cmdStartInd[i][0]);
				}
				execvp(cmdStartInd[i][0], cmdStartInd[i]); // either return with error, either never return. No need to test
				fprintf(stderr, "janksh: %s: %s\n", strerror(errno), cmdStartInd[i][0]);
				exit(1);
			}
		}
		for (i = 0; i < pipSegI; i++) { // Close any unused pipe. It's closed here since it might cause children writing to a empty pipe.
			close(pipes[i][STDOUT_FILENO]);
			close(pipes[i][STDIN_FILENO]);
		}
		job->nPipe = pipSegI + 1;
		job->groupId = new_gpid;
		job->finishCount = 0;

		free(processTok);
	} else if (job->status == HIATUS) {
		new_gpid = job->groupId;
		job->status = RUNNING;
		kill(-new_gpid, SIGCONT);
	} else if (job->status == RUNNING) {
		new_gpid = job->groupId;
	} else {
		return 0;
	}

	if (job->isBackground == 1) {
		job->jobId = nJobs++;
		printJobInfo(job);
		return 0;
	}

	if (tcsetpgrp(STDIN_FILENO, new_gpid) == -1) {
		perror("tcsetpgrp error\n");
	}
	int ret = 0;
	// fprintf(stderr, "Waiting for group id: ret%5d\n", new_gpid);
	int child_status;
	sigset_t *prev = blockSig(SIGCHLD);
	while ((ret = waitpid(-new_gpid, &child_status, WUNTRACED)) > 0) { // At this time the exec should be doen (properties of vfork). So it should be fine to just fork here.
		int num;
		if (num = WIFSTOPPED(child_status)) {
			debugf("Stop signal, %d", num);
			job->isBackground = 1;
			job->status = HIATUS;
			job->jobId = nJobs++;
			break;
		}
	}

	/* If all children done*/
	if (ret < 0) { // This means there's no child is in the process group.
		job->status = DONE;
	}

	sigprocmask(SIG_UNBLOCK, prev, NULL);
	free(prev);
	prev = blockSig(SIGTTOU);
	if (tcsetpgrp(STDIN_FILENO, _shellGroup) == -1) {
		perror("tcsetpgrp error");
	}
	sigprocmask(SIG_UNBLOCK, prev, NULL);
	free(prev);
	return 0;
}

int execJob(struct Job *job) {
	pid_t bg_pid = 0;
	if (job->status != PENDING || strcmp(*job->tokenized, "") == 0) {
		return 0;
	}
	if ((bg_pid = fork()) > 0) {
		job->status = RUNNING;
		if (job->isBackground) {
			return 0;
		}
		suspendSig();
		blockSig(SIGTTOU);
		int ret = 0;
		while ((ret = waitpid(-bg_pid, 0, WNOHANG)) >= 0) {
			debugf("Waiting for childs: ret%d\n", ret);
			sleep(3);
		};
		debugf("Finish waiting for childs: ret%d\n", ret);
		tcsetpgrp(STDIN_FILENO, _shellGroup);
		return 0;
	}
	setpgid(0, getpid());
	blockSig(SIGTTOU);
	tcsetpgrp(STDIN_FILENO, getpid());
	// tcsetpgrp(STDIN_FILENO, _gLeaderId);
	char **processTok = malloc(sizeof(char *) * (job->ntok + 1));
	char **currTokens = processTok;
	int currCmdEnd = 0;
	int pipes[MAX_PIPES][2] = {
		[0 ... MAX_PIPES - 1] = {0, 0}};
	int pipSegI = 0;

	struct jobDescriptor descTable[MAX_PIPES] = {[0 ... MAX_PIPES - 1].fileDescriptor = {[0 ... 3] = -1},
												 [0 ... MAX_PIPES - 1].background = 0,
												 [0 ... MAX_PIPES - 1].ntok = 0,
												 [0 ... MAX_PIPES - 1].tokens = NULL};

	char **cmdStartInd[MAX_PIPES] = {
		[0 ... MAX_PIPES - 1] = NULL,
		[0] = processTok};

	int i;
	for (i = 0; i < job->ntok; i++) {
		int fd;
		if (strcmp(job->tokenized[i], "<") == 0) {
			if ((fd = open(job->tokenized[++i], O_RDONLY, 0666)) < 0) {
				perror("Error when reading file");
				exit(1);
			}
			descTable[pipSegI].fileDescriptor[STDIN_FILENO] = fd;
		} else if (strcmp(job->tokenized[i], "&>") == 0) {
			if ((fd = open(job->tokenized[++i], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
				perror("Error when writing file");
				exit(1);
			}
			descTable[pipSegI].fileDescriptor[STDOUT_FILENO] = fd;
			descTable[pipSegI].fileDescriptor[STDERR_FILENO] = fd;
		} else if (strcmp(job->tokenized[i], ">>") == 0) {
			if ((fd = open(job->tokenized[++i], O_APPEND | O_CREAT, 0666)) < 0) {
				perror("Error when writing file");
				exit(1);
			}
			descTable[pipSegI].fileDescriptor[STDOUT_FILENO] = fd;
		} else if (strcmp(job->tokenized[i], ">") == 0) {
			if ((fd = open(job->tokenized[++i], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
				perror("Error when writing file");
				exit(1);
			}
			descTable[pipSegI].fileDescriptor[STDOUT_FILENO] = fd;
		} else if (strcmp(job->tokenized[i], "|") == 0) {
			processTok[currCmdEnd++] = NULL;
			pipSegI++;
		} else {
			if (cmdStartInd[pipSegI] == NULL) {
				cmdStartInd[pipSegI] = processTok + currCmdEnd;
			}
			processTok[currCmdEnd++] = job->tokenized[i];
		}
	}
	processTok[currCmdEnd] = 0;
	for (i = pipSegI; i > 0; i--) {
		pipe(pipes[i]);
		pid_t __children = fork();
		if (__children == 0) {
			dup2(pipes[i][STDOUT_FILENO], STDOUT_FILENO);
			close(pipes[i][STDIN_FILENO]);
		} else {
			setFileDescriptor(descTable[i].fileDescriptor);
			dup2(pipes[i][STDIN_FILENO], STDIN_FILENO);
			close(pipes[i][STDOUT_FILENO]);
			// waitpid(__children, 0, 0);
			if (execvp(cmdStartInd[i][0], cmdStartInd[i]) <= -1) {
				fprintf(stderr, "janksh: %s: %s\n", strerror(errno), cmdStartInd[i][0]);
				exit(1);
			}
		}
	}
	if (execvp(cmdStartInd[i][0], cmdStartInd[i]) <= -1) {
		fprintf(stderr, "janksh: %s: %s\n", strerror(errno), cmdStartInd[i][0]);
		exit(1);
	}
}

void execJobs() {
	struct Job *var;
	TAILQ_FOREACH(var, joblist_head, traverser) {
		if (var->status == PENDING) {
			execJob2(var);
		}
	}
}

struct Job *moveJob(struct Job *var) {
	struct Job *newJob = malloc(sizeof(struct Job));
	memcpy(newJob, var, sizeof(struct Job));
	return newJob;
}

void printPrompt() {
	if (strcmp(CURR_PATH, getpwuid(getuid())->pw_dir) == 0)
		printf("[%s] jankersh > ", "~");
	else if (strcmp(CURR_PATH, "/") == 0)
		printf("[%s] jankersh > ", "/");
	else
		printf("[%s] jankersh > ", strrchr(CURR_PATH, '/') + 1);
}

void addJobs(char **tokenized, int ntok) {
	int i = 0;
	char **currJobCmd = tokenized;
	int prevTokS = 0;
	for (; i < ntok; i++) {
		if (strcmp("&", tokenized[i]) == 0) {
			char **newTokenized = malloc((i - prevTokS + 1) * sizeof(char *));
			char *cmd = cpyCmdTok(currJobCmd, i - prevTokS, newTokenized);
			newTokenized[i - prevTokS] = NULL;
			struct Job tempJobLocal = {
				.cmd = cmd, .isBackground = 1, .ntok = i - prevTokS, .tokenized = newTokenized, .status = PENDING};
			prevTokS = i + 1;
			currJobCmd = &tokenized[i + 1];
			struct Job *tempJob = moveJob(&tempJobLocal);
			TAILQ_INSERT_TAIL(joblist_head, tempJob, traverser);
		} else if (i == ntok - 1) {
			char **newTokenized = malloc((i - prevTokS + 2) * sizeof(char *));
			char *cmd = cpyCmdTok(currJobCmd, i - prevTokS + 1, newTokenized);
			newTokenized[i - prevTokS + 1] = NULL;
			// currFgJob.cmd = cmd;
			// currFgJob.isBackground = 0;
			// currFgJob.ntok = i - prevTokS + 1;
			// currFgJob.status = PENDING;
			// currFgJob.tokenized = newTokenized;

			struct Job tempJobLocal = {
				.cmd = cmd, .isBackground = 0, .ntok = i - prevTokS + 1, .tokenized = newTokenized, .status = PENDING};

			struct Job *tempJob = moveJob(&tempJobLocal);
			TAILQ_INSERT_TAIL(joblist_head, tempJob, traverser);
		}
	}
}

int main() {
	SHELL_INIT();
	if (getcwd(PREV_PATH, 4096) == NULL)
		perror("Get current directory failed");
	printf("Welcome to JankSh\n");
	char *command = malloc(sysconf(_SC_ARG_MAX) * sizeof(char));
	size_t _size = 0;
	char **tokenized = malloc(sysconf(_SC_ARG_MAX) * sizeof(char *));
	while (1) {
		if (getcwd(CURR_PATH, 4096) == NULL)
			perror("Get current directory failed");
		printPrompt();
		if (fgets(command, sysconf(_SC_ARG_MAX), stdin) == NULL) {
			printf("exit\n");
			exit(0);
		}
		int tokenCount = tokenize2(command, tokenized);
		tokenized[tokenCount] = 0;
		printTokenized(tokenized, tokenCount); // function will create buffer and it ought to be free after a fwhile.
		if (inBuiltCommands(tokenized, tokenCount) != 1) {
			cleanJobList();
		} else {
			addJobs(tokenized, tokenCount);
			execJobs();
		}
		cleanJobList();
        free(*tokenized);
	}
	free(command);
	free(tokenized);
	JOBLIST_DEINIT();
	return 0;
}