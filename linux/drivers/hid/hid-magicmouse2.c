/*
 *   Apple "Magic" Wireless Mouse driver
 *
 *   Copyright (c) 2010 Michael Poole <mdpoole@troilus.org>
 *   Copyright (c) 2010 Chase Douglas <chase.douglas@canonical.com>
 *   Copyright (c) 2018 Rohit Pidaparthi <rohitkernel@gmail.com>
 *   Copyright (c) 2021 Ricardo Rodrigues <ricardo.e.p.rodrigues@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input/mt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>

#include "hid-ids.h"

static bool emulate_3button = true;
module_param(emulate_3button, bool, 0644);
MODULE_PARM_DESC(emulate_3button, "Emulate a middle button");

static bool middle_click_3finger = false;
module_param(middle_click_3finger, bool, 0644);
MODULE_PARM_DESC(middle_click_3finger, "Use 3 finger click to emulate middle button");

static int middle_button_start = -250;
static int middle_button_stop = +750;

static bool emulate_scroll_wheel = true;
module_param(emulate_scroll_wheel, bool, 0644);
MODULE_PARM_DESC(emulate_scroll_wheel, "Emulate a scroll wheel");

static bool stop_scroll_while_moving = false;
module_param(stop_scroll_while_moving, bool, 0644);
MODULE_PARM_DESC(stop_scroll_while_moving, "Stop scrolling whenever the mouse moves");

static unsigned int scroll_speed = 0;
static int param_set_scroll_speed(const char *val,
				  const struct kernel_param *kp) {
	unsigned long speed;
	if (!val || kstrtoul(val, 0, &speed) || speed > 63)
		return -EINVAL;
	scroll_speed = speed;
	return 0;
}
module_param_call(scroll_speed, param_set_scroll_speed, param_get_uint, &scroll_speed, 0644);
MODULE_PARM_DESC(scroll_speed, "Scroll speed, value from 0 (slow) to 63 (fast)");

static unsigned int scroll_delay_pos_x = 200;
static int param_set_scroll_delay_pos_x(const char *val,
				  const struct kernel_param *kp) {
	unsigned long delay;
	if (!val || kstrtoul(val, 0, &delay))
		return -EINVAL;
	scroll_delay_pos_x = delay;
	return 0;
}
module_param_call(scroll_delay_pos_x, param_set_scroll_delay_pos_x, param_get_uint, &scroll_delay_pos_x, 0644);
MODULE_PARM_DESC(scroll_delay_pos_x, "Scroll X position delay before start scrolling");

static unsigned int scroll_delay_pos_y = 200;
static int param_set_scroll_delay_pos_y(const char *val,
				  const struct kernel_param *kp) {
	unsigned long delay;
	if (!val || kstrtoul(val, 0, &delay))
		return -EINVAL;
	scroll_delay_pos_y = delay;
	return 0;
}
module_param_call(scroll_delay_pos_y, param_set_scroll_delay_pos_y, param_get_uint, &scroll_delay_pos_y, 0644);
MODULE_PARM_DESC(scroll_delay_pos_y, "Scroll Y position delay before start scrolling");

static bool scroll_acceleration = true;
module_param(scroll_acceleration, bool, 0644);
MODULE_PARM_DESC(scroll_acceleration, "Accelerate sequential scroll events");

static bool report_undeciphered;
module_param(report_undeciphered, bool, 0644);
MODULE_PARM_DESC(report_undeciphered, "Report undeciphered multi-touch state field using a MSC_RAW event");

#define TRACKPAD_REPORT_ID 0x28
#define TRACKPAD2_USB_REPORT_ID 0x02
#define TRACKPAD2_BT_REPORT_ID 0x31
#define MOUSE_REPORT_ID    0x29
#define MOUSE2_REPORT_ID   0x12
#define MOUSE2_REQUEST_REPORT_ID   0xa1
#define DOUBLE_REPORT_ID   0xf7
/* These definitions are not precise, but they're close enough.  (Bits
 * 0x03 seem to indicate the aspect ratio of the touch, bits 0x70 seem
 * to be some kind of bit mask -- 0x20 may be a near-field reading,
 * and 0x40 is actual contact, and 0x10 may be a start/stop or change
 * indication.)
 */
#define TOUCH_STATE_MASK  0xf0
#define TOUCH_STATE_NONE  0x00
#define TOUCH_STATE_START 0x30
#define TOUCH_STATE_DRAG  0x40

/* Number of high-resolution events for each low-resolution detent. */
#define SCROLL_HR_STEPS 10
#define SCROLL_HR_MULT (120 / SCROLL_HR_STEPS)
#define SCROLL_HR_THRESHOLD 90 /* units */
#define SCROLL_ACCEL_DEFAULT 1

/* Touch surface information. Dimension is in hundredths of a mm, min and max
 * are in units. */
