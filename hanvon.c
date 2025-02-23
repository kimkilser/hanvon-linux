#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <asm/unaligned.h>

#define DRIVER_VERSION "0.7a"
#define DRIVER_AUTHOR "Ondra Havel <ondra.havel@gmail.com>"
#define DRIVER_DESC "USB Hanvon tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANVON	0x0b57
#define USB_PRODUCT_ID_AM3M		0x8528
#define USB_PRODUCT_ID_AM0605	0x8503
#define USB_PRODUCT_ID_AM0806	0x8502
#define USB_PRODUCT_ID_AM0906   0x852b
#define USB_PRODUCT_ID_AM1107	0x8505
#define USB_PRODUCT_ID_AM1209	0x8501
#define USB_PRODUCT_ID_RL0604	0x851f
#define USB_PRODUCT_ID_RL0504	0x851d
#define USB_PRODUCT_ID_GP0806	0x8039
#define USB_PRODUCT_ID_GP0806B	0x8511
#define USB_PRODUCT_ID_GP0605	0x8512
#define USB_PRODUCT_ID_GP0605A	0x803a
#define USB_PRODUCT_ID_GP0504	0x8037
#define USB_PRODUCT_ID_NXS1513	0x8030
#define USB_PRODUCT_ID_GP0906	0x8521
#define USB_PRODUCT_ID_APPIV0906	0x8532
#define USB_PRODUCT_ID_GP0604	0x8563

#define USB_AM_PACKET_LEN   10

static int lbuttons[]={BTN_0,BTN_1,BTN_2,BTN_3};   /* reported on all AMs */
static int rbuttons[]={BTN_4,BTN_5,BTN_6,BTN_7};   /* reported on AM0906, AM1107+ */

#define AM_WHEEL_THRESHOLD   4

#define AM_MAX_TILT_X   0x3f
#define AM_MAX_TILT_Y   0x7f
#define AM_MAX_PRESSURE   0x400		// 0 - 1023
#define AM0906_MAX_PRESSURE   0x800	// 0 - 2047

struct hanvon {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	int old_wheel_pos;
	char phys[32];
};

static void report_buttons(struct hanvon *hanvon, int buttons[], unsigned char dta)
{
	struct input_dev *dev = hanvon->dev;

	if((dta & 0xf0) == 0xa0) {
		input_report_key(dev, buttons[1], dta & 0x02);
		input_report_key(dev, buttons[2], dta & 0x04);
		input_report_key(dev, buttons[3], dta & 0x08);
	} else {
		if(dta <= 0x3f) {   /* slider area active */
			int diff = dta - hanvon->old_wheel_pos;
			if(abs(diff) < AM_WHEEL_THRESHOLD)
				input_report_rel(dev, REL_WHEEL, diff);

			hanvon->old_wheel_pos = dta;
			printk("report_buttons(): Slider area change: %d\n", dta);
		}
	}
}

static inline void handle_default(struct hanvon *hanvon)
{
	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;

#define AM_MAX_ABS_X   0x27de
#define AM_MAX_ABS_Y   0x1cfe

	//printk("handle_default(): 0: %02x  1: %02x  2: %02x  3: %02x  4: %02x  5: %02x  6: %02x  7: %02x  8: %02x  \n", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8]);
	switch(data[0]) {
		case 0x01:   /* button press */
			if(data[1]==0x55)   /* left side */
				report_buttons(hanvon, lbuttons,data[2]);

			if(data[3]==0xaa)   /* right side (am1107, am1209) */
				report_buttons(hanvon, rbuttons,data[4]);
			break;

		case 0x02:   /* position change */
			if((data[1] & 0xf0) != 0) {
				input_report_abs(dev, ABS_X, get_unaligned_be16(&data[2]) * 0xffff / AM_MAX_ABS_X);
				input_report_abs(dev, ABS_Y, get_unaligned_be16(&data[4]) * 0xffff / AM_MAX_ABS_Y);
				input_report_abs(dev, ABS_TILT_X, data[7] & 0x3f);
				input_report_abs(dev, ABS_TILT_Y, data[8]);
				input_report_abs(dev, ABS_PRESSURE, get_unaligned_be16(&data[6])>>6);
			}

			input_report_key(dev, BTN_LEFT, data[1] & 0x01); /* pen touches the surface */
			input_report_key(dev, BTN_RIGHT, data[1] & 0x02); /* stylus button pressed (right click) */
			input_report_key(dev, lbuttons[0], data[1] & 0x20);	/* 'eraser' button */
			break;
	}
}

