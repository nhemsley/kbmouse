/*
 *  Virtual mouse driver - Alessandro Pira <writeme@alessandropira.org>
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/sched.h> // For jiffies

#include "vmouse.h"

MODULE_AUTHOR("Alessandro Pira <writeme@alessandropira.org>");
MODULE_DESCRIPTION("Virtual mouse driver module");
MODULE_LICENSE("GPL");

#define VMOUSE_NONE -1
#define VMOUSE_UL 0
#define VMOUSE_UP 1
#define VMOUSE_UR 2
#define VMOUSE_LF 3
#define VMOUSE_RG 4
#define VMOUSE_DL 5
#define VMOUSE_DN 6
#define VMOUSE_DR 7
#define VMOUSE_B1 8
#define VMOUSE_B2 9

#define VMOUSE_GET(var,dir) ( ((var) & (1<<(dir))) != 0 )
#define VMOUSE_SET(var,dir) ((var) |= (1<<(dir)))
#define VMOUSE_UNSET(var,dir) ((var) &= ~(1<<(dir)))
#define VMOUSE_SET_TO(var,dir,value) ( (value)?VMOUSE_SET(var,dir):VMOUSE_UNSET(var,dir) )

#define DEVICE_NAME "virtual_mouse"

#define LOG_PRE KBUILD_MODNAME

#ifdef VEROSE
  #define HPRINTK(fmt,args...) printk(KERN_DEBUG "%s %s: " fmt,LOG_PRE,__FUNCTION__,args)
#else
  #define HPRINTK(fmt,args...)
#endif

#ifdef LOG
  #define DPRINTK(fmt,args...) printk(KERN_INFO "%s %s: " fmt,LOG_PRE,__FUNCTION__,args)
#else
  #define DPRINTK(fmt,args...)
#endif

static struct input_dev *vmouse_dev;
static int vmouse_ctrl_state;
static unsigned short vmouse_keys=0;
static int vmouse_accel=0;
static long unsigned int last_cmd_jiffies=0;

static inline int get_index(int code)
{
	switch(code)
	{
	case VMOUSE_SC_UL:
		return VMOUSE_UL;
	case VMOUSE_SC_UP:
		return VMOUSE_UP;
	case VMOUSE_SC_UR:
		return VMOUSE_UR;
	case VMOUSE_SC_LF:
		return VMOUSE_LF;
	case VMOUSE_SC_RG:
		return VMOUSE_RG;
	case VMOUSE_SC_DL:
		return VMOUSE_DL;
	case VMOUSE_SC_DN:
		return VMOUSE_DN;
	case VMOUSE_SC_DR:
		return VMOUSE_DR;
	case VMOUSE_SC_B1:
		return VMOUSE_B1;
	case VMOUSE_SC_B2:
		return VMOUSE_B2;
	}
	return VMOUSE_NONE;
}

static inline int is_code_managed(int code)
{
	return
	   	(code == VMOUSE_SC_UL) ||
		(code == VMOUSE_SC_UP) ||
		(code == VMOUSE_SC_UR) ||
		(code == VMOUSE_SC_LF) ||
		(code == VMOUSE_SC_RG) ||
		(code == VMOUSE_SC_DL) ||
		(code == VMOUSE_SC_DN) ||
		(code == VMOUSE_SC_DR) ||
		(code == VMOUSE_SC_B1) ||
		(code == VMOUSE_SC_B2);
}

static inline int get_rel_y(int code, int accel)
{
	switch(code)
	{
	case VMOUSE_SC_UL:
	case VMOUSE_SC_UP:
	case VMOUSE_SC_UR:
		return -(STEP + accel);
	case VMOUSE_SC_DL:
	case VMOUSE_SC_DN:
	case VMOUSE_SC_DR:
		return STEP + accel;
	}
	return 0;
}
static inline int get_rel_x(int code, int accel)
{
	switch(code)
	{
	case VMOUSE_SC_UL:
	case VMOUSE_SC_LF:
	case VMOUSE_SC_DL:
		return -(STEP + accel);
	case VMOUSE_SC_UR:
	case VMOUSE_SC_RG:
	case VMOUSE_SC_DR:
		return STEP + accel;
	}
	return 0;
}

/* BEWARE: returning 1 will block that scancode. This could completely
   disable your keyboard if not used properly */