#define MOUSE_DIMENSION_X (float)9056
#define MOUSE_MIN_X -1100
#define MOUSE_MAX_X 1258
#define MOUSE_RES_X ((MOUSE_MAX_X - MOUSE_MIN_X) / (MOUSE_DIMENSION_X / 100))
#define MOUSE_DIMENSION_Y (float)5152
#define MOUSE_MIN_Y -1589
#define MOUSE_MAX_Y 2047
#define MOUSE_RES_Y ((MOUSE_MAX_Y - MOUSE_MIN_Y) / (MOUSE_DIMENSION_Y / 100))

#define TRACKPAD_DIMENSION_X (float)13000
#define TRACKPAD_MIN_X -2909
#define TRACKPAD_MAX_X 3167
#define TRACKPAD_RES_X \
	((TRACKPAD_MAX_X - TRACKPAD_MIN_X) / (TRACKPAD_DIMENSION_X / 100))
#define TRACKPAD_DIMENSION_Y (float)11000
#define TRACKPAD_MIN_Y -2456
#define TRACKPAD_MAX_Y 2565
#define TRACKPAD_RES_Y \
	((TRACKPAD_MAX_Y - TRACKPAD_MIN_Y) / (TRACKPAD_DIMENSION_Y / 100))

#define TRACKPAD2_DIMENSION_X (float)16000
#define TRACKPAD2_MIN_X -3678
#define TRACKPAD2_MAX_X 3934
#define TRACKPAD2_RES_X \
	((TRACKPAD2_MAX_X - TRACKPAD2_MIN_X) / (TRACKPAD2_DIMENSION_X / 100))
#define TRACKPAD2_DIMENSION_Y (float)11490
#define TRACKPAD2_MIN_Y -2478
#define TRACKPAD2_MAX_Y 2587
#define TRACKPAD2_RES_Y \
	((TRACKPAD2_MAX_Y - TRACKPAD2_MIN_Y) / (TRACKPAD2_DIMENSION_Y / 100))

#define MAX_TOUCHES		16

/**
 * struct magicmouse_sc - Tracks Magic Mouse-specific data.
 * @input: Input device through which we report events.
 * @quirks: Currently unused.
 * @ntouches: Number of touches in most recent touch report.
 * @scroll_accel: Number of consecutive scroll motions.
 * @scroll_jiffies: Time of last scroll motion.
 * @drag_start: Time of drag start.
 * @touches: Most recent data for a touch, indexed by tracking ID.
 * @tracking_ids: Mapping of current touch input data to @touches.
 */
struct magicmouse_sc {
	struct input_dev *input;
	unsigned long quirks;

	int ntouches;
	int scroll_accel;
	unsigned long scroll_jiffies;
	int x;
	int y;

	struct {
		short x;
		short y;
		short scroll_x;
		short scroll_y;
		short scroll_x_hr;
		short scroll_y_hr;
		u8 size;
		bool scroll_x_active;
		bool scroll_y_active;
	} touches[MAX_TOUCHES];
	int tracking_ids[MAX_TOUCHES];

	struct hid_device *hdev;
	struct delayed_work work;
};

static int magicmouse_firm_touch(struct magicmouse_sc *msc)
{
	int touch = -1;
	int ii;

	/* If there is only one "firm" touch, set touch to its
	 * tracking ID.
	 */
	for (ii = 0; ii < msc->ntouches; ii++) {
		int idx = msc->tracking_ids[ii];
		if (msc->touches[idx].size < 8) {
			/* Ignore this touch. */
			continue;
		} 
		
		if (touch >= 0) {
			touch = -1;
			break;
		} else {
			touch = idx;
		}
	}

	return touch;
}

static int magicmouse_firm_touch_v2(struct magicmouse_sc *msc, int firmness)
{
	int touch = 0;
	int ii;

	/* If there is only one "firm" touch, set touch to its
	 * tracking ID.
	 */
	for (ii = 0; ii < msc->ntouches; ii++) {
		int idx = msc->tracking_ids[ii];
		if (msc->touches[idx].size < firmness) {
			/* Ignore this touch. */
			continue;
		} 
		
		// if (touch >= 0) {
		// 	touch = -2;
		// 	break;
		// } else {
		// 	touch = idx;
		// }
		touch++;
	}

	return touch;
}

static int magicmouse_detect_3finger_click(struct magicmouse_sc *msc)
{
	// int fingers = 0;
	// int ii;

	// /* Only consider fingers with size > 10 as real clicks.
	//  * TODO: Consider better palm rejection.
	//  */
	// for (ii = 0; ii < msc->ntouches; ii++) {
	// 	int idx = msc->tracking_ids[ii];
	// 	if (msc->touches[idx].size > 10) {
	// 		fingers++;
	// 	}
	// }

	// return fingers;

	int ii;
	int touch_size = 0;

	for (ii = 0; ii < msc->ntouches; ii++) {
		int idx = msc->tracking_ids[ii];
		if (msc->touches[idx].size > 0.1) {
			touch_size += msc->touches[idx].size;
		}
	}

	return touch_size > 5 && touch_size < 12;
}

