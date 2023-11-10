#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>

#include <linux/uaccess.h>

static dev_t dev;
static struct class *cl;

struct myfirst_data {
	struct cdev cdev;
	char last;
};
static struct myfirst_data myfirst;

static int myfirst_open(struct inode *inode, struct file *filp)
{
	struct myfirst_data *myfirst;
	printk(KERN_INFO "myfirst: open()\n");

	myfirst = container_of(inode->i_cdev, struct myfirst_data, cdev);
	filp->private_data = myfirst;

	return 0;
}

static int myfirst_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "myfirst: release()\n");
	return 0;
}

static ssize_t myfirst_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct myfirst_data *myfirst;
	printk(KERN_INFO "myfirst: read()\n");

	myfirst = filp->private_data;
	if (*f_pos == 0) {
		if (copy_to_user(buf, &myfirst->last, 1)) {
			return -EFAULT;
		}
		else {
			++(*f_pos);
			return 1;
		}
	}
	else {
		return 0;
	}
}

static ssize_t myfirst_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct myfirst_data *myfirst;
	printk(KERN_INFO "myfirst: write()\n");

	myfirst = filp->private_data;
	if (copy_from_user(&myfirst->last, buf, 1)) {
		return -EFAULT;
	}
	else {
		return count;
	}
}

static const struct file_operations myfirst_fops = {
	.owner = THIS_MODULE,
	.open = myfirst_open,
	.release = myfirst_release,
	.read = myfirst_read,
	.write = myfirst_write
};

static int __init myfirst_init(void) 
{
	int ret;
	struct device *device;

	if ((ret = alloc_chrdev_region(&dev, 0, 1, "comp4511")) < 0) {
		return ret;
	}

	if (IS_ERR(cl = class_create(THIS_MODULE, "comp4511"))) {
		unregister_chrdev_region(dev, 1);
		return PTR_ERR(cl);
	}

	if (IS_ERR(device = device_create(cl, NULL, dev, NULL, "myfirst"))) {
		class_destroy(cl);
		unregister_chrdev_region(dev, 1);
		return PTR_ERR(device);
	}

	cdev_init(&myfirst.cdev, &myfirst_fops);
	if ((ret = cdev_add(&myfirst.cdev, dev, 1)) < 0) {
		device_destroy(cl, dev);
		class_destroy(cl);
		unregister_chrdev_region(dev, 1);
		return ret;
	}

	printk(KERN_INFO "myfirst: init()\n");

	return 0;
}

static void __exit myfirst_exit(void)
{
	cdev_del(&myfirst.cdev);
	device_destroy(cl, dev);
	class_destroy(cl);
	unregister_chrdev_region(dev, 1);

	printk(KERN_INFO "myfirst: exit()\n");
}

module_init(myfirst_init);
module_exit(myfirst_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author's name");
MODULE_DESCRIPTION("myfirst device driver");