static inline void handle_gp0504(struct hanvon *hanvon)
{
	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;

#define AM_MAX_ABS_X   0x27de
#define AM_MAX_ABS_Y   0x1cfe

	switch(data[0]) {
		case 0x01:   /* button press */
			if(data[1]==0x55)   /* left side */
				report_buttons(hanvon, lbuttons,data[2]);

			if(data[3]==0xaa)   /* right side (am1107, am1209) */
				report_buttons(hanvon, rbuttons,data[4]);
			break;

		case 0x02:   /* position change */
            /*printk(KERN_INFO "Hanvon Test : 1x%04x 2x%04x 4x%04x 6x%04x 7x%04x 8x%04x\n\t\tClicked : %s %i/%04x\n", data[1], data[2], data[4], data[6], data[7], data[8], data[6] > 63 ? "true" : "false", data[6], get_unaligned_be16(&data[6]));*/

			if((data[1] & 0xf0) != 0) {
				input_report_abs(dev, ABS_X, get_unaligned_be16(&data[2]) * 0xffff / AM_MAX_ABS_X);
				input_report_abs(dev, ABS_Y, get_unaligned_be16(&data[4]) * 0xffff / AM_MAX_ABS_Y);
				input_report_abs(dev, ABS_TILT_X, data[7] & 0x3f);
				input_report_abs(dev, ABS_TILT_Y, data[8]);
				input_report_abs(dev, ABS_PRESSURE, get_unaligned_be16(&data[6])>>6);
			}

            /*printk(KERN_INFO "Hanvon Test : %i\n", data[6]);*/
			input_report_key(dev, BTN_LEFT, data[6] > 68); /* pen touches the surface */
			input_report_key(dev, BTN_RIGHT, data[1] & 0x02); /* stylus button pressed (right click) */
			input_report_key(dev, lbuttons[0], data[1] & 0x20);	/* 'eraser' button */
			break;
	}
}

static inline void handle_gp0906(struct hanvon *hanvon)
{
	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;
/*
hanvon graphic pal 3, gp0906!

- data definition. this is not official...
[header, 1Byte][event type, 1Byte][x, 2Bytes][y, 2Bytes][pressure, 2Bytes][tilt, 2Bytes]
*/

	switch( data[0] ) {
		case 0x02:	/* pen event */
			if( ( data[1] & 0xe0) == 0xe0 ) {
				if( data[1] & 0x01 )	/* pressure change */
					input_report_abs(dev, ABS_PRESSURE, get_unaligned_be16(&data[6])>>6);
				
				if( data[1] & 0x04 )
					input_report_key(dev, BTN_RIGHT, data[1] & 0x2);		/* stylus button pressed (right click)  */
				
				input_report_abs(dev, ABS_X, get_unaligned_be16(&data[2]) * 0xffff / AM_MAX_ABS_X);
				input_report_abs(dev, ABS_Y, get_unaligned_be16(&data[4]) * 0xffff / AM_MAX_ABS_Y);

				input_report_key(dev, lbuttons[0], data[6]);
			} else {
				if( data[1] == 0xc2)	/* pen enters */
				{}
				
				if( data[1] == 0x80)	/* pen leaves */
				{}
			}
			break;
	
	case 0x0c:	/* key press on the tablet */
		input_report_key(dev, lbuttons[0], data[3] & 0x01);
		input_report_key(dev, lbuttons[1], data[3] & 0x02);
		input_report_key(dev, lbuttons[2], data[3] & 0x04);
		input_report_key(dev, lbuttons[3], data[3] & 0x08);
		break;
	}
}

