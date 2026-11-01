// SPDX-License-Identifier: GPL-2.0
/*
 * Apple T1 (iBridge) host power-state notifications
 *
 * On MacBookPro13,x / 14,x the T1 has an ACPI node APP7777 (RHUB.ASOC).
 * Its SOCW method passes the host power state to the T1 through the EC
 * mailbox: SOCW(0) before sleep, SOCW(1) after wakeup. macOS and the
 * original T1 driver (apple-ibridge.c, macbook12-spi-driver) call it.
 *
 * Without SOCW(1) the T1 on a MacBookPro13,2 often drops off USB after a
 * system sleep longer than ~40 s and re-enumerates with the Touch Bar
 * display and digitizer dead (camera still fine); re-sending the mode or
 * re-authorizing the USB device does not help, only a reboot does. With it
 * the T1 just gets a USB reset on resume, also after 30+ minute sleeps.
 *
 * SOCW(0) on suspend makes the MacBookPro13,2 wake up again at once, so it
 * is only sent with suspend_notify=1; by default the driver just tells the
 * T1 the host is up, at probe and after every system resume.
 *
 * Only SOCW is ever evaluated. The node also has DFU and T1 reset methods,
 * which this driver deliberately never touches.
 */

#include <linux/acpi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static bool suspend_notify;
module_param(suspend_notify, bool, 0644);
MODULE_PARM_DESC(suspend_notify, "Send SOCW(0) before system sleep (wakes the 13,2 up immediately)");

static int apple_ibridge_socw(struct device *dev, u64 state)
{
	union acpi_object arg = { .integer = { .type = ACPI_TYPE_INTEGER, .value = state } };
	struct acpi_object_list args = { .count = 1, .pointer = &arg };
	unsigned long long ret;
	acpi_status status;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "SOCW", &args, &ret);
	if (ACPI_FAILURE(status)) {
		dev_warn(dev, "SOCW(%llu) failed: %s\n", state, acpi_format_exception(status));
		return -EIO;
	}
	if (ret)
		dev_warn(dev, "SOCW(%llu) returned %llu\n", state, ret);
	else
		dev_dbg(dev, "SOCW(%llu) done\n", state);

	return 0;
}

static int apple_ibridge_pm_probe(struct platform_device *pdev)
{
	if (!acpi_has_method(ACPI_HANDLE(&pdev->dev), "SOCW"))
		return -ENODEV;

	/* make sure the T1 knows the host is up (as after an EFI boot) */
	apple_ibridge_socw(&pdev->dev, 1);

	return 0;
}

/*
 * The APP7777 platform device is registered long before the USB devices,
 * so these run after the Touch Bar has been switched off on suspend and
 * before USB resumes.
 */
static int apple_ibridge_pm_suspend(struct device *dev)
{
	if (suspend_notify)
		apple_ibridge_socw(dev, 0);
	return 0;
}

static int apple_ibridge_pm_resume(struct device *dev)
{
	apple_ibridge_socw(dev, 1);
	return 0;
}

static const struct dev_pm_ops apple_ibridge_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(apple_ibridge_pm_suspend, apple_ibridge_pm_resume)
};

static const struct acpi_device_id apple_ibridge_pm_acpi_match[] = {
	{ "APP7777" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, apple_ibridge_pm_acpi_match);

static struct platform_driver apple_ibridge_pm_driver = {
	.driver = {
		.name = "apple-ibridge-pm",
		.acpi_match_table = apple_ibridge_pm_acpi_match,
		.pm = pm_sleep_ptr(&apple_ibridge_pm_ops),
	},
	.probe = apple_ibridge_pm_probe,
};
module_platform_driver(apple_ibridge_pm_driver);

MODULE_DESCRIPTION("Apple T1 (iBridge) host power-state notifications");
MODULE_LICENSE("GPL");
