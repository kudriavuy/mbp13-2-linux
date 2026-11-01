// SPDX-License-Identifier: GPL-2.0
/*
 * Apple T1 Touch Bar digitizer and panel power, USB configuration 2
 * (MacBookPro13,2 and the other 2016/2017 Touch Bar models).
 *
 * In configuration 2 the T1 shows the Touch Bar as a DRM display
 * (appletbdrm) and its touch surface on HID interface 2: a Digitizer /
 * Touch Pad application with ten finger collections, no report ID, 52-byte
 * input reports. Each 4-byte slot is: flags (bits 0-3 contact id, 4 tip,
 * 5 in range, 6-7 reserved), X little-endian 0..32767, Y 0..127; the last
 * twelve bytes are vendor data. The layout follows T1Bridge's
 * crates/t1-touchbar-hw/src/digitizer.rs (MIT, Copyright (c) 2026 T1Bridge
 * contributors, https://github.com/standardagents/t1bridge) and the device's
 * report descriptor. Here the slots become a multitouch input device named
 * "... Touch Bar", which is what tiny-dfr looks for.
 *
 * Panel power is HID feature report 3 on interface 6, byte 1 = 1 parked /
 * 2 awake (T1Bridge issues #47/#49). Brightness is feature report 2 of the
 * same vendor collection (0xff12 usage 1, the one hid-appletb-bl drives in
 * configuration 1): aux 1, then a level, 1 full / 2 about 15 % / 3 about 10 %
 * / 4 off; the T1 has no other steps (the percentage fields are ignored,
 * tested 2026-10-22). "appletb_backlight" 0..255: 0 parks the panel, 1..84
 * is level 3, 85..169 level 2, 170..255 level 1, so tiny-dfr's dimmed value
 * (1) dims and its active one (255) is full. appletbdrm parks the panel at
 * suspend and wakes it at resume; here the level is set again after a
 * resume, and a panel the user switched off is parked again.
 *
 * Binds only to the digitizer (checked from the parsed descriptor, before
 * any I/O): in configuration 1 hid-appletb-kbd and hid-sensor-hub keep
 * their interfaces.
 */
#include <linux/backlight.h>
#include <linux/hid.h>
#include <linux/input/mt.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include <linux/usb.h>

#define T1_VENDOR		0x05ac
#define T1_PRODUCT		0x8600
#define T1_DIGITIZER_IFACE	2
#define T1_POWER_IFACE		6

#define T1_SLOTS		10
#define T1_SLOT_SIZE		4
#define T1_REPORT_SIZE		52
#define T1_X_MAX		32767
#define T1_Y_MAX		127
/* active area about 255 x 7.5 mm (2170 x 60 px panel) */
#define T1_X_RES		(T1_X_MAX / 255)
#define T1_Y_RES		(T1_Y_MAX * 10 / 75)

#define T1_PARKED		0x01
#define T1_AWAKE		0x02
#define T1_LEVEL_FULL		1
#define T1_LEVEL_DIM		2	/* about 15 % */
#define T1_LEVEL_DIMMER		3	/* about 10 % */

struct t1tb {
	struct hid_device *hdev;
	struct input_dev *input;
	struct backlight_device *bl;
	int brightness;
	u8 level;	/* last report 2 level sent, 0 unknown */
};

static bool t1tb_is_digitizer(struct hid_device *hdev)
{
	struct usb_interface *intf;
	unsigned int i;

	if (!hid_is_usb(hdev))
		return false;
	intf = to_usb_interface(hdev->dev.parent);
	if (intf->cur_altsetting->desc.bInterfaceNumber != T1_DIGITIZER_IFACE)
		return false;
	for (i = 0; i < hdev->maxcollection; i++)
		if (hdev->collection[i].type == HID_COLLECTION_APPLICATION &&
		    hdev->collection[i].usage == HID_DG_TOUCHPAD)
			return true;
	return false;
}