static inline void handle_appiv0906(struct hanvon *hanvon)
{
	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;

#define APPIV_XMAX	0x5750
#define APPIV_YMAX	0x3692

	switch(data[0]) {
		case 0x01:	/* pen button event */
			input_report_abs(dev, ABS_X, get_unaligned_le16(&data[2]) * 0xffff / APPIV_XMAX);
			input_report_abs(dev, ABS_Y, get_unaligned_le16(&data[4]) * 0xffff / APPIV_YMAX);
			input_report_key(dev, BTN_LEFT, data[1] & 0x01); /* pen touches the surface */
			input_report_key(dev, BTN_RIGHT, data[1] & 0x02); /* stylus button pressed (right click) */
			input_report_key(dev, BTN_MIDDLE, data[1] & 0x04); /* stylus button pressed (right click) */
			input_report_key(dev, BTN_0, data[1] & 0x08); /* stylus button pressed (right click) */
			break;

		case 0x02:	/* pen event */
			input_report_abs(dev, ABS_X, get_unaligned_be16(&data[2]) * 0xffff / APPIV_XMAX);
			input_report_abs(dev, ABS_Y, get_unaligned_be16(&data[4]) * 0xffff / APPIV_YMAX);

			if(data[1] & 1)
				input_report_abs(dev, ABS_PRESSURE, get_unaligned_be16(&data[6])>>6);


			break;

		case 0x0c:	/* tablet button event */
			input_report_key(dev, BTN_1, data[3] & 0x01);
			input_report_key(dev, BTN_2, data[3] & 0x02);
			input_report_key(dev, BTN_3, data[3] & 0x04);
			input_report_key(dev, BTN_4, data[3] & 0x08);
			input_report_key(dev, BTN_5, data[3] & 0x10);
			input_report_key(dev, BTN_6, data[3] & 0x20);
			input_report_key(dev, BTN_7, data[3] & 0x40);
			break;
	}
}

// Support for Art Master IV / AM0906
static inline void handle_AM0906(struct hanvon *hanvon)
{
#define AM0906_XMAX	0x5750
#define AM0906_YMAX	0x3692
	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;

	// BUTTON Values
	// data[1]: 0x10 no pen buttons pressed
	// data[1]: 0x10+1 low button, 
	// data[1]: 0x10+3 high button
	switch(data[0]) {
		case 0x01:	/* pen button event */
			input_report_abs(dev, ABS_X, get_unaligned_le16(&data[2]) * 0xffff / AM0906_XMAX);
			input_report_abs(dev, ABS_Y, get_unaligned_le16(&data[4]) * 0xffff / AM0906_YMAX);
			input_report_key(dev, BTN_LEFT, data[1] & 0x01); /* pen touches the surface */
			input_report_key(dev, BTN_RIGHT, data[1] & 0x02); /* right stylus button pressed (emulate right mouse click) */
			input_report_key(dev, BTN_MIDDLE, data[1] & 0x04); /* middle stylus button pressed (emulate right mouse click) */
			input_report_key(dev, BTN_0, data[1] & 0x08); /* stylus button pressed (emulate right mouse click) */
			
			/* pressure change */
			input_report_abs(dev, ABS_PRESSURE, (data[7]<<8|data[6]));
			break;

		case 0x0c:	/* tablet button event */
			/*
			 * ring sensor:
			 * data[1] (byte): 0x81, 0x82, 0x83 .. 0xc3, 0xc4, 0xc5 (wraps)
			 * data[2]: 0/1 for ring sensor center button
			 * 
			 * Buttons 0 - 7
			 * data[3]:
			 * 0 - 0x80 (Lowest)
			 * 1 - 0x40
			 * 2 - 0x20
			 * 3 - 0x10
			 * 4 - 0x08
			 * 5 - 0x04
			 * 6 - 0x02
			 * 7 - 0x01
			 */
			input_report_key(dev, BTN_0, data[3] & 0x01);
			input_report_key(dev, BTN_1, data[3] & 0x02);
			input_report_key(dev, BTN_2, data[3] & 0x04);
			input_report_key(dev, BTN_3, data[3] & 0x08);
			input_report_key(dev, BTN_4, data[3] & 0x10);
			input_report_key(dev, BTN_5, data[3] & 0x20);
			input_report_key(dev, BTN_6, data[3] & 0x40);
			input_report_key(dev, BTN_7, data[3] & 0x80);
			break;
			
		default:
			//Should never reach this, data[0] should be 0x01 or 0x0c
			printk("hanvon.c: handle_AM0906(): warning, unhandled case value: %x\n", data[0]);
			break;
	}
}