static int magicmouse_detect_2fingers(struct magicmouse_sc *msc)
{
	int ii;
	int touch_size = 0;

	for (ii = 0; ii < msc->ntouches; ii++) {
		int idx = msc->tracking_ids[ii];
		if (msc->touches[idx].size > 0.1) {
			touch_size += msc->touches[idx].size;
		}
	}

	return touch_size > 0.8 && touch_size < 9;

	// int fingers = 0;
	// int ii;

	// /* Only consider fingers with size > 10 as real clicks.
	//  * TODO: Consider better palm rejection.
	//  */
	// for (ii = 0; ii < msc->ntouches; ii++) {
	// 	int idx = msc->tracking_ids[ii];
	// 	if (msc->touches[idx].size > 10) {
	// 		fingers++;
	// 	}
	// }

	// return fingers == 2;
}

static void magicmouse_emit_buttons(struct magicmouse_sc *msc, int state)
{
	int last_state = test_bit(BTN_LEFT, msc->input->key) << 0 |
		test_bit(BTN_RIGHT, msc->input->key) << 1 |
		test_bit(BTN_MIDDLE, msc->input->key) << 2;

	if (emulate_3button) {
		int id;

		id = magicmouse_firm_touch(msc);

		/* If some button was pressed before, keep it held
		 * down.  Otherwise, if there's exactly one firm
		 * touch, use that to override the mouse's guess.
		 */
		if (state == 0) {
			/* The button was released. */
			// int t_touches = magicmouse_firm_touch_v2(msc, 16);
			// printk(KERN_INFO "MAGIC MOUSE DATA: %d %d\n", msc->ntouches, t_touches);
			// if (last_state == 0 && msc->ntouches == 1 && t_touches == msc->ntouches) {
			// 	state = 1;
			// }	
		} else if (last_state != 0) {
			state = last_state;
		} else if (id >= 0 && middle_click_3finger){
			int x;
			x = msc->touches[id].x;
			if (magicmouse_detect_2fingers(msc))
				state = 4;
			else if (x <= 0)
				state = 1;
			else if (x > 0)
				state = 2;
		} else if (id >= 0) {
			int x = msc->touches[id].x;
			if (x < middle_button_start)
				state = 1;
			else if (x > middle_button_stop)
				state = 2;
			else
				state = 4;
		}/* else: we keep the mouse's guess */

		// int t_count = magicmouse_firm_touch_v2(msc);

		// if (state == 0) {
		// 	// pass
		// } else if (last_state == 2) {
		// 	state = 2;
		// } else if (state == 1) {
		// 	if (t_count > 2) {
		// 		state = 4;
		// 	}
		// }

		input_report_key(msc->input, BTN_MIDDLE, state & 4);
	}

	input_report_key(msc->input, BTN_LEFT, state & 1);
	input_report_key(msc->input, BTN_RIGHT, state & 2);

	if (state != last_state)
		msc->scroll_accel = SCROLL_ACCEL_DEFAULT;
}

