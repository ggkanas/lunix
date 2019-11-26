/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * Giorgos Gkanas
 * Panagiotis Aivasiliotis
 *
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>

#include "lunix.h"
#include "lunix-chrdev.h"
#include "lunix-lookup.h"

/*
 * Global data
 */
struct cdev lunix_chrdev_cdev;
int flag;

/*
 * Just a quick [unlocked] check to see if the cached
 * chrdev state needs to be updated from sensor measurements.
 */
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;

	WARN_ON ( !(sensor = state->sensor));

	/* ? */

	/* This might work */
    printk("Timestamps are %d and %d\n", state->buf_timestamp, sensor->msr_data[state->type]->last_update);
	return state->buf_timestamp < sensor->msr_data[state->type]->last_update;
}

/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	uint32_t value;
    long value2 = 0,valdiv, valmod;
	debug("entering\n");
    WARN_ON( !(sensor = state->sensor));
    printk("First print\n");
	/*
	 * Grab the raw data quickly, hold the
	 * spinlock for as little as possible.
	 */
     if (!spin_trylock(&sensor->lock)) {
         return -EAGAIN;
     }
     printk("Print 2\n");
     flag = 0;
     printk("Print 3\n");
	/* ? */

	/* Why use spinlocks? See LDD3, p. 119 */

	/*
	 * Any new data available?
	 */
    value = sensor->msr_data[state->type]->values[0];
    printk("Print 4\n");
	/* ? */
    spin_unlock(&sensor->lock);
    /* Wake sleeping processes up */
    flag = 1;
    printk("Print 5\n");
    wake_up_interruptible(&sensor->wq);
    printk("Print 6\n");

	/*
	 * Now we can take our time to format them,
	 * holding only the private state semaphore
	 */
    switch(state->type) {
         case BATT: value2 = lookup_voltage[value]; break;
         case TEMP: value2 = lookup_temperature[value]; break;
         case LIGHT: value2 = lookup_light[value]; break;
         default: break;
     }
     printk("Print 7\n");
     valdiv = value2 / 1000;
     valmod = value2 % 1000;
     printk("Print 8\n");
     snprintf(state->buf_data, 8, "%3ld.%3ld", valdiv, valmod);
     printk("Print 9\n");
     //if (buf_data[1] == '0') {buf_data[1] = sign; buf_data[0] = ' '; }
     /* 0s at the end? */
     state->buf_lim = 8;
     state->buf_timestamp = get_seconds();
     printk("Print 10\n");
     /* ? */

	debug("leaving\n");
	return 0;
}

/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/

static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	/* Declarations */
    unsigned int inodeminor = iminor(inode);
    enum lunix_msr_enum type = inodeminor % 8;
	/* Perhaps more ? */
	int ret, sensor_number = inodeminor / 8;
    struct lunix_chrdev_state_struct* state;

    printk("M: %u T: %d SN: %d", inodeminor, type, sensor_number);

	debug("entering\n");
	ret = -ENODEV;
	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto out;

    /* Allocate a new Lunix character device private state structure */
    filp->private_data = kzalloc(sizeof(struct lunix_chrdev_state_struct), GFP_KERNEL);
    /* More? */

    /*
	 * Associate this open file with the relevant sensor based on
	 * the minor number of the device node [/dev/sensor<NO>-<TYPE>]
	 */
    state = filp->private_data;
    state->type = type;
    state->sensor = &lunix_sensors[sensor_number];
    printk("Sensor ptr is %llu\n", state->sensor);
    state->buf_timestamp = 0;
    sema_init(&state->lock, 1);
    ret = 0;

out:
	debug("leaving, with ret = %d\n", ret);
	return ret;
}

static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
    kfree(filp->private_data);
	/* More? */
	return 0;
}

static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* Why? */
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
	ssize_t ret, copy_success = 0;

	struct lunix_sensor_struct *sensor;
	struct lunix_chrdev_state_struct *state;
    loff_t basepos = *f_pos;
    char data[cnt];

    debug("entering\n");

	state = filp->private_data;
	WARN_ON(!state);


	sensor = state->sensor;
	WARN_ON(!sensor);
    printk("Sensor in read is %llu\n", sensor);
	/* Lock? */
    if ((ret = down_interruptible(&state->lock)) < 0) return ret;
	/*
	 * If the cached character device state needs to be
	 * updated by actual sensor data (i.e. we need to report
	 * on a "fresh" measurement, do so
	 */
    if (lunix_chrdev_state_needs_refresh(state) == 0) {
        ret = 0;
        goto out;
    }
	if (*f_pos == 0) {
		while (lunix_chrdev_state_update(state) == -EAGAIN) {
            printk("Entered here\n");
			/* ? */
			/* The process needs to sleep */
			/* See LDD3, page 153 for a hint */
            ret = wait_event_interruptible(sensor->wq, flag == 1);
		}
	}
    if (ret < 0) goto out;

	/* End of file */
	/* Determine the number of cached bytes to copy to userspace */
    ret = 0;
    for (; *f_pos < basepos + cnt && *f_pos < state->buf_lim; ++(*f_pos)) data[ret++] = state->buf_data[*f_pos];
    if (ret != 0) copy_success = copy_to_user(usrbuf, data, ret);
    if (copy_success < 0) ret = copy_success;
	/* Auto-rewind on EOF mode? */
    if (*f_pos == state->buf_lim) *f_pos = 0;
out:
	/* Unlock? */
    up(&state->lock);
    debug("leaving with ret = %ld\n", ret);
	return ret;
}

static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static struct file_operations lunix_chrdev_fops =
{
    .owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap
};

int lunix_chrdev_init(void)
{
	/*
	 * Register the character device with the kernel, asking for
	 * a range of minor numbers (number of sensors * 8 measurements / sensor)
	 * beginning with LINUX_CHRDEV_MAJOR:0
	 */
	int ret;
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

	debug("initializing character device\n");
	cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops);
	lunix_chrdev_cdev.owner = THIS_MODULE;

	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	/* ? */
	/* register_chrdev_region? */
    ret = register_chrdev_region(dev_no, lunix_minor_cnt, "Lunix:TNG");
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}
	/* ? */
	/* cdev_add? */
    ret = cdev_add(&lunix_chrdev_cdev, dev_no, lunix_minor_cnt);
	if (ret < 0) {
		debug("failed to add character device\n");
		goto out_with_chrdev_region;
	}
	debug("completed successfully\n");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
out:
	return ret;
}

void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

	debug("entering\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev);
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving\n");
}
