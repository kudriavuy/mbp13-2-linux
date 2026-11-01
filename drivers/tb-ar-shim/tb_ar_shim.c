// SPDX-License-Identifier: GPL-2.0
/*
 * tb_ar_shim: suspend/resume fix for the Thunderbolt 3 controller (Intel
 * Alpine Ridge, DSL6540/JHL6540) on Apple MacBookPro13,x and 14,x.
 *
 * By Andrew Clunis (https://github.com/orospakr/linux-apple-alpine-ridge-suspend,
 * v1.0.0 module). Changed for omarchy-macbookpro-a1706: every on-board
 * controller gets the fix, not only the first (the four-port MacBookPro13,2,
 * 13,3, 14,2 and 14,3 have two), also one re-added by a rescan; a controller
 * that does not come back from a sleep is marked gone at once instead of
 * blocking the resume. Validated on a MacBookPro13,2 in S3.
 *
 * [FOR-UPSTREAM] Out-of-tree carry of two fixes headed for
 * drivers/pci/quirks.c (see upstream/ in https://github.com/orospakr/linux-apple-alpine-ridge-suspend and
 * https://github.com/omacom/omarchy/issues/10593). Installed as a
 * dev_pm_domain on the controller's upstream bridge because that is the only
 * out-of-tree way to run at the right phases; in-tree these are two fixups.
 *
 * Bug 1 (deep/S3): device suspend_noirq runs children first, so the
 * Thunderbolt NHI's tb_lc_set_sleep() drops the whole controller off the bus
 * before the PCI core saves the bridges' config space — it saves all-ones
 * and restores garbage on wake (75-107 s resume, dead NHI, xHCI removed).
 * Fix: save the bridge's and its subtree's config space in suspend_late,
 * before any suspend_noirq callback runs. pci_pm_suspend_noirq() honours
 * state_saved and skips its own save.
 *
 * Bug 2 (s2idle): pci_pm_resume_noirq() skips config restore entirely when
 * skip_bus_pm && pm_suspend_no_platform() (true on this firmware), assuming
 * devices kept their config space — but tb_lc_set_sleep() really cleared it.
 * The NHI then latches going_away (-ESHUTDOWN until reload) and pciehp
 * hot-removes the xHCI. Fix: restore the saved state ourselves in
 * resume_noirq, parents before children. pci_restore_state() consumes
 * state_saved, so under S3 (where the core does restore) this is a no-op.
 *
 * Validated on MacBookPro14,1: 28/28 suspend cycles across both sleep modes,
 * resume 1.3-1.4 s (baseline 75-107 s with dead Thunderbolt).
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/pm_domain.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/suspend.h>

/* resume_noirq: max wait for the upstream bridge to answer config reads.
 * Measured: immediate on s2idle (rails never drop), <1 s on S3 (platform
 * re-powers the chip before the first noirq callback); on a MacBookPro13,2
 * it answers at once or not at all. */
#define BRIDGE_WAIT_MS	1000
/* resume_noirq: shared budget for the subtree restore walk. Measured need is
 * ~1 ms; the budget has never been consumed in validation. */
#define SUBTREE_WAIT_MS	200

#define MAX_BRIDGES	4
static struct pci_dev *upsb[MAX_BRIDGES];
static bool cut[MAX_BRIDGES];		/* power cut by us for this sleep */
static DEFINE_MUTEX(upsb_lock);

static int slot(struct pci_dev *pdev)
{
	for (int i = 0; i < MAX_BRIDGES; i++)
		if (upsb[i] == pdev)
			return i;
	return -1;
}
static const struct dev_pm_ops *bus_pm;
static struct dev_pm_domain shim_pd;

/*
 * The chip did not come back from the sleep (it does not answer at all after
 * a sleep of a few seconds; only minutes without power revive it). Mark the
 * bridge and everything below it disconnected, as the PCI core does for a
 * surprise removal: the core and the drivers then fail at once instead of
 * waiting for each device (pci_dev_wait(), up to a minute per device, with
 * the whole resume blocked). Removing the tree and rescanning it after a
 * power cycle brings back fresh devices.
 */
static int mark_gone(struct pci_dev *dev, void *data)
{
	WRITE_ONCE(dev->error_state, pci_channel_io_perm_failure);
	return 0;
}

