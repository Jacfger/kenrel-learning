#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int from;
module_param(from, int, S_IRUGO); // S_IRUGO means 0444, now 0644 means user can edit

static int to;
module_param(to, int, S_IRUGO);

static int result;
module_param(result, int, S_IRUGO);

static int __init init(void) 
{
    result = 0;
    if (to < from) 
        return 0;
    int i;
    for (i = from; i <= to; i++) {
        result += i;
    }
	printk(KERN_INFO "%d\n", result);
	return 0;
}

static void __exit exit(void)
{
	printk(KERN_INFO "Do nothing\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author's name");
MODULE_DESCRIPTION("Hello World module with parameters");

module_init(init);
module_exit(exit);
