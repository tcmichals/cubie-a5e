// SPDX-License-Identifier: GPL-2.0
/*
 * ret_override - test-only module: make one kernel function return an error,
 * for one task, so that a driver's error path for it can be run on the board.
 *
 * Used for the two probe checks of the A5E message box and remoteproc drivers
 * that neither fault injection nor a malformed device tree can reach: a reset
 * deassert that fails and a 32-bit DMA mask that is refused. A kretprobe on
 * the function replaces its return value with -errno when the caller is the
 * task whose PID was written to "pid" (the shell that then writes to the
 * driver's bind file). Needs CONFIG_KRETPROBES. Never load it outside a test.
 *
 *   insmod ret_override.ko func=reset_control_deassert errno=5
 *   echo $$ > /sys/module/ret_override/parameters/pid
 *   echo 3003000.mailbox > /sys/bus/platform/drivers/sun55i-msgbox/bind
 *   echo 0 > /sys/module/ret_override/parameters/pid
 *   cat /sys/module/ret_override/parameters/hits
 *   rmmod ret_override
 */

#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/sched.h>

static char *func = "";
module_param(func, charp, 0444);
MODULE_PARM_DESC(func, "function whose return value is replaced");

static int errno_val = 5;	/* EIO */
module_param_named(errno, errno_val, int, 0444);
MODULE_PARM_DESC(errno, "positive errno; the function returns -errno");

static int pid;
module_param(pid, int, 0644);
MODULE_PARM_DESC(pid, "task whose calls fail; 0 disarms");

static int hits;
module_param(hits, int, 0444);
MODULE_PARM_DESC(hits, "number of calls whose return value was replaced");

static int ret_override_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	int armed = READ_ONCE(pid);

	/* Nonzero: no return handler for this call */
	return !(armed && task_pid_nr(current) == armed);
}

static int ret_override_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	regs_set_return_value(regs, (unsigned long)(long)-errno_val);
	hits++;
	return 0;
}

static struct kretprobe ret_override_probe = {
	.entry_handler	= ret_override_entry,
	.handler	= ret_override_ret,
	.maxactive	= 4,
};

static int __init ret_override_init(void)
{
	int ret;

	if (!func[0] || errno_val <= 0 || errno_val >= MAX_ERRNO)
		return -EINVAL;

	ret_override_probe.kp.symbol_name = func;
	ret = register_kretprobe(&ret_override_probe);
	if (ret)
		pr_err("ret_override: cannot probe %s: %d\n", func, ret);
	else
		pr_info("ret_override: %s returns -%d for pid set in parameters/pid\n",
			func, errno_val);
	return ret;
}

static void __exit ret_override_exit(void)
{
	unregister_kretprobe(&ret_override_probe);
	pr_info("ret_override: %s released after %d hits\n", func, hits);
}

module_init(ret_override_init);
module_exit(ret_override_exit);
MODULE_DESCRIPTION("Test only: replace one function's return value for one task");
MODULE_LICENSE("GPL");
