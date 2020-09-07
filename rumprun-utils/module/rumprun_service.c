/*
 * LibrettOS Network Server Utility Kernel Module
 *
 * Copyright (c) 2019 Mincheol Sung <mincheol@vt.edu>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <asm/xen/hypercall.h>
#include <xen/events.h>

#include "_rumprun.h"
#include "../_service.h"

#ifndef __HYPERVISOR_rumprun_service_op
# define __HYPERVISOR_rumprun_service_op   46
#endif

struct arg
{
	unsigned int remote_domain;
	unsigned int remote_port;
};

struct evtchn
{
	int domid;
	int port;
};

static struct evtchn evtchn_table[RUMPRUN_NUM_OF_APPS];
static inline void init_evtchn_table(void)
{
	memset(evtchn_table, 0xFF, sizeof(evtchn_table));
}

static int set_evtchn(int domid, int port)
{
	unsigned i;
	for (i = 0; i < RUMPRUN_NUM_OF_APPS; i++) {
		if (evtchn_table[i].domid == -1) {
			evtchn_table[i].domid = domid;
			evtchn_table[i].port = port;
			return 0;
		}
	}

	return -1;
}

static int get_evtchn(int domid)
{
	unsigned i;
	for (i = 0; i < RUMPRUN_NUM_OF_APPS; i++) {
		if (evtchn_table[i].domid == domid)
			return evtchn_table[i].port;
	}

	return -1;
}

static inline int
do_rumprun_service_op(int op, int sysid, void *ptr)
{
	return _hypercall3(int, rumprun_service_op, op, sysid, ptr);
}

static long rumprun_service_ioctl(struct file *filep, unsigned int cmd, unsigned long user_args)
{
	int rc;
	int local_port;
	struct arg args;
	switch (cmd) {
		case RUMPRUN_SERVICE_IOCTL_CLEANUP:
			rc = do_rumprun_service_op(RUMPRUN_SERVICE_CLEANUP, 0, 0);
			init_evtchn_table();
			break;
		case RUMPRUN_SERVICE_IOCTL_BIND:
			if (copy_from_user(&args, (void *)user_args, sizeof(struct arg)))
				return -EFAULT;

			rc = bind_interdomain_evtchn_to_irq(args.remote_domain, args.remote_port);

			if (set_evtchn(args.remote_domain, rc))
			{
				printk("set_evtchn fails, domain: %d, remote port: %d, local port: %d\n", args.remote_domain, args.remote_port, rc);
			}
			printk("bind domain: %u, remote port: %u, local port: %d\n",args.remote_domain, args.remote_port, rc);

			break;

		case RUMPRUN_SERVICE_IOCTL_SWITCH:
			if (copy_from_user(&args, (void *)user_args, sizeof(struct arg)))
				return -EFAULT;

			local_port = get_evtchn(args.remote_domain);
			if (local_port < 0)
			{
				printk("get_evtchn fails, domain: %d, local port: %d\n", args.remote_domain, local_port);
				rc = -1;
				break;
			}
			notify_remote_via_irq(local_port);
			printk("send event to domain %u, local port: %d\n", args.remote_port, local_port);
			rc = 0;
			break;

		default:
			rc = -EINVAL;
			break;
	}
	return rc;
}

static struct file_operations rumprun_service_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rumprun_service_ioctl
};

static struct miscdevice rumprun_service_device = {
	.minor = 242,
	.name = "rumprun_service",
	.fops = &rumprun_service_ops
};

static int __init init_rumprun_service(void)
{
	int err;
	printk(KERN_INFO "Init module\n");
	err = misc_register(&rumprun_service_device);
	if (err) {
		printk(KERN_ERR "Register rumprun_service_device fails, err: %d\n", err);
		misc_deregister(&rumprun_service_device);
	}

	init_evtchn_table();

	return 0;
}

static void __exit cleanup_rumprun_service(void)
{
	printk(KERN_INFO "Cleaning up module.\n");
	misc_deregister(&rumprun_service_device);
}

module_init(init_rumprun_service);
module_exit(cleanup_rumprun_service);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Mincheol Sung <mincheol@vt.edu>");
MODULE_DESCRIPTION("Utility Operations for LibrettOS Network Server Frontend Infomation in Xen");