static void magicmouse_emit_touch(struct magicmouse_sc *msc, int raw_id,
		u8 *tdata, int npoints, int mouse_loc_x, int mouse_loc_y)
{
	struct input_dev *input = msc->input;
	int id, x, y, size, orientation, touch_major, touch_minor, state, down;
	int pressure = 0;

	/* tdata is 8 bytes per finger detected.
		* tdata[0] (lsb of x) and least sig 4bits of tdata[1] (msb of x)
		*          are x position of touch on touch surface.
		* tdata[1] most sig 4bits (lsb of y) and and tdata[2] (msb of y)
		*          are y position of touch on touch surface.
		* tdata[1] bits look like [y y y y x x x x]
		* tdata[3] touch major axis of ellipse of finger detected
		* tdata[4] touch minor axis of ellipse of finger detected
		* tdata[5] contains 6bits of size info (lsb) and the two msb of tdata[5]
		*          are the lsb of id: [id id size size size size size size]
		* tdata[6] 2 lsb bits of tdata[6] are the msb of id and 6msb of tdata[6]
		*          are the orientation of the touch. [o o o o o o id id]
		* tdata[7] 4 msb are state. 4lsb are unknown.
		*
		* [ x x x x x x x x ]
		* [ y y y y x x x x ]
		* [ y y y y y y y y ]
		* [touch major      ]
		* [touch minor      ]
		* [id id s s s s s s]
		* [o o o o o o id id]
		* [s s s s | unknown]
		*/
	id = (tdata[6] << 2 | tdata[5] >> 6) & 0xf;
	x = (tdata[1] << 28 | tdata[0] << 20) >> 20;
	y = -((tdata[2] << 24 | tdata[1] << 16) >> 20);
	size = tdata[5] & 0x3f;
	orientation = (tdata[6] >> 2) - 32;
	touch_major = tdata[3];
	touch_minor = tdata[4];
	state = tdata[7] & TOUCH_STATE_MASK;
	down = state != TOUCH_STATE_NONE;

	/* Store tracking ID and other fields. */
	msc->tracking_ids[raw_id] = id;
	msc->touches[id].x = x;
	msc->touches[id].y = y;
	msc->touches[id].size = size;

	/* If requested, emulate a scroll wheel by detecting small
	 * vertical touch motions.
	 */
	if (emulate_scroll_wheel) {
		unsigned long now = jiffies;
		int step_x = msc->touches[id].scroll_x - x;
		int step_y = msc->touches[id].scroll_y - y;
		int step_hr = ((128 - (int)scroll_speed) * msc->scroll_accel) /
			      SCROLL_HR_STEPS;
		int step_x_hr = msc->touches[id].scroll_x_hr - x;
		int step_y_hr = msc->touches[id].scroll_y_hr - y;

		/* Determine if the mouse has moved, if so then disable scrolling. */
		bool continue_scroll = true;
		// if (stop_scroll_while_moving)
		// {
		// 	continue_scroll = msc->x == mouse_loc_x && msc->y == mouse_loc_y;
		// }

		if (continue_scroll) {
			/* Calculate and apply the scroll motion. */
			switch (state) {
			case TOUCH_STATE_START:
				msc->touches[id].scroll_x = x;
				msc->touches[id].scroll_y = y;
				msc->touches[id].scroll_x_hr = x;
				msc->touches[id].scroll_y_hr = y;
				msc->touches[id].scroll_x_active = false;
				msc->touches[id].scroll_y_active = false;

				/* Reset acceleration after half a second. */
				if (scroll_acceleration && time_before(now,
							msc->scroll_jiffies + HZ / 2))
					msc->scroll_accel = max_t(int,
							msc->scroll_accel - 1, 1);
				else
					msc->scroll_accel = SCROLL_ACCEL_DEFAULT;

				break;
			case TOUCH_STATE_DRAG:
				/* Add a position delay since the drag start in which
				* drag events are not registered. This decreases the
				* sensitivity of dragging on Magic Mouse devices.
				*/
				if (abs(step_x) < scroll_delay_pos_x) {
					step_x = 0;
				} else {
					step_x /= (64 - (int)scroll_speed) * msc->scroll_accel;
				}

				if (abs(step_y) < scroll_delay_pos_y) {
					step_y = 0;
				} else {
					step_y /= (64 - (int)scroll_speed) * msc->scroll_accel;
				}

				// if (!magicmouse_detect_2fingers(msc)) {
				int t_touches = magicmouse_firm_touch_v2(msc, 5);
				// if (msc->ntouches != 1 || t_touches != msc->ntouches) {
				if (t_touches != 0 || x < middle_button_start || x > middle_button_stop) {
					step_x = 0;
					step_y = 0;
					step_x_hr = 0;
					step_y_hr = 0;
				}

				// if (abs(msc->x - mouse_loc_x) > 3) {
				// 	step_x = 0;
				// 	step_x_hr = 0;
				// }

				// if (abs(msc->y - mouse_loc_y) > 3) {
				// 	step_y = 0;
				// 	step_y_hr = 0;
				// }

				if (step_x != 0) {
					msc->touches[id].scroll_x -= step_x *
						(64 - scroll_speed) * msc->scroll_accel;
					msc->scroll_jiffies = now;
					input_report_rel(input, REL_HWHEEL, -step_x);
				}

				if (step_y != 0) {
					msc->touches[id].scroll_y -= step_y *
						(64 - scroll_speed) * msc->scroll_accel;
					msc->scroll_jiffies = now;
					input_report_rel(input, REL_WHEEL, step_y);
				}

				if (!msc->touches[id].scroll_x_active &&
					abs(step_x_hr) > SCROLL_HR_THRESHOLD) {
					msc->touches[id].scroll_x_active = true;
					msc->touches[id].scroll_x_hr = x;
					step_x_hr = 0;
				}

				step_x_hr /= step_hr;
				if (step_x_hr != 0 &&
					msc->touches[id].scroll_x_active) {
					msc->touches[id].scroll_x_hr -= step_x_hr *
						step_hr;
					input_report_rel(input,
							REL_HWHEEL_HI_RES,
							-step_x_hr * SCROLL_HR_MULT);
				}

				if (!msc->touches[id].scroll_y_active &&
					abs(step_y_hr) > SCROLL_HR_THRESHOLD) {
					msc->touches[id].scroll_y_active = true;
					msc->touches[id].scroll_y_hr = y;
					step_y_hr = 0;
				}

				step_y_hr /= step_hr;
				if (step_y_hr != 0 &&
					msc->touches[id].scroll_y_active) {
					msc->touches[id].scroll_y_hr -= step_y_hr *
						step_hr;
					input_report_rel(input,
							REL_WHEEL_HI_RES,
							step_y_hr * SCROLL_HR_MULT);
				}
				break;
			}
		}
	}

	if (down)
		msc->ntouches++;

	input_mt_slot(input, input_mt_get_slot_by_key(input, id));
	input_mt_report_slot_state(input, MT_TOOL_FINGER, down);

	/* Generate the input events for this touch. */
	if (down) {
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch_major << 2);
		input_report_abs(input, ABS_MT_TOUCH_MINOR, touch_minor << 2);
		input_report_abs(input, ABS_MT_ORIENTATION, -orientation);
		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);

		if (input->id.product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD2) {
			input_report_abs(input, ABS_TOOL_WIDTH, size);
			input_report_abs(input, ABS_MT_PRESSURE, pressure + 30);
		}

		if (report_undeciphered) {
			if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE ||
				input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE2)
				input_event(input, EV_MSC, MSC_RAW, tdata[7]);
			else if (input->id.product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD)
				input_event(input, EV_MSC, MSC_RAW, tdata[8]);
		}
	}
}