static int save_cb(struct pci_dev *dev, void *data)
{
	int *n = data;
	u32 id;

	pci_read_config_dword(dev, PCI_VENDOR_ID, &id);
	if (id == 0xffffffff) {
		pci_warn(dev, "tb_ar_shim: not readable at suspend_late, not saving\n");
		return 0;
	}
	if (dev->class == PCI_CLASS_SERIAL_USB_XHCI) {
		/*
		 * The controller's xHCI is usually runtime-suspended (D3hot,
		 * state already saved) by now — leave it to the core. If it
		 * is still in D0, save it and let it reach D3hot with wake
		 * armed while its config space is still readable.
		 */
		if (dev->current_state != PCI_D0)
			return 0;
		pci_save_state(dev);
		(*n)++;
		pci_prepare_to_sleep(dev);
		return 0;
	}
	pci_save_state(dev);
	(*n)++;
	return 0;
}

/* Save the configuration of the bridge and its subtree while the chip still answers. */
static int save_tree(struct device *dev, int (*bus_cb)(struct device *), const char *phase)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret, n = 0;

	ret = bus_cb ? bus_cb(dev) : 0;
	if (ret)
		return ret;
	save_cb(pdev, &n);
	if (pdev->subordinate)
		pci_walk_bus(pdev->subordinate, save_cb, &n);
	pci_info(pdev, "tb_ar_shim: %s: saved config of %d devices\n", phase, n);
	return 0;
}

static int shim_suspend_late(struct device *dev)
{
	return save_tree(dev, bus_pm->suspend_late, "suspend_late");
}

static int shim_poweroff_late(struct device *dev)
{
	return save_tree(dev, bus_pm->poweroff_late, "poweroff_late");
}

/*
 * Power, the way macOS (and mainline Linux for the older Cactus Ridge Macs,
 * quirk_apple_poweroff_thunderbolt) handles it: before the firmware takes the
 * power (S3, or switching off for hibernation), once everything below the
 * bridge has suspended, cut the chip's power with Apple's method
 * NHI0.SXFP(0) (the USB side, then the Thunderbolt side). Left to itself the
 * firmware cuts it late in S3 entry, and a wake within a few seconds of that
 * left the chip unpowered and stuck: it then answered nothing until it had
 * been without power for about a minute.
 */
static int cut_power(struct device *dev, int (*bus_cb)(struct device *), const char *why)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	acpi_handle sxfp;
	int ret, i;

	ret = bus_cb ? bus_cb(dev) : 0;
	if (ret)
		return ret;
	i = slot(pdev);
	if (i < 0 || ACPI_FAILURE(acpi_get_handle(ACPI_HANDLE(dev), "DSB0.NHI0.SXFP", &sxfp)))
		return 0;
	if (ACPI_SUCCESS(acpi_execute_simple_method(sxfp, NULL, 0))) {
		cut[i] = true;
		pci_info(pdev, "tb_ar_shim: power cut for %s (SXFP)\n", why);
	}
	return 0;
}

static int shim_suspend_noirq(struct device *dev)
{
	/* s2idle keeps the power: only a firmware sleep takes it. */
	if (!pm_suspend_via_firmware())
		return bus_pm->suspend_noirq ? bus_pm->suspend_noirq(dev) : 0;
	return cut_power(dev, bus_pm->suspend_noirq, "the sleep");
}

static int shim_poweroff_noirq(struct device *dev)
{
	return cut_power(dev, bus_pm->poweroff_noirq, "hibernation");
}

static bool answers(struct pci_dev *pdev, int ms)
{
	u32 id;

	for (;;) {
		pci_read_config_dword(pdev, PCI_VENDOR_ID, &id);
		if (id != 0xffffffff && id != 0)
			return true;
		if (ms <= 0)
			return false;
		msleep(20);
		ms -= 20;
	}
}

/*
 * Power on with the root port's UGIO, the part of Apple's _PS0 that releases
 * the power GPIOs (the firmware has usually done that already; UGIO only
 * acts on a GPIO still holding the power off). The link was never disabled,
 * so it trains by itself. The whole _PS0 (link retrain and long waits, ~8.5 s)
 * only if the chip does not answer after that. After a suspend this runs only
 * when the power was cut for it; after a hibernation always (the flag set at
 * power-off is not in the image).
 */