// GP0604
static inline void handle_gp0604(struct hanvon *hanvon) 
{

#define GP0604_XMAX	0x27de
#define GP0604_YMAX	0x1cfe

    /*printk(KERN_INFO "Hanvon Test : 1x%04x 2x%04x 4x%04x 6x%04x 7x%04x 8x%04x\n\t\tClicked : %s %i/%04x\n", data[1], data[2], data[4], data[6], data[7], data[8], data[6] > 63 ? "true" : "false", data[6], get_unaligned_be16(&data[6]));*/

	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;

	/*
	hanvon graphic pal 4, gp0604!

	- data definition. this is not official...
	[header, 1Byte][event type, 1Byte][x, 2Bytes][y, 2Bytes][pressure, 2Bytes][tilt, 2Bytes]
	*/

	//printk(KERN_INFO "Hanvon Test : 0x%04x 1x%04x 2x%04x 4x%04x 6x%04x 7x%04x 8x%04x\n\t\tClicked : %s %i/%04x\n", data[0], data[1], data[2], data[4], data[6], data[7], data[8], data[6] > 63 ? "true" : "false", data[6], get_unaligned_be16(&data[6]));

	// printk(KERN_INFO "Hanvon Test : 0x%04x 1x%04x 2x%04x 3x%04x 4x%04x 6x%04x 7x%04x 8x%04x\n\t\tClicked : %s %i/%04x\n", data[0], data[1], data[2], data[3],data[4], data[6], data[7], data[8], data[6] > 63 ? "true" : "false", data[6], get_unaligned_be16(&data[6]));

	switch( data[0] ) {

	case 0x01:	/* pen button event */
		input_report_abs(dev, ABS_X, get_unaligned_le16(&data[2]) * 0xffff / GP0604_XMAX);
		input_report_abs(dev, ABS_Y, get_unaligned_le16(&data[4]) * 0xffff / GP0604_YMAX);
		input_report_key(dev, BTN_LEFT, data[1] & 0x01); /* pen touches the surface */
		input_report_key(dev, BTN_RIGHT, data[1] & 0x02); /* right stylus button pressed (emulate right mouse click) */
		input_report_key(dev, BTN_MIDDLE, data[1] & 0x04); /* middle stylus button pressed (emulate right mouse click) */
		input_report_key(dev, BTN_0, data[1] & 0x08); /* stylus button pressed (emulate right mouse click) */
		
		/* pressure change */
		input_report_abs(dev, ABS_PRESSURE, (data[7]<<8|data[6]));
		break;
	
	case 0x0c:	/* key press on the tablet */
		input_report_key(dev, BTN_0, data[3] & 0x01);
		input_report_key(dev, BTN_1, data[3] & 0x02);
		input_report_key(dev, BTN_2, data[3] & 0x04);
		input_report_key(dev, BTN_3, data[3] & 0x08);
		input_report_key(dev, BTN_4, data[3] & 0x10);
		break;
	}
}


static void hanvon_irq(struct urb *urb)
{
	struct hanvon *hanvon = urb->context;
	int retval;

	switch (urb->status) {
		case 0:
			/* success */
			switch( hanvon->usbdev->descriptor.idProduct ) {
				case USB_PRODUCT_ID_GP0906:
					handle_gp0906(hanvon);
					break;
				case USB_PRODUCT_ID_AM0906:
					handle_AM0906(hanvon);
					break;
				case USB_PRODUCT_ID_APPIV0906:
					handle_appiv0906(hanvon);
					break;
				case USB_PRODUCT_ID_GP0504:
				    handle_gp0504(hanvon);
				    break;
				case USB_PRODUCT_ID_GP0604:
					handle_gp0604(hanvon);
					break;
				default:
					handle_default(hanvon);
					break;
				}
			break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/* this urb is terminated, clean up */
			printk("%s - urb shutting down with status: %d", __func__, urb->status);
			return;
		default:
			printk("%s - nonzero urb status received: %d", __func__, urb->status);
				break;
	}


	input_sync(hanvon->dev);

	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		printk("%s - usb_submit_urb failed with result %d", __func__, retval);
}