static int magicmouse_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);
	struct input_dev *input = msc->input;
	int x = 0, y = 0, ii, clicks = 0, npoints;

	switch (data[0]) {
	case TRACKPAD_REPORT_ID:
	case TRACKPAD2_BT_REPORT_ID:
		/* Expect four bytes of prefix, and N*9 bytes of touch data. */
		if (size < 4 || ((size - 4) % 9) != 0)
			return 0;
		npoints = (size - 4) / 9;
		if (npoints > 15) {
			hid_warn(hdev, "invalid size value (%d) for TRACKPAD_REPORT_ID\n",
					size);
			return 0;
		}
		msc->ntouches = 0;
		for (ii = 0; ii < npoints; ii++)
			magicmouse_emit_touch(msc, ii, data + ii * 9 + 4, npoints, 0, 0);

		clicks = data[1];
		break;
	case TRACKPAD2_USB_REPORT_ID:
		/* Expect twelve bytes of prefix, and N*9 bytes of touch data. */
		if (size < 12 || ((size - 12) % 9) != 0)
			return 0;
		npoints = (size - 12) / 9;
		if (npoints > 15) {
			hid_warn(hdev, "invalid size value (%d) for TRACKPAD_REPORT_ID\n",
					size);
			return 0;
		}
		msc->ntouches = 0;
		for (ii = 0; ii < npoints; ii++)
			magicmouse_emit_touch(msc, ii, data + ii * 9 + 12, npoints, 0, 0);

		clicks = data[1];
		break;
	case MOUSE_REPORT_ID:
		/* Expect six bytes of prefix, and N*8 bytes of touch data. */
		if (size < 6 || ((size - 6) % 8) != 0)
			return 0;
		npoints = (size - 6) / 8;
		if (npoints > 15) {
			hid_warn(hdev, "invalid size value (%d) for MOUSE_REPORT_ID\n",
					size);
			return 0;
		}
		msc->ntouches = 0;

		/* When emulating three-button mode, it is important
		 * to have the current touch information before
		 * generating a click event.
		 */
		x = (int)(((data[3] & 0x0c) << 28) | (data[1] << 22)) >> 22;
		y = (int)(((data[3] & 0x30) << 26) | (data[2] << 22)) >> 22;
		for (ii = 0; ii < npoints; ii++)
			magicmouse_emit_touch(msc, ii, data + ii * 8 + 6, npoints, x, y);

		clicks = data[3];
		/* The following bits provide a device specific timestamp. They
		 * are unused here.
		 *
		 * ts = data[3] >> 6 | data[4] << 2 | data[5] << 10;
		 */
		break;
	case MOUSE2_REPORT_ID:
		/* The data layout for magic mouse 2 is:
		 * 14 bytes of prefix
		 * data[0] is the device report ID
		 * data[1] is the mouse click events. Value of 1 is left, 2 is right.
		 * data[2] (contains lsb) and data[3] (msb) are the x movement
		 *         of the mouse 16bit representation.
		 * data[4] (contains msb) and data[5] (msb) are the y movement
		 *         of the mouse 16bit representation.
		 * data[6] data[13] are unknown so far. Need to decode this still
		 *
		 * data[14] onwards represent touch data on top of the mouse surface
		 *          touchpad. There are 8 bytes per finger. e.g:
		 * data[14]-data[21] will be the first finger detected.
		 * data[22]-data[29] will be finger 2 etc.
		 * these sets of 8 bytes are passed in as tdata to
		 * magicmouse_emit_touch()
		 *
		 * npoints is the number of fingers detected.
		 * size is minimum 14 but could be any multpiple of 14+ii*8 based on
		 * how many fingers are detected. e.g for 1 finger, size=22 for
		 * 2 fingers, size=30 and so on.
		 */
		if (size > 14 && ((size - 14) % 8) != 0)
            return 0;
        npoints = (size - 14) / 8;
        if (npoints > 15) {
            hid_warn(hdev, "invalid size value (%d) for MOUSE_REPORT_ID\n",
                     size);
            return 0;
        }
        msc->ntouches = 0;

        /* When emulating three-button mode, it is important
         * to have the current touch information before
         * generating a click event.
         */
        x = (int)((data[3] << 24) | (data[2] << 16)) >> 16;
        y = (int)((data[5] << 24) | (data[4] << 16)) >> 16;

		// print the values of the first 14 bytes of data and number of points and size.
		// printk("The contents of npoints are: %i\n", npoints);
		// printk("Size is: %i\n", size);
		// int jj = 0;
		// for (jj=0; jj < 15; jj++){
		// 	int d = data[jj];
		// 	printk("data %i is: %i\n", jj, d);
		// }
        for (ii = 0; ii < npoints; ii++)
            magicmouse_emit_touch(msc, ii, data + ii * 8 + 14, npoints, x, y);
		
        clicks = data[1];
        break;
	case DOUBLE_REPORT_ID:
		/* Sometimes the trackpad sends two touch reports in one
		 * packet.
		 */
		magicmouse_raw_event(hdev, report, data + 2, data[1]);
		magicmouse_raw_event(hdev, report, data + 2 + data[1],
			size - 2 - data[1]);
		break;
	default:
		return 0;
	}

	if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE ||
		input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE2) {
		msc->x = x;
		msc->y = y;
		magicmouse_emit_buttons(msc, clicks & 3);
		input_report_rel(input, REL_X, x);
		input_report_rel(input, REL_Y, y);
	} else if (input->id.product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD) {
		input_report_key(input, BTN_MOUSE, clicks & 1);
		input_mt_report_pointer_emulation(input, true);
	}
	else /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD2 */ {
		input_mt_sync_frame(input);
		input_report_key(input, BTN_MOUSE, clicks & 1);
	}

	input_sync(input);
	return 1;
}