/* SET_REPORT of a feature report on interface 6 (not ours: plain ep0) */
static int t1tb_set_feature(struct t1tb *tb, const u8 *data, int len)
{
	struct usb_device *udev = interface_to_usbdev(to_usb_interface(tb->hdev->dev.parent));
	u8 *buf;
	int ret;

	buf = kmemdup(data, len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), HID_REQ_SET_REPORT,
			      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			      (HID_FEATURE_REPORT + 1) << 8 | data[0], T1_POWER_IFACE,
			      buf, len, USB_CTRL_SET_TIMEOUT);
	kfree(buf);
	return ret < 0 ? ret : 0;
}

static int t1tb_set_power(struct t1tb *tb, u8 state)
{
	const u8 report[15] = { 0x03, state, 0xf4, 0x01 };
	int ret = t1tb_set_feature(tb, report, sizeof(report));

	if (ret)
		hid_warn(tb->hdev, "panel %s failed (%d)\n",
			 state == T1_AWAKE ? "wake" : "park", ret);
	return ret;
}

/* report 2: aux 1, level, 0 % and 1.0 (both ignored by the T1) */
static int t1tb_set_level(struct t1tb *tb, u8 level)
{
	const u8 report[11] = { 0x02, 0x01, level, 0, 0, 0, 0, 0xe8, 0x03 };
	int ret = t1tb_set_feature(tb, report, sizeof(report));

	if (ret)
		hid_warn(tb->hdev, "panel level %u failed (%d)\n", level, ret);
	else
		tb->level = level;
	return ret;
}

static u8 t1tb_level_for(int value)
{
	if (value >= 170)
		return T1_LEVEL_FULL;
	return value >= 85 ? T1_LEVEL_DIM : T1_LEVEL_DIMMER;
}

static int t1tb_bl_update(struct backlight_device *bl)
{
	struct t1tb *tb = bl_get_data(bl);
	int value = backlight_get_brightness(bl);
	int ret;

	if (!value) {
		if (tb->brightness) {
			ret = t1tb_set_power(tb, T1_PARKED);
			if (ret)
				return ret;
		}
		tb->brightness = 0;
		return 0;
	}
	if (!tb->brightness) {
		ret = t1tb_set_power(tb, T1_AWAKE);
		if (ret)
			return ret;
	}
	tb->brightness = value;
	if (t1tb_level_for(value) != tb->level)
		return t1tb_set_level(tb, t1tb_level_for(value));
	return 0;
}

/* no BL_CORE_SUSPENDRESUME: appletbdrm parks and wakes the panel around sleep */
static const struct backlight_ops t1tb_bl_ops = {
	.update_status = t1tb_bl_update,
};

static int t1tb_raw_event(struct hid_device *hdev, struct hid_report *report,
			  u8 *data, int size)
{
	struct t1tb *tb = hid_get_drvdata(hdev);
	struct input_dev *input = tb->input;
	unsigned int i;

	if (!input || size != T1_REPORT_SIZE)
		return 0;

	/* drop the whole frame on any malformed slot, as T1Bridge does */
	for (i = 0; i < T1_SLOTS; i++) {
		const u8 *s = data + i * T1_SLOT_SIZE;

		if ((s[0] & 0xc0) || get_unaligned_le16(s + 1) > T1_X_MAX || s[3] > T1_Y_MAX)
			return 0;
	}

	for (i = 0; i < T1_SLOTS; i++) {
		const u8 *s = data + i * T1_SLOT_SIZE;
		bool tip = s[0] & 0x10;
		int slot;

		if (!tip)
			continue;
		slot = input_mt_get_slot_by_key(input, s[0] & 0x0f);
		if (slot < 0)
			continue;
		input_mt_slot(input, slot);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X, get_unaligned_le16(s + 1));
		input_report_abs(input, ABS_MT_POSITION_Y, s[3]);
	}
	/* contacts missing from this frame (lifted) are released here */
	input_mt_sync_frame(input);
	input_sync(input);
	return 0;
}