static void power_up(struct pci_dev *pdev, bool always)
{
	struct pci_dev *rp = pci_upstream_bridge(pdev);
	ktime_t t0 = ktime_get();
	acpi_handle ugio;
	acpi_status st;
	int i = slot(pdev);

	if (i < 0 || (!cut[i] && !always))
		return;
	cut[i] = false;
	if (rp && ACPI_HANDLE(&rp->dev) &&
	    ACPI_SUCCESS(acpi_get_handle(ACPI_HANDLE(&rp->dev), "UGIO", &ugio)) &&
	    ACPI_SUCCESS(acpi_evaluate_object(ugio, NULL, NULL, NULL)) &&
	    answers(pdev, 1000)) {
		pci_info(pdev, "tb_ar_shim: power restored (UGIO) in %lld ms\n",
			 ktime_ms_delta(ktime_get(), t0));
		return;
	}
	st = acpi_evaluate_object(ACPI_HANDLE(&pdev->dev), "_PS0", NULL, NULL);
	pci_info(pdev, "tb_ar_shim: power restored (UGIO not enough, _PS0 %s) in %lld ms\n",
		 ACPI_SUCCESS(st) ? "ok" : "failed", ktime_ms_delta(ktime_get(), t0));
}

/*
 * Poll one subtree device until it answers config reads, spending from a
 * shared time budget, then restore its saved config. pci_walk_bus is
 * pre-order and the caller restored the bridge itself first, so each
 * device's path is programmed before we reach it.
 */
static int restore_cb(struct pci_dev *dev, void *data)
{
	int *budget = data;
	u32 id = 0xffffffff;

	for (;;) {
		pci_read_config_dword(dev, PCI_VENDOR_ID, &id);
		if (id != 0xffffffff && id != 0)
			break;
		if (*budget <= 0) {
			pci_warn(dev, "tb_ar_shim: not readable after the wake, budget spent\n");
			return 0;
		}
		msleep(10);
		*budget -= 10;
	}
	if (dev->state_saved)
		pci_restore_state(dev);
	return 0;
}

static int wake_tree(struct device *dev, int (*bus_cb)(struct device *), bool always_power,
		     const char *phase)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	ktime_t t0 = ktime_get();
	int budget = SUBTREE_WAIT_MS;
	int ret;

	power_up(pdev, always_power);
	if (!answers(pdev, BRIDGE_WAIT_MS)) {
		pci_err(pdev, "tb_ar_shim: bridge not readable %d ms after wake: Thunderbolt marked gone\n",
			BRIDGE_WAIT_MS);
		mark_gone(pdev, NULL);
		if (pdev->subordinate)
			pci_walk_bus(pdev->subordinate, mark_gone, NULL);
		return bus_cb ? bus_cb(dev) : 0;
	}
	ret = bus_cb ? bus_cb(dev) : 0;
	if (ret)
		return ret;
	if (pdev->state_saved)
		pci_restore_state(pdev);
	if (pdev->subordinate)
		pci_walk_bus(pdev->subordinate, restore_cb, &budget);
	pci_info(pdev, "tb_ar_shim: %s done in %lld us (subtree budget left %d ms)\n",
		 phase, ktime_us_delta(ktime_get(), t0), budget);
	return ret;
}

static int shim_resume_noirq(struct device *dev)
{
	return wake_tree(dev, bus_pm->resume_noirq, false, "resume_noirq");
}

static int shim_restore_noirq(struct device *dev)
{
	return wake_tree(dev, bus_pm->restore_noirq, true, "restore_noirq");
}

/*
 * The on-board Alpine Ridge upstream bridges. External devices can carry the
 * same 8086:1578, but only the on-board ones have Apple's ACPI power methods
 * under them (same trick as mainline's quirk_apple_poweroff_thunderbolt).
 */
static bool onboard_upsb(struct pci_dev *pdev)
{
	acpi_handle bridge = ACPI_HANDLE(&pdev->dev), sxfp;

	return pdev->vendor == PCI_VENDOR_ID_INTEL && pdev->device == 0x1578 &&
	       bridge && ACPI_SUCCESS(acpi_get_handle(bridge, "DSB0.NHI0.SXFP", &sxfp));
}