static int magicmouse_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);
	if (msc->input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE2 &&
	    field->report->id == MOUSE2_REPORT_ID) {
		// magic_mouse_raw_event has done all the work. Skip hidinput.
		//
		// Specifically, hidinput may modify BTN_LEFT and BTN_RIGHT,
		// breaking emulate_3button.
		return 1;
	}
	return 0;
}

static int magicmouse_setup_input(struct input_dev *input, struct hid_device *hdev)
{
	int error;
	int mt_flags = 0;

	__set_bit(EV_KEY, input->evbit);

	if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE ||
		input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE2) {
		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(BTN_RIGHT, input->keybit);
		if (emulate_3button)
			__set_bit(BTN_MIDDLE, input->keybit);

		__set_bit(EV_REL, input->evbit);
		__set_bit(REL_X, input->relbit);
		__set_bit(REL_Y, input->relbit);
		if (emulate_scroll_wheel) {
			__set_bit(REL_WHEEL, input->relbit);
			__set_bit(REL_HWHEEL, input->relbit);
			__set_bit(REL_WHEEL_HI_RES, input->relbit);
			__set_bit(REL_HWHEEL_HI_RES, input->relbit);
		}
	} else if (input->id.product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD){
		/* input->keybit is initialized with incorrect button info
		 * for Magic Trackpad. There really is only one physical
		 * button (BTN_LEFT == BTN_MOUSE). Make sure we don't
		 * advertise buttons that don't exist...
		 */
		__clear_bit(BTN_RIGHT, input->keybit);
		__clear_bit(BTN_MIDDLE, input->keybit);
		__set_bit(BTN_MOUSE, input->keybit);
		__set_bit(BTN_TOOL_FINGER, input->keybit);
		__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
		__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
		__set_bit(BTN_TOOL_QUADTAP, input->keybit);
		__set_bit(BTN_TOOL_QUINTTAP, input->keybit);
		__set_bit(BTN_TOUCH, input->keybit);
		__set_bit(INPUT_PROP_POINTER, input->propbit);
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);
	}
	else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD2) */

		/* setting the device name to ensure the same driver settings
		 * get loaded, whether connected through bluetooth or USB
		 */
		input->name = "Apple Inc. Magic Trackpad 2";

		__clear_bit(EV_MSC, input->evbit);
		__clear_bit(ABS_TOOL_WIDTH, input->absbit);
		__clear_bit(BTN_0, input->keybit);
		__clear_bit(BTN_RIGHT, input->keybit);
		__clear_bit(BTN_MIDDLE, input->keybit);
		__set_bit(BTN_MOUSE, input->keybit);
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);
		__set_bit(BTN_TOOL_FINGER, input->keybit);

		mt_flags = INPUT_MT_POINTER | INPUT_MT_DROP_UNUSED | INPUT_MT_TRACK;
	}


	__set_bit(EV_ABS, input->evbit);

	error = input_mt_init_slots(input, MAX_TOUCHES, mt_flags);
	if (error)
		return error;
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255 << 2,
			     4, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MINOR, 0, 255 << 2,
			     4, 0);

	/* Note: Touch Y position from the device is inverted relative
	 * to how pointer motion is reported (and relative to how USB
	 * HID recommends the coordinates work).  This driver keeps
	 * the origin at the same position, and just uses the additive
	 * inverse of the reported Y.
	 */
	if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE ||
		input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE2) {
		input_set_abs_params(input, ABS_MT_ORIENTATION, -31, 32, 1, 0);
		input_set_abs_params(input, ABS_MT_POSITION_X,
				     MOUSE_MIN_X, MOUSE_MAX_X, 4, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y,
				     MOUSE_MIN_Y, MOUSE_MAX_Y, 4, 0);
		input_abs_set_res(input, ABS_MT_POSITION_X,
				  MOUSE_RES_X);
		input_abs_set_res(input, ABS_MT_POSITION_Y,
				  MOUSE_RES_Y);
	} else if (input->id.product ==  USB_DEVICE_ID_APPLE_MAGICTRACKPAD) {
		input_set_abs_params(input, ABS_MT_ORIENTATION, -31, 32, 1, 0);
		input_set_abs_params(input, ABS_X, TRACKPAD_MIN_X,
				     TRACKPAD_MAX_X, 4, 0);
		input_set_abs_params(input, ABS_Y, TRACKPAD_MIN_Y,
				     TRACKPAD_MAX_Y, 4, 0);
		input_set_abs_params(input, ABS_MT_POSITION_X,
				     TRACKPAD_MIN_X, TRACKPAD_MAX_X, 4, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y,
				     TRACKPAD_MIN_Y, TRACKPAD_MAX_Y, 4, 0);

		input_abs_set_res(input, ABS_X, TRACKPAD_RES_X);
		input_abs_set_res(input, ABS_Y, TRACKPAD_RES_Y);
		input_abs_set_res(input, ABS_MT_POSITION_X,
				  TRACKPAD_RES_X);
		input_abs_set_res(input, ABS_MT_POSITION_Y,
				  TRACKPAD_RES_Y);
	} else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD2 */
		input_set_abs_params(input, ABS_MT_PRESSURE, 0, 283, 0, 0);
		input_set_abs_params(input, ABS_PRESSURE, 0, 283, 0, 0);
		input_set_abs_params(input, ABS_TOOL_WIDTH, 0, 255, 0, 0);
		input_set_abs_params(input, ABS_MT_ORIENTATION, -3, 4, 0, 0);
		input_set_abs_params(input, ABS_X, TRACKPAD2_MIN_X,
				     TRACKPAD2_MAX_X, 0, 0);
		input_set_abs_params(input, ABS_Y, TRACKPAD2_MIN_Y,
				     TRACKPAD2_MAX_Y, 0, 0);
		input_set_abs_params(input, ABS_MT_POSITION_X,
				     TRACKPAD2_MIN_X, TRACKPAD2_MAX_X, 0, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y,
				     TRACKPAD2_MIN_Y, TRACKPAD2_MAX_Y, 0, 0);

		input_abs_set_res(input, ABS_X, TRACKPAD2_RES_X);
		input_abs_set_res(input, ABS_Y, TRACKPAD2_RES_Y);
		input_abs_set_res(input, ABS_MT_POSITION_X,
				  TRACKPAD2_RES_X);
		input_abs_set_res(input, ABS_MT_POSITION_Y,
				  TRACKPAD2_RES_Y);
	}

	input_set_events_per_packet(input, 60);

	if (report_undeciphered &&
	    input->id.product != USB_DEVICE_ID_APPLE_MAGICTRACKPAD2) {
		__set_bit(EV_MSC, input->evbit);
		__set_bit(MSC_RAW, input->mscbit);
	}

	return 0;
}