static int t1tb_input_register(struct t1tb *tb)
{
	struct hid_device *hdev = tb->hdev;
	struct input_dev *input;
	int ret;

	input = devm_input_allocate_device(&hdev->dev);
	if (!input)
		return -ENOMEM;
	input->name = "Apple Inc. iBridge Touch Bar";
	input->phys = hdev->phys;
	input->uniq = hdev->uniq;
	input->id.bustype = BUS_USB;
	input->id.vendor = T1_VENDOR;
	input->id.product = T1_PRODUCT;
	input->id.version = hdev->version;
	input->dev.parent = &hdev->dev;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, T1_X_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, T1_Y_MAX, 0, 0);
	input_abs_set_res(input, ABS_MT_POSITION_X, T1_X_RES);
	input_abs_set_res(input, ABS_MT_POSITION_Y, T1_Y_RES);
	ret = input_mt_init_slots(input, T1_SLOTS, INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		return ret;
	ret = input_register_device(input);
	if (ret)
		return ret;
	tb->input = input;
	return 0;
}

static int t1tb_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct backlight_properties props = { };
	struct t1tb *tb;
	int ret;

	ret = hid_parse(hdev);
	if (ret)
		return ret;
	/* not the digitizer: leave it to hid-appletb-kbd / hid-sensor-hub */
	if (!t1tb_is_digitizer(hdev))
		return -ENODEV;

	tb = devm_kzalloc(&hdev->dev, sizeof(*tb), GFP_KERNEL);
	if (!tb)
		return -ENOMEM;
	tb->hdev = hdev;
	tb->brightness = 255;	/* appletbdrm wakes the panel at probe */
	hid_set_drvdata(hdev, tb);

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;
	ret = t1tb_input_register(tb);
	if (ret)
		goto stop;
	ret = hid_hw_open(hdev);
	if (ret)
		goto stop;

	props.type = BACKLIGHT_RAW;
	props.max_brightness = 255;
	props.brightness = 255;
	tb->bl = devm_backlight_device_register(&hdev->dev, "appletb_backlight", &hdev->dev,
						tb, &t1tb_bl_ops, &props);
	if (IS_ERR(tb->bl)) {
		hid_warn(hdev, "no panel power control (%pe)\n", tb->bl);
		tb->bl = NULL;
	}
	t1tb_set_level(tb, T1_LEVEL_FULL);

	hid_info(hdev, "T1 Touch Bar digitizer\n");
	return 0;

stop:
	hid_hw_stop(hdev);
	return ret;
}

static void t1tb_remove(struct hid_device *hdev)
{
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int t1tb_resume(struct hid_device *hdev)
{
	struct t1tb *tb = hid_get_drvdata(hdev);

	if (!tb->bl)
		return 0;
	/* appletbdrm sets the panel awake at resume; keep it off if it was */
	if (!tb->brightness)
		t1tb_set_power(tb, T1_PARKED);
	else	/* the level the T1 kept over the sleep is not known: set it again */
		t1tb_set_level(tb, t1tb_level_for(tb->brightness));
	return 0;
}

static const struct hid_device_id t1tb_ids[] = {
	{ HID_USB_DEVICE(T1_VENDOR, T1_PRODUCT) },
	{ }
};
MODULE_DEVICE_TABLE(hid, t1tb_ids);

static struct hid_driver t1tb_driver = {
	.name = "hid-t1-touchbar",
	.id_table = t1tb_ids,
	.probe = t1tb_probe,
	.remove = t1tb_remove,
	.raw_event = t1tb_raw_event,
	.resume = pm_ptr(t1tb_resume),
	.reset_resume = pm_ptr(t1tb_resume),
};
module_hid_driver(t1tb_driver);

MODULE_DESCRIPTION("Apple T1 Touch Bar digitizer, panel power and brightness (USB configuration 2)");
MODULE_LICENSE("GPL");
