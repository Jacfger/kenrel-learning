// Include necessary header files
#include "linux/types.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define __NR_xmerge 336

struct xmerge_params {
	char *outfile;
	char **infiles;
	unsigned int num_files;
	int oflags;
	mode_t mode;
	int *ofile_count;
};

int main(int argc, char *argv[]) {
	struct xmerge_params to_be_pass = {.oflags = O_WRONLY,
									   .mode = 0,
									   .num_files = 0,
									   .ofile_count = malloc(sizeof(int))};
	int fileCount = 0;
	int i;
	for (i = 0; i < argc; i++) {
		if (strcmp("-c", argv[i]) == 0) {
			to_be_pass.oflags |= O_CREAT;
		} else if (strcmp("-t", argv[i]) == 0) {
			to_be_pass.oflags |= O_TRUNC;
		} else if (strcmp("-e", argv[i]) == 0) {
			to_be_pass.oflags |= O_EXCL;
		} else if (strcmp("-a", argv[i]) == 0) {
			to_be_pass.oflags |= O_APPEND;
		} else if (strcmp("-m", argv[i]) == 0) {
			to_be_pass.mode = strtol(argv[i + 1], 0, 8);
			i++;
		} else {
			if (fileCount++ == 1) {
				to_be_pass.outfile = argv[i];
				to_be_pass.infiles = &argv[i + 1];
			} else if (fileCount > 1) {
				to_be_pass.num_files++;
			}
		}
	}
	if (to_be_pass.mode == 0) {
		to_be_pass.mode = S_IRUSR | S_IWUSR;
	}
	if (to_be_pass.oflags == O_WRONLY) {
		to_be_pass.oflags |= O_CREAT | O_APPEND;
	}

	*to_be_pass.ofile_count = 0;

	int len_copied = 0;
	if ((len_copied = syscall(__NR_xmerge, &to_be_pass, sizeof(struct xmerge_params))) < 0) {
		perror("xmerge");
	} else {
		printf("%d bytes merged\n", len_copied);
	}
	printf("%d file(s) merged\n", *to_be_pass.ofile_count);

    free(to_be_pass.ofile_count);
	return 0;
}