static int magicmouse_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);

	if (!msc->input)
		msc->input = hi->input;

	/* Magic Trackpad does not give relative data after switching to MT */
	if ((hi->input->id.product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD ||
	     hi->input->id.product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD2) &&
	    field->flags & HID_MAIN_ITEM_RELATIVE)
		return -1;

	return 0;
}

static int magicmouse_input_configured(struct hid_device *hdev,
		struct hid_input *hi)

{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);
	int ret;

	ret = magicmouse_setup_input(msc->input, hdev);
	if (ret) {
		hid_err(hdev, "magicmouse setup input failed (%d)\n", ret);
		/* clean msc->input to notify probe() of the failure */
		msc->input = NULL;
		return ret;
	}

	return 0;
}



static int magicmouse_enable_multitouch(struct hid_device *hdev)
{
	const u8 *feature;
	const u8 feature_mt[] = { 0xD7, 0x01 };
	const u8 feature_mt_mouse2[] = { 0xF1, 0x02, 0x01 };
	const u8 feature_mt_trackpad2_usb[] = { 0x02, 0x01 };
	const u8 feature_mt_trackpad2_bt[] = { 0xF1, 0x02, 0x01 };
	u8 *buf;
	int ret;
	int feature_size;

	if (hdev->product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD2) {
		if (hdev->vendor == BT_VENDOR_ID_APPLE) {
			feature_size = sizeof(feature_mt_trackpad2_bt);
			feature = feature_mt_trackpad2_bt;
		} else { /* USB_VENDOR_ID_APPLE */
			feature_size = sizeof(feature_mt_trackpad2_usb);
			feature = feature_mt_trackpad2_usb;
		}
	} else if (hdev->product == USB_DEVICE_ID_APPLE_MAGICMOUSE2) {
		feature_size = sizeof(feature_mt_mouse2);
		feature = feature_mt_mouse2;
	} else {
		feature_size = sizeof(feature_mt);
		feature = feature_mt;
	}

	buf = kmemdup(feature, feature_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, buf[0], buf, feature_size,
				HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	kfree(buf);
	return ret;
}

static void magicmouse_enable_mt_work(struct work_struct *work)
{
	struct magicmouse_sc *msc =
		container_of(work, struct magicmouse_sc, work.work);
	int ret;

	ret = magicmouse_enable_multitouch(msc->hdev);
	if (ret < 0)
		hid_err(msc->hdev, "unable to request touch data (%d)\n", ret);
}

static int magicmouse_probe(struct hid_device *hdev,
	const struct hid_device_id *id)
{
	struct magicmouse_sc *msc;
	struct hid_report *report;
	int ret;

	if (id->vendor == USB_VENDOR_ID_APPLE &&
	    id->product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD2 &&
	    hdev->type != HID_TYPE_USBMOUSE)
		return -ENODEV;

	msc = devm_kzalloc(&hdev->dev, sizeof(*msc), GFP_KERNEL);
	if (msc == NULL) {
		hid_err(hdev, "can't alloc magicmouse descriptor\n");
		return -ENOMEM;
	}

	msc->scroll_accel = SCROLL_ACCEL_DEFAULT;
	msc->hdev = hdev;
	INIT_DEFERRABLE_WORK(&msc->work, magicmouse_enable_mt_work);

	msc->quirks = id->driver_data;
	hid_set_drvdata(hdev, msc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "magicmouse hid parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "magicmouse hw start failed\n");
		return ret;
	}

	if (!msc->input) {
		hid_err(hdev, "magicmouse input not registered\n");
		ret = -ENOMEM;
		goto err_stop_hw;
	}

	if (id->product == USB_DEVICE_ID_APPLE_MAGICMOUSE)
		report = hid_register_report(hdev, HID_INPUT_REPORT,
			MOUSE_REPORT_ID, 0);
	else if (id->product == USB_DEVICE_ID_APPLE_MAGICMOUSE2)
		report = hid_register_report(hdev, HID_INPUT_REPORT,
			MOUSE2_REQUEST_REPORT_ID, 0);
	else if (id->product == USB_DEVICE_ID_APPLE_MAGICTRACKPAD) {
		report = hid_register_report(hdev, HID_INPUT_REPORT,
			TRACKPAD_REPORT_ID, 0);
		report = hid_register_report(hdev, HID_INPUT_REPORT,
			DOUBLE_REPORT_ID, 0);
	} else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD2 */
		if (id->vendor == BT_VENDOR_ID_APPLE)
			report = hid_register_report(hdev, HID_INPUT_REPORT,
				TRACKPAD2_BT_REPORT_ID, 0);
		else /* USB_VENDOR_ID_APPLE */
			report = hid_register_report(hdev, HID_INPUT_REPORT,
				TRACKPAD2_USB_REPORT_ID, 0);
	}

	if (!report) {
		hid_err(hdev, "unable to register touch report\n");
		ret = -ENOMEM;
		goto err_stop_hw;
	}
	report->size = 6;


	/*
	 * Some devices repond with 'invalid report id' when feature
	 * report switching it into multitouch mode is sent to it.
	 *
	 * This results in -EIO from the _raw low-level transport callback,
	 * but there seems to be no other way of switching the mode.
	 * Thus the super-ugly hacky success check below.
	 */
	ret = magicmouse_enable_multitouch(hdev);
	if (ret != -EIO && ret < 0) {
		hid_err(hdev, "unable to request touch data (%d)\n", ret);
		goto err_stop_hw;
	}
	if (ret == -EIO && id->product == USB_DEVICE_ID_APPLE_MAGICMOUSE2) {
		schedule_delayed_work(&msc->work, msecs_to_jiffies(500));
	}

	return 0;
err_stop_hw:
	hid_hw_stop(hdev);
	return ret;
}