static int handle_vmouse_scancode(int code, int pressed)
{
	int rx, ry, b1, b2;
	int sync;

	if (code == VMOUSE_SC_CONTROL)
	{
		HPRINTK("VMCTRL: %d\n", pressed);
		vmouse_ctrl_state = pressed;
		return 0;
	}

	if (!vmouse_ctrl_state)
	{
		vmouse_accel = 0;
		vmouse_keys = 0;
		return 0;
	}

	if (!is_code_managed(code))
	{
		return 0;
	}

	HPRINTK("J: %lu LAST: %lu DIFF: %lu\n", jiffies, last_cmd_jiffies, jiffies - last_cmd_jiffies);
	if ((last_cmd_jiffies != 0) && (jiffies - last_cmd_jiffies >= 100)) { // 1 sec.
		vmouse_accel = 0;
	}
	last_cmd_jiffies = jiffies;

	sync = 0;
	rx = ry = 0;

	b1 = VMOUSE_GET(vmouse_keys, VMOUSE_B1);
	b2 = VMOUSE_GET(vmouse_keys, VMOUSE_B2);

	if (pressed) {
		HPRINTK("ACCEL: %d\n", vmouse_accel);
		rx = get_rel_x(code, vmouse_accel);
		ry = get_rel_y(code, vmouse_accel);
	}

	switch (code) {
	case VMOUSE_SC_B1:
		b1 = pressed;
		break;
	case VMOUSE_SC_B2:
		b2 = pressed;
		break;
	}

	if ((rx != 0) || (ry != 0))
	{
		HPRINTK("RX: %d RY: %d\n", rx, ry);
		input_report_rel(vmouse_dev, REL_X, rx);
		input_report_rel(vmouse_dev, REL_Y, ry);
		sync = 1;
		if ((++vmouse_accel) > MAX_ACCEL)
		{
			vmouse_accel = MAX_ACCEL;
		}
	}

	if (b1 != VMOUSE_GET(vmouse_keys, VMOUSE_B1))
	{
		DPRINTK("B1: %d\n", b1);
		input_report_key(vmouse_dev, BTN_LEFT, b1);
		sync = 1;
	}

	if (b2 != VMOUSE_GET(vmouse_keys, VMOUSE_B2))
	{
		DPRINTK("B2: %d\n", b2);
		input_report_key(vmouse_dev, BTN_RIGHT, b2);
		sync = 1;
	}

	if (sync)
	{
		input_sync(vmouse_dev);
	}

	VMOUSE_SET_TO(vmouse_keys,get_index(code),pressed);

	return 1;
}

/********** Device Operations **********/

static int vmouse_open(struct input_dev *dev) { return 0; }
static void vmouse_close(struct input_dev *dev) { }

/********** Keyboard handling **********/

static bool vmouse_kb_filter(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	if (type != EV_KEY)
	{
		return 0;
	}

	HPRINTK("Filter. Dev: %s, Type: %d, Code: %d, Value: %d\n",
	       dev_name(&handle->dev->dev), type, code, value);

	return handle_vmouse_scancode(code, value);
}

static int vmouse_kb_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "vmouse_kb";

	error = input_register_handle(handle);
	if (error)
		goto err_free_handle;

	error = input_open_device(handle);
	if (error)
		goto err_unregister_handle;

	DPRINTK("Connected device: %s (%s at %s)\n",
	       dev_name(&dev->dev),
	       dev->name ?: "unknown",
	       dev->phys ?: "unknown");

	return 0;

 err_unregister_handle:
	input_unregister_handle(handle);
 err_free_handle:
	kfree(handle);
	return error;
}

static void vmouse_kb_disconnect(struct input_handle *handle)
{
	DPRINTK("Disconnect %s\n", handle->dev->name);

	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id vmouse_kb_ids[] = {
	{ .driver_info = 1 },	/* Matches all devices */
	{ },			/* Terminating zero entry */
};

MODULE_DEVICE_TABLE(input, vmouse_kb_ids);

static struct input_handler vmouse_kb_handler = {
	.filter =	vmouse_kb_filter,
	.connect =	vmouse_kb_connect,
	.disconnect =	vmouse_kb_disconnect,
	.name =		DEVICE_NAME,
	.id_table =	vmouse_kb_ids,
};

static int __init vmouse_init(void)
{
	int error;

	vmouse_dev = input_allocate_device();
	if (!vmouse_dev)
	{
		DPRINTK("Registering device %s failed (no memory)\n",DEVICE_NAME);
		error = -ENOMEM;
		goto err_exit;
	}

	vmouse_dev->name = DEVICE_NAME;
	vmouse_dev->phys = "vmouse/input0";
	vmouse_dev->id.bustype = BUS_VIRTUAL;
	vmouse_dev->id.vendor  = 0x0000;
	vmouse_dev->id.product = 0x0000;
	vmouse_dev->id.version = 0x0000;

	vmouse_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	vmouse_dev->keybit[BIT_WORD(BTN_LEFT)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	vmouse_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	vmouse_dev->open  = vmouse_open;
	vmouse_dev->close = vmouse_close;

	error = input_register_device(vmouse_dev);
	if (error != 0)
	{
		DPRINTK("Registering %s failed (%d)\n",DEVICE_NAME,error);
		goto err_free_dev;
	}
	else
	{
		DPRINTK("Registered %s successfully\n",DEVICE_NAME);
	}

	error = input_register_handler(&vmouse_kb_handler);
	if (error)
	{
		DPRINTK("Registering input handler failed with (%d)\n",error);
		goto err_unregister_dev;
	}

	return 0;

err_unregister_dev:
	input_unregister_device(vmouse_dev);

err_free_dev:
	input_free_device(vmouse_dev);

err_exit:
	return error;
}

static void __exit vmouse_exit(void)
{
	input_unregister_handler(&vmouse_kb_handler);
	input_unregister_device(vmouse_dev);
	input_free_device(vmouse_dev);
}

module_init(vmouse_init);
module_exit(vmouse_exit);
