/* comp4511/xmerge.c */

// Include necessary header files

#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/syscalls.h"
#include "linux/types.h"

struct xmerge_params {
	char __user *outfile;
	char __user **infiles;
	unsigned int num_files;
	int oflags;
	mode_t mode;
	int __user *ofile_count;
};


#define _debugf(line, ...) printk(KERN_INFO "DEBUG " #line ": " __VA_ARGS__)
// #define debugf(...) _debugf(__LINE__, __VA_ARGS__);

// You should use SYSCALL_DEFINEn macro

SYSCALL_DEFINE2(xmerge, unsigned long __user, args, unsigned long, argslen) // Fill in the arguments
{
	int i;
	ssize_t acculen = 0, nfiles = 0;
	struct xmerge_params better_than_nothing;
	long outputFile = 0, inFile = 0;
	long __sys_ret = 0;
	char _buf[1024];
	char __debug_buf[1024];

	long __ret = 0;
	mm_segment_t old_fs;

	printk(KERN_INFO "Copying from user space\n");
	if (copy_from_user(&better_than_nothing, (void *)args, (size_t)argslen)) {
		return -EFAULT;
	}

	if (__sys_ret = strncpy_from_user(__debug_buf, better_than_nothing.outfile, 255) > 0) {
		printk(KERN_INFO "Opening File, filename %s\n", __debug_buf);
	} else {
		printk(KERN_INFO "Failed to convert string", __debug_buf);
	}
	printk(KERN_INFO "Openinng Files, flags: %d\n", better_than_nothing.oflags);
	if ((outputFile = ksys_open(better_than_nothing.outfile, better_than_nothing.oflags, better_than_nothing.mode)) < 0) {
		__ret = outputFile;
		goto __error_return;
	}

	for (i = 0; i < better_than_nothing.num_files; i++) {
		char __user *file_name;
		printk(KERN_INFO "Copying file names\n");
		if ((__sys_ret = get_user(file_name, better_than_nothing.infiles + i))) {
			__ret = __sys_ret;
			goto __error_return;
		}
		if (__sys_ret = strncpy_from_user(__debug_buf, file_name, 255) > 0) {
			printk(KERN_INFO "Opening File, filename %s\n", __debug_buf);
		}
		printk(KERN_INFO "Sys opening input files\n");
		inFile = ksys_open(file_name, O_RDONLY, 0);
		if (inFile < 0) {
			__ret = inFile;
			goto __error_return;
		} else {
			nfiles++;
		}
		printk(KERN_INFO "Setting address space");
		old_fs = get_fs();
		set_fs(get_ds());
		printk(KERN_INFO "Sys read file\n");
		while (__sys_ret = ksys_read(inFile, _buf, 1024)) {
			if (__sys_ret < 0) {
				__ret = __sys_ret;
				set_fs(old_fs);
				goto __error_return;
			}
			printk(KERN_INFO "Sys write %d bytes\n", __sys_ret);
			if ((__sys_ret = ksys_write(outputFile, _buf, __sys_ret)) < 0) {
				__ret = __sys_ret;
				set_fs(old_fs);
				goto __error_return;
			}
			acculen += __sys_ret;
		}
		set_fs(old_fs);
		printk(KERN_INFO "Sys close file\n");
		if ((__sys_ret = ksys_close(inFile)) < 0) {
			__ret = __sys_ret;
			goto __error_return;
		}
	}
	__ret = acculen;
	inFile = 0;

__error_return:
	printk(KERN_INFO "Copying int to user space\n", __sys_ret);
	if (copy_to_user(better_than_nothing.ofile_count, &nfiles, sizeof(int))) {
		__ret = -EFAULT;
	}
	printk(KERN_INFO "All process finishing, deallocating resources\n");
	if (inFile > 0) {
		if ((__sys_ret = ksys_close(inFile)) < 0) {
			__ret = __sys_ret;
		}
	}
	if (outputFile > 0) {
		if ((__sys_ret = ksys_close(outputFile)) < 0) {
			__ret = __sys_ret;
		}
	}
	return __ret;
}