/* Track a bridge; with bound, re-attach its driver around the domain change. */
static void attach(struct pci_dev *pdev, bool bound)
{
	int i;

	mutex_lock(&upsb_lock);
	for (i = 0; i < MAX_BRIDGES && upsb[i]; i++)
		if (upsb[i] == pdev)
			goto out;
	if (i == MAX_BRIDGES) {
		pci_warn(pdev, "tb_ar_shim: too many controllers, not handled\n");
		goto out;
	}
	upsb[i] = pci_dev_get(pdev);
	/* dev_pm_domain_set() wants an unbound device: drop pcieport, set, re-attach. */
	if (bound)
		device_release_driver(&pdev->dev);
	dev_pm_domain_set(&pdev->dev, &shim_pd);
	if (bound && device_attach(&pdev->dev) < 0)
		pci_warn(pdev, "tb_ar_shim: device_attach failed\n");
	pci_info(pdev, "tb_ar_shim: installed\n");
out:
	mutex_unlock(&upsb_lock);
}

static void forget(struct pci_dev *pdev)
{
	mutex_lock(&upsb_lock);
	for (int i = 0; i < MAX_BRIDGES; i++)
		if (upsb[i] == pdev) {
			pci_dev_put(pdev);
			upsb[i] = NULL;
			/* keep the array packed */
			for (; i + 1 < MAX_BRIDGES; i++) {
				upsb[i] = upsb[i + 1];
				cut[i] = cut[i + 1];
			}
			upsb[MAX_BRIDGES - 1] = NULL;
			cut[MAX_BRIDGES - 1] = false;
			break;
		}
	mutex_unlock(&upsb_lock);
}

/*
 * A controller removed and rescanned (to revive it after a failed wake) comes
 * back as a new device: give it the domain before a driver binds to it.
 */
static int pci_event(struct notifier_block *nb, unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);

	if (action == BUS_NOTIFY_ADD_DEVICE && onboard_upsb(pdev))
		attach(pdev, false);
	else if (action == BUS_NOTIFY_REMOVED_DEVICE)
		forget(pdev);
	return NOTIFY_DONE;
}

static struct notifier_block pci_nb = { .notifier_call = pci_event };

static int __init shim_init(void)
{
	struct pci_dev *pdev = NULL;
	bool found = false;

	bus_pm = pci_bus_type.pm;
	shim_pd.ops = *bus_pm;
	shim_pd.ops.suspend_late = shim_suspend_late;
	shim_pd.ops.suspend_noirq = shim_suspend_noirq;
	shim_pd.ops.resume_noirq = shim_resume_noirq;
	shim_pd.ops.poweroff_late = shim_poweroff_late;
	shim_pd.ops.poweroff_noirq = shim_poweroff_noirq;
	shim_pd.ops.restore_noirq = shim_restore_noirq;

	/* Every on-board controller: the four-port models have two. */
	while ((pdev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x1578, pdev)))
		if (onboard_upsb(pdev))
			found = true;
	if (!found)
		return -ENODEV;
	bus_register_notifier(&pci_bus_type, &pci_nb);
	while ((pdev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x1578, pdev)))
		if (onboard_upsb(pdev))
			attach(pdev, true);
	return 0;
}

static void __exit shim_exit(void)
{
	bus_unregister_notifier(&pci_bus_type, &pci_nb);
	mutex_lock(&upsb_lock);
	for (int i = 0; i < MAX_BRIDGES && upsb[i]; i++) {
		device_release_driver(&upsb[i]->dev);
		dev_pm_domain_set(&upsb[i]->dev, NULL);
		if (device_attach(&upsb[i]->dev) < 0)
			pci_warn(upsb[i], "tb_ar_shim: device_attach failed\n");
		pci_info(upsb[i], "tb_ar_shim: removed\n");
		pci_dev_put(upsb[i]);
		upsb[i] = NULL;
	}
	mutex_unlock(&upsb_lock);
}

module_init(shim_init);
module_exit(shim_exit);
/* Autoload via udev coldplug when the bridge is present; init still verifies
 * the Apple ACPI signature, so a non-Apple 1578 just gets -ENODEV. */
MODULE_ALIAS("pci:v00008086d00001578sv*sd*bc*sc*i*");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0-a1706.5");
MODULE_DESCRIPTION("Apple Alpine Ridge Thunderbolt suspend/resume fix (MacBookPro13,x/14,x)");
