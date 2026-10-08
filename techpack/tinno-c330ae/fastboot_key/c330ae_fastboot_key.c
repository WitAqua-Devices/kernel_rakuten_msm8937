// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reboot into the bootloader (fastboot) when volume-down is held during the
 * first seconds of the kernel boot, or when the bootloader started us in
 * FFBM mode.
 *
 * The Rakuten Mini (Tinno C330AE) bootloader maps volume-up to recovery and
 * volume-down to FFBM (FastMMI). FFBM is useless on this kernel, the
 * bootloader cannot be modified, and on this device it does not even pass
 * androidboot.mode=ffbm-xx to the kernel: it just falls through to a normal
 * boot. So the kernel provides its own key path into fastboot that depends
 * only on boot.img, not on recovery or on the Android userspace.
 *
 * Volume-down (PMIC RESIN) is read straight from PON_RT_STS, so a key that
 * was already held before the PON driver probed is seen too. It is polled
 * for a short window after late init so the user only has to hold the key
 * once the boot logo is up; the poll runs from a workqueue and does not
 * delay the boot.
 */

#define pr_fmt(fmt) "c330ae_fastboot_key: " fmt

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/input/qpnp-power-on.h>
#include <dt-bindings/input/qcom,qpnp-power-on.h>
#include <asm/system_misc.h>

/* How long after late init volume-down is still watched (ms). 0 disables. */
static unsigned int window_ms = 8000;
module_param(window_ms, uint, 0444);
MODULE_PARM_DESC(window_ms, "Watch volume-down for this many ms after late init");

/* Poll interval and how many consecutive polls must see the key down. */
static unsigned int poll_ms = 100;
module_param(poll_ms, uint, 0444);
static unsigned int hold_polls = 3;
module_param(hold_polls, uint, 0444);

static struct delayed_work poll_work;
static unsigned long deadline;
static unsigned int pressed_polls;

/* True when the bootloader passed androidboot.mode=ffbm-xx */
static bool __init ffbm_in_cmdline(void)
{
	static const char key[] = "androidboot.mode=";
	const char *p = saved_command_line;

	if (!p)
		return false;

	while ((p = strstr(p, key)) != NULL) {
		bool at_token_start = (p == saved_command_line || p[-1] == ' ');

		p += sizeof(key) - 1;
		if (at_token_start)
			return strncmp(p, "ffbm", 4) == 0;
	}

	return false;
}

/*
 * Only reboot when the restart reason can actually be recorded. A reboot
 * without a reason just boots normally, which with the key still held would
 * be a boot loop.
 */
static bool reboot_path_ready(void)
{
	return qpnp_pon_check_hard_reset_stored() && arm_pm_restart;
}

static void reboot_to_bootloader(const char *why)
{
	if (!reboot_path_ready()) {
		pr_err("%s, but reboot path not ready; booting normally\n", why);
		return;
	}

	pr_emerg("%s, rebooting into bootloader\n", why);
	kernel_restart("bootloader");
}

static void fastboot_key_poll(struct work_struct *work)
{
	int resin = qpnp_pon_key_is_pressed(PON_POWER_ON_TYPE_RESIN);

	if (resin < 0) {
		pr_err("cannot read volume-down state (%d); giving up\n", resin);
		return;
	}

	if (resin) {
		if (++pressed_polls >= hold_polls) {
			reboot_to_bootloader("volume-down held");
			return;
		}
	} else {
		pressed_polls = 0;
	}

	if (time_before(jiffies, deadline))
		schedule_delayed_work(&poll_work, msecs_to_jiffies(poll_ms));
}

static int __init c330ae_fastboot_key_init(void)
{
	if (ffbm_in_cmdline()) {
		reboot_to_bootloader("FFBM boot mode");
		return 0;
	}

	if (!window_ms)
		return 0;

	if (!hold_polls)
		hold_polls = 1;

	INIT_DELAYED_WORK(&poll_work, fastboot_key_poll);
	deadline = jiffies + msecs_to_jiffies(window_ms);
	schedule_delayed_work(&poll_work, 0);

	return 0;
}
late_initcall_sync(c330ae_fastboot_key_init);