static struct usb_device_id hanvon_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM3M) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM1209) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM1107) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM0906) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM0806) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM0605) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_RL0604) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_RL0504) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_GP0806) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_GP0806B) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_GP0605) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_GP0605A) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_GP0504) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_NXS1513) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_GP0906) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_APPIV0906)},
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_GP0604) },
	{}
};

MODULE_DEVICE_TABLE(usb, hanvon_ids);

static int hanvon_open(struct input_dev *dev)
{
	struct hanvon *hanvon = input_get_drvdata(dev);

	hanvon->old_wheel_pos = -AM_WHEEL_THRESHOLD-1;
	hanvon->irq->dev = hanvon->usbdev;
	if (usb_submit_urb(hanvon->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void hanvon_close(struct input_dev *dev)
{
	struct hanvon *hanvon = input_get_drvdata(dev);

	usb_kill_urb(hanvon->irq);
}

static int hanvon_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct hanvon *hanvon;
	struct input_dev *input_dev;
	int error = -ENOMEM, i;

	hanvon = kzalloc(sizeof(struct hanvon), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!hanvon || !input_dev)
		goto fail1;

	hanvon->data = (unsigned char *)usb_alloc_coherent(dev, USB_AM_PACKET_LEN, GFP_KERNEL, &hanvon->data_dma);
	if (!hanvon->data)
		goto fail1;

	hanvon->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!hanvon->irq)
		goto fail2;

	hanvon->usbdev = dev;
	hanvon->dev = input_dev;

	usb_make_path(dev, hanvon->phys, sizeof(hanvon->phys));
	strlcat(hanvon->phys, "/input0", sizeof(hanvon->phys));

	input_dev->name = "Hanvon tablet";
	input_dev->phys = hanvon->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, hanvon);

	input_dev->open = hanvon_open;
	input_dev->close = hanvon_close;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_PEN) | BIT_MASK(BTN_TOUCH);
	input_dev->keybit[BIT_WORD(BTN_LEFT)] |= BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	for(i=0;i<sizeof(lbuttons)/sizeof(lbuttons[0]);i++)
	  __set_bit(lbuttons[i], input_dev->keybit);
	for(i=0;i<sizeof(rbuttons)/sizeof(rbuttons[0]);i++)
	  __set_bit(rbuttons[i], input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, 0xffff, 4, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0xffff, 4, 0);
	input_set_abs_params(input_dev, ABS_TILT_X, 0, AM_MAX_TILT_X, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_Y, 0, AM_MAX_TILT_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, AM_MAX_PRESSURE, 0, 0);
	input_set_capability(input_dev, EV_REL, REL_WHEEL);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	usb_fill_int_urb(hanvon->irq, dev,
			usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			hanvon->data, USB_AM_PACKET_LEN,
			hanvon_irq, hanvon, endpoint->bInterval);
	hanvon->irq->transfer_dma = hanvon->data_dma;
	hanvon->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(hanvon->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, hanvon);
	return 0;

fail3:   usb_free_urb(hanvon->irq);
fail2:   usb_free_coherent(dev, USB_AM_PACKET_LEN, hanvon->data, hanvon->data_dma);
fail1:   input_free_device(input_dev);
	kfree(hanvon);
	return error;
}

static void hanvon_disconnect(struct usb_interface *intf)
{
	struct hanvon *hanvon = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (hanvon) {
		usb_kill_urb(hanvon->irq);
		input_unregister_device(hanvon->dev);
		usb_free_urb(hanvon->irq);
		usb_free_coherent(interface_to_usbdev(intf), USB_AM_PACKET_LEN, hanvon->data, hanvon->data_dma);
		kfree(hanvon);
	}
}

static struct usb_driver hanvon_driver = {
	.name = "hanvon",
	.probe = hanvon_probe,
	.disconnect = hanvon_disconnect,
	.id_table =   hanvon_ids,
};

static int __init hanvon_init(void)
{
	int rv;

	if((rv = usb_register(&hanvon_driver)) != 0)
		return rv;

	printk(DRIVER_DESC " " DRIVER_VERSION "\n");

	return 0;
}

static void __exit hanvon_exit(void)
{
	usb_deregister(&hanvon_driver);
}

module_init(hanvon_init);
module_exit(hanvon_exit);