static void magicmouse_remove(struct hid_device *hdev)
{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);
	if (msc)
		cancel_delayed_work_sync(&msc->work);
	hid_hw_stop(hdev);
}

static const struct hid_device_id magic_mice[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_MAGICMOUSE), .driver_data = 0 },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_MAGICMOUSE2), .driver_data = 0 },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_MAGICTRACKPAD), .driver_data = 0 },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_MAGICTRACKPAD2), .driver_data = 0 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_MAGICTRACKPAD2), .driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(hid, magic_mice);

static struct hid_driver magicmouse_driver = {
	.name = "magicmouse",
	.id_table = magic_mice,
	.probe = magicmouse_probe,
	.remove = magicmouse_remove,
	.raw_event = magicmouse_raw_event,
	.event = magicmouse_event,
	.input_mapping = magicmouse_input_mapping,
	.input_configured = magicmouse_input_configured,
};
module_hid_driver(magicmouse_driver);

MODULE_AUTHOR("Ricardo Rodrigues");
MODULE_AUTHOR("John Chen");
MODULE_AUTHOR("Rohit Pidaparthi");
MODULE_AUTHOR("Chase Douglas");
MODULE_AUTHOR("Michael Poole");

MODULE_DESCRIPTION("Magic Mouse 2 driver for Linux");

MODULE_LICENSE("GPL");
