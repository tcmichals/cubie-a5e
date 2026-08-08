// SPDX-License-Identifier: GPL-2.0
/*
 ****************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief bt uses the sdio interface function definitions
 *
 ****************************************************************************
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "aic_btsdio.h"
#include "rwnx_msg_tx.h"

static spinlock_t queue_lock;

static inline struct sk_buff *bt_skb_alloc(unsigned int len, gfp_t how)
{
	struct sk_buff *skb;

	skb = alloc_skb(len + BT_SKB_RESERVE, how);
	if (skb) {
		skb_reserve(skb, BT_SKB_RESERVE);
		bt_cb(skb)->incoming = 0;
	}
	return skb;
}

static spinlock_t queue_lock;
static spinlock_t dlfw_lock;
static u16 dlfw_dis_state;

/* Global parameters for bt usb char driver */
#define BT_CHAR_DEVICE_NAME "aicbt_dev"
/* mutex for btchr */
struct mutex btchr_mutex;
static struct sk_buff_head btchr_readq;
static wait_queue_head_t btchr_read_wait;
static wait_queue_head_t bt_dlfw_wait;
static int bt_char_dev_registered;
static dev_t bt_devid;              /* bt char device number */
static struct cdev bt_char_dev;     /* bt character device structure */
static struct class *bt_char_class; /* device class for usb char driver */
static int bt_reset;
/* HCI device & lock */
DEFINE_RWLOCK(hci_dev_lock);

struct hci_dev *ghdev;

static struct sk_buff *aic_skb_queue[QUEUE_SIZE];
static int aic_skb_queue_front;
static int aic_skb_queue_rear;

static inline int check_set_dlfw_state_value(uint16_t change_value)
{
	spin_lock(&dlfw_lock);
	if (!dlfw_dis_state)
		dlfw_dis_state = change_value;
	spin_unlock(&dlfw_lock);
	return dlfw_dis_state;
}

static inline void set_dlfw_state_value(uint16_t change_value)
{
	spin_lock(&dlfw_lock);
	dlfw_dis_state = change_value;
	spin_unlock(&dlfw_lock);
}

static void print_acl(struct sk_buff *skb, int direction)
{
#if PRINT_ACL_DATA
	u16 *handle = (u16 *)(skb->data);
	u16 len = *(handle + 1);

	AICBT_INFO("aic %s: direction %d, handle %04x, len %d", __func__, direction,
		   *handle, len);
#endif
}

static void print_sco(struct sk_buff *skb, int direction)
{
#if PRINT_SCO_DATA
	uint wlength = skb->len;
	u16 *handle = (u16 *)(skb->data);
	u8 len = *(u8 *)(handle + 1);

	AICBT_INFO("aic %s: direction %d, handle %04x, len %d,wlength %d", __func__,
		   direction, *handle, len, wlength);
#endif
}

int bt_bypass_event(struct sk_buff *skb)
{
	int ret = 0;
	u8 *opcode = (u8 *)(skb->data);
	// debug trace
	// printk("bypass_event
	// %x,%x,%x,%x,%x\r\n",opcode[0],opcode[1],opcode[2],opcode[3],opcode[4]);

	switch (opcode[1]) {
	case HCI_EV_LE_Meta: {
		u8 subevent_code;

		subevent_code = opcode[3];
		switch (subevent_code) {
		case HCI_BLE_ADV_PKT_RPT_EVT:
		case HCI_LE_EXTENDED_ADVERTISING_REPORT_EVT: {
			if (aic_queue_cnt() > (QUEUE_SIZE - 490)) {
				pr_warn("more adv report bypass\r\n");
				ret = 1;
			}
		} break;
		}
	} break;
	default:
		break;
	}
	return ret;
}

int bt_sdio_recv(u8 *data, u32 data_len)
{
	struct sk_buff *skb;
	int type = data[0];
	struct hci_dev *hdev;
	u32 len = data_len;

	hdev = hci_dev_get(0);
	if (!hdev) {
		AICWFDBG(LOGERROR, "%s: Failed to get hci dev[NULL]", __func__);
		return -ENODEV;
	}

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		AICWFDBG(LOGERROR, "alloc skb fail %s\n", __func__);
	memcpy(skb_put(skb, len), data, len);
	if (bt_bypass_event(skb)) {
		kfree_skb(skb);
		return 0;
	}
	// bt_data_dump("bt_skb", skb, skb->len); //debug trace

	if (aic_enqueue(skb) < 0) {
		kfree_skb(skb);
	} else {
		// printk("wake up\n"); //debug trace
		wake_up_interruptible(&btchr_read_wait);
	}
	return 0;
}

static int bypass_event(struct sk_buff *skb)
{
	int ret = 0;
	u8 *opcode = (u8 *)(skb->data);

	switch (*opcode) {
#ifdef CONFIG_SUPPORT_VENDOR_APCF
	case HCI_EV_CMD_COMPLETE: {
		u16 sub_opcpde;

		sub_opcpde = ((u16)opcode[3] | (u16)(opcode[4]) << 8);
		if (sub_opcpde == 0xfd57) {
			if (vendor_apcf_sent_done) {
				vendor_apcf_sent_done--;
				pr_warn("apcf bypass\r\n");
				ret = 1;
			}
		}
	} break;
#endif // CONFIG_SUPPORT_VENDOR_APCF
	case HCI_EV_LE_Meta: {
		u8 subevent_code;

		subevent_code = opcode[2];
		switch (subevent_code) {
		case HCI_BLE_ADV_PKT_RPT_EVT:
		case HCI_LE_EXTENDED_ADVERTISING_REPORT_EVT: {
			if (aic_queue_cnt() > (QUEUE_SIZE - 100)) {
				pr_warn("more adv report bypass\r\n");
				ret = 1;
			}
		} break;
		}
	} break;
	default:
		break;
	}
	return ret;
}

static void print_event(struct sk_buff *skb)
{
#if PRINT_CMD_EVENT
	u8 *opcode = (u8 *)(skb->data);

	pr_info("aic %s ", __func__);
	switch (*opcode) {
	case HCI_EV_INQUIRY_COMPLETE:
		pr_info("HCI_EV_INQUIRY_COMPLETE");
		break;
	case HCI_EV_INQUIRY_RESULT:
		pr_info("HCI_EV_INQUIRY_RESULT");
		break;
	case HCI_EV_CONN_COMPLETE:
		pr_info("HCI_EV_CONN_COMPLETE");
		break;
	case HCI_EV_CONN_REQUEST:
		pr_info("HCI_EV_CONN_REQUEST");
		break;
	case HCI_EV_DISCONN_COMPLETE:
		pr_info("HCI_EV_DISCONN_COMPLETE");
		break;
	case HCI_EV_AUTH_COMPLETE:
		pr_info("HCI_EV_AUTH_COMPLETE");
		break;
	case HCI_EV_REMOTE_NAME:
		pr_info("HCI_EV_REMOTE_NAME");
		break;
	case HCI_EV_ENCRYPT_CHANGE:
		pr_info("HCI_EV_ENCRYPT_CHANGE");
		break;
	case HCI_EV_CHANGE_LINK_KEY_COMPLETE:
		pr_info("HCI_EV_CHANGE_LINK_KEY_COMPLETE");
		break;
	case HCI_EV_REMOTE_FEATURES:
		pr_info("HCI_EV_REMOTE_FEATURES");
		break;
	case HCI_EV_REMOTE_VERSION:
		pr_info("HCI_EV_REMOTE_VERSION");
		break;
	case HCI_EV_QOS_SETUP_COMPLETE:
		pr_info("HCI_EV_QOS_SETUP_COMPLETE");
		break;
	case HCI_EV_CMD_COMPLETE:
		pr_info("HCI_EV_CMD_COMPLETE");
		break;
	case HCI_EV_CMD_STATUS:
		pr_info("HCI_EV_CMD_STATUS");
		break;
	case HCI_EV_ROLE_CHANGE:
		pr_info("HCI_EV_ROLE_CHANGE");
		break;
	case HCI_EV_NUM_COMP_PKTS:
		pr_info("HCI_EV_NUM_COMP_PKTS");
		break;
	case HCI_EV_MODE_CHANGE:
		pr_info("HCI_EV_MODE_CHANGE");
		break;
	case HCI_EV_PIN_CODE_REQ:
		pr_info("HCI_EV_PIN_CODE_REQ");
		break;
	case HCI_EV_LINK_KEY_REQ:
		pr_info("HCI_EV_LINK_KEY_REQ");
		break;
	case HCI_EV_LINK_KEY_NOTIFY:
		pr_info("HCI_EV_LINK_KEY_NOTIFY");
		break;
	case HCI_EV_CLOCK_OFFSET:
		pr_info("HCI_EV_CLOCK_OFFSET");
		break;
	case HCI_EV_PKT_TYPE_CHANGE:
		pr_info("HCI_EV_PKT_TYPE_CHANGE");
		break;
	case HCI_EV_PSCAN_REP_MODE:
		pr_info("HCI_EV_PSCAN_REP_MODE");
		break;
	case HCI_EV_INQUIRY_RESULT_WITH_RSSI:
		pr_info("HCI_EV_INQUIRY_RESULT_WITH_RSSI");
		break;
	case HCI_EV_REMOTE_EXT_FEATURES:
		pr_info("HCI_EV_REMOTE_EXT_FEATURES");
		break;
	case HCI_EV_SYNC_CONN_COMPLETE:
		pr_info("HCI_EV_SYNC_CONN_COMPLETE");
		break;
	case HCI_EV_SYNC_CONN_CHANGED:
		pr_info("HCI_EV_SYNC_CONN_CHANGED");
		break;
	case HCI_EV_SNIFF_SUBRATE:
		pr_info("HCI_EV_SNIFF_SUBRATE");
		break;
	case HCI_EV_EXTENDED_INQUIRY_RESULT:
		pr_info("HCI_EV_EXTENDED_INQUIRY_RESULT");
		break;
	case HCI_EV_IO_CAPA_REQUEST:
		pr_info("HCI_EV_IO_CAPA_REQUEST");
		break;
	case HCI_EV_SIMPLE_PAIR_COMPLETE:
		pr_info("HCI_EV_SIMPLE_PAIR_COMPLETE");
		break;
	case HCI_EV_REMOTE_HOST_FEATURES:
		pr_info("HCI_EV_REMOTE_HOST_FEATURES");
		break;
	default:
		pr_info("unknown event");
		break;
	}
	pr_info("\n");
#endif
}

static inline ssize_t sdio_put_user(struct sk_buff *skb, char __user *buf,
				    int count)
{
	char __user *ptr = buf;
	int len = min_t(unsigned int, skb->len, count);

	if (copy_to_user(ptr, skb->data, len))
		return -EFAULT;

	return len;
}

int aic_enqueue(struct sk_buff *skb)
{
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&queue_lock, flags);
	if (aic_skb_queue_front == (aic_skb_queue_rear + 1) % QUEUE_SIZE) {
		/*
		 * If queue is full, current solution is to drop
		 * the following entries.
		 */
		AICBT_WARN("%s: Queue is full, entry will be dropped", __func__);
		ret = -1;
	} else {
		aic_skb_queue[aic_skb_queue_rear] = skb;

		aic_skb_queue_rear++;
		aic_skb_queue_rear %= QUEUE_SIZE;
	}
	spin_unlock_irqrestore(&queue_lock, flags);
	return ret;
}

static struct sk_buff *aic_dequeue_try(unsigned int deq_len)
{
	struct sk_buff *skb;
	struct sk_buff *skb_copy;
	unsigned long flags = 0;

	spin_lock_irqsave(&queue_lock, flags);
	if (aic_skb_queue_front == aic_skb_queue_rear) {
		AICBT_WARN("%s: Queue is empty", __func__);
		spin_unlock_irqrestore(&queue_lock, flags);
		return NULL;
	}

	skb = aic_skb_queue[aic_skb_queue_front];
	if (deq_len >= skb->len) {
		aic_skb_queue_front++;
		aic_skb_queue_front %= QUEUE_SIZE;

		/*
		 * Return skb addr to be dequeued, and the caller
		 * should free the skb eventually.
		 */
		spin_unlock_irqrestore(&queue_lock, flags);
		return skb;
	}
	skb_copy = pskb_copy(skb, GFP_ATOMIC);
	skb_pull(skb, deq_len);
	/* Return its copy to be freed */
	spin_unlock_irqrestore(&queue_lock, flags);
	return skb_copy;
}

static inline int is_queue_empty(void)
{
	return (aic_skb_queue_front == aic_skb_queue_rear) ? 1 : 0;
}

void aic_clear_queue(void)
{
	struct sk_buff *skb;
	unsigned long flags = 0;

	spin_lock_irqsave(&queue_lock, flags);
	while (!is_queue_empty()) {
		skb = aic_skb_queue[aic_skb_queue_front];
		aic_skb_queue[aic_skb_queue_front] = NULL;
		aic_skb_queue_front++;
		aic_skb_queue_front %= QUEUE_SIZE;
		if (skb)
			kfree_skb(skb);
	}
	spin_unlock_irqrestore(&queue_lock, flags);
}

int aic_queue_cnt(void)
{
	int ret_cnt = 0;
	unsigned long flags = 0;

	spin_lock_irqsave(&queue_lock, flags);
	if (is_queue_empty()) {
		ret_cnt = 0;
	} else {
		if (aic_skb_queue_rear > aic_skb_queue_front)
			ret_cnt = aic_skb_queue_rear - aic_skb_queue_front;
		else
			ret_cnt = aic_skb_queue_rear + QUEUE_SIZE - aic_skb_queue_front;
	}
	spin_unlock_irqrestore(&queue_lock, flags);
	return ret_cnt;
}

/*
 * AicSemi - Integrate from hci_core.c
 */

/* Get HCI device by index.
 * Device is held on return.
 */
struct hci_dev *hci_dev_get(int index)
{
	if (index != 0)
		return NULL;

	return ghdev;
}

/* ---- HCI ioctl helpers ---- */
static int hci_dev_open(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	AICBT_DBG("%s: dev %d", __func__, dev);

	hdev = hci_dev_get(dev);
	if (!hdev) {
		AICBT_ERR("%s: Failed to get hci dev[Null]", __func__);
		return -ENODEV;
	}

done:
	return ret;
}

static int hci_dev_do_close(struct hci_dev *hdev)
{
	/* Clear flags */
	hdev->flags = 0;
	return 0;
}

static int hci_dev_close(__u16 dev)
{
	struct hci_dev *hdev;
	int err;

	hdev = hci_dev_get(dev);
	if (!hdev) {
		AICBT_ERR("%s: failed to get hci dev[Null]", __func__);
		return -ENODEV;
	}

	err = hci_dev_do_close(hdev);

	return err;
}

#if CONFIG_BLUEDROID
static struct hci_dev *hci_alloc_dev(void)
{
	struct hci_dev *hdev;

	hdev = kzalloc_obj(*hdev, GFP_KERNEL);
	if (!hdev)
		return NULL;

	return hdev;
}

/* Free HCI device */
static void hci_free_dev(struct hci_dev *hdev)
{
	kfree(hdev);
}

/* Register HCI device */
static int hci_register_dev(struct hci_dev *hdev)
{
	int i, id;

	AICBT_DBG("%s: %p name %s bus %d", __func__, hdev, hdev->name, hdev->bus);
	/* Do not allow HCI_AMP devices to register at index 0,
	 * so the index can be used as the AMP controller ID.
	 */
	id = (hdev->dev_type == HCI_BREDR) ? 0 : 1;

	write_lock(&hci_dev_lock);

	sprintf(hdev->name, "hci%d", id);
	hdev->id = id;
	hdev->flags = 0;
	hdev->dev_flags = 0;
	mutex_init(&hdev->lock);

	AICBT_DBG("%s: id %d, name %s", __func__, hdev->id, hdev->name);

	for (i = 0; i < NUM_REASSEMBLY; i++)
		hdev->reassembly[i] = NULL;

	memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));
	atomic_set(&hdev->promisc, 0);

	if (ghdev) {
		write_unlock(&hci_dev_lock);
		AICBT_ERR("%s: Hci device has been registered already", __func__);
		return -1;
	}
	ghdev = hdev;

	write_unlock(&hci_dev_lock);

	return id;
}

/* Unregister HCI device */
static void hci_unregister_dev(struct hci_dev *hdev)
{
	int i;

	AICBT_DBG("%s: hdev %p name %s bus %d", __func__, hdev, hdev->name,
		  hdev->bus);
	set_bit(HCI_UNREGISTER, &hdev->dev_flags);

	write_lock(&hci_dev_lock);
	ghdev = NULL;
	write_unlock(&hci_dev_lock);

	hci_dev_do_close(hdev);
	for (i = 0; i < NUM_REASSEMBLY; i++)
		kfree_skb(hdev->reassembly[i]);
}

static void hci_send_to_stack(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct sk_buff *aic_skb_copy = NULL;

	if (!hdev) {
		AICBT_ERR("%s: Frame for unknown HCI device", __func__);
		return;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		AICBT_ERR("%s: HCI not running", __func__);
		return;
	}

	aic_skb_copy = pskb_copy(skb, GFP_ATOMIC);
	if (!aic_skb_copy) {
		AICBT_ERR("%s: Copy skb error", __func__);
		return;
	}

	memcpy(skb_push(aic_skb_copy, 1), &bt_cb(skb)->pkt_type, 1);
	aic_enqueue(aic_skb_copy);

	/* Make sure bt char device existing before wakeup read queue */
	hdev = hci_dev_get(0);
	if (hdev)
		wake_up_interruptible(&btchr_read_wait);
}

/* Receive frame from HCI drivers */
static int hci_recv_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;

	if (!hdev || (!test_bit(HCI_UP, &hdev->flags) &&
		      !test_bit(HCI_INIT, &hdev->flags))) {
		kfree_skb(skb);
		return -ENXIO;
	}

	/* Incoming skb */
	bt_cb(skb)->incoming = 1;

	/* Time stamp */
	__net_timestamp(skb);

	if (atomic_read(&hdev->promisc)) {
#ifdef CONFIG_SCO_OVER_HCI
		if (bt_cb(skb)->pkt_type == HCI_SCODATA_PKT) {
			hci_send_to_alsa_ringbuffer(hdev, skb);
		} else {
			if (bt_cb(skb)->pkt_type == HCI_EVENT_PKT) {
				if (bypass_event(skb)) {
					kfree_skb(skb);
					return 0;
				}
			}
			hci_send_to_stack(hdev, skb);
		}
#else
		if (bt_cb(skb)->pkt_type == HCI_EVENT_PKT) {
			if (bypass_event(skb)) {
				kfree_skb(skb);
				return 0;
			}
		}
		/* Send copy to the sockets */
		hci_send_to_stack(hdev, skb);
#endif
	}

	kfree_skb(skb);
	return 0;
}

static int hci_reassembly(struct hci_dev *hdev, int type, void *data, int count,
			  __u8 index)
{
	int len = 0;
	int hlen = 0;
	int remain = count;
	struct sk_buff *skb;
	struct bt_skb_cb *scb;

	if ((type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT) ||
	    index >= NUM_REASSEMBLY)
		return -EILSEQ;

	skb = hdev->reassembly[index];

	if (!skb) {
		switch (type) {
		case HCI_ACLDATA_PKT:
			len = HCI_MAX_FRAME_SIZE;
			hlen = HCI_ACL_HDR_SIZE;
			break;
		case HCI_EVENT_PKT:
			len = HCI_MAX_EVENT_SIZE;
			hlen = HCI_EVENT_HDR_SIZE;
			break;
		case HCI_SCODATA_PKT:
			len = HCI_MAX_SCO_SIZE;
			hlen = HCI_SCO_HDR_SIZE;
			break;
		}

		skb = bt_skb_alloc(len, GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;

		scb = (void *)skb->cb;
		scb->expect = hlen;
		scb->pkt_type = type;

		skb->dev = (void *)hdev;
		hdev->reassembly[index] = skb;
	}

	while (count) {
		scb = (void *)skb->cb;
		len = min_t(uint, scb->expect, count);

		memcpy(skb_put(skb, len), data, len);

		count -= len;
		data += len;
		scb->expect -= len;
		remain = count;

		switch (type) {
		case HCI_EVENT_PKT:
			if (skb->len == HCI_EVENT_HDR_SIZE) {
				struct hci_event_hdr *h = hci_event_hdr(skb);

				scb->expect = h->plen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;

		case HCI_ACLDATA_PKT:
			if (skb->len == HCI_ACL_HDR_SIZE) {
				struct hci_acl_hdr *h = hci_acl_hdr(skb);

				scb->expect = __le16_to_cpu(h->dlen);

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;

		case HCI_SCODATA_PKT:
			if (skb->len == HCI_SCO_HDR_SIZE) {
				struct hci_sco_hdr *h = hci_sco_hdr(skb);

				scb->expect = h->dlen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;
		}

		if (scb->expect == 0) {
			/* Complete frame */
			if (type == HCI_ACLDATA_PKT)
				print_acl(skb, 0);
			if (type == HCI_SCODATA_PKT)
				print_sco(skb, 0);
			if (type == HCI_EVENT_PKT)
				print_event(skb);

			bt_cb(skb)->pkt_type = type;
			hci_recv_frame(skb);

			hdev->reassembly[index] = NULL;
			return remain;
		}
	}

	return remain;
}

int hci_recv_fragment(struct hci_dev *hdev, int type, void *data, int count)
{
	int rem = 0;

	if (type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT)
		return -EILSEQ;

	while (count) {
		rem = hci_reassembly(hdev, type, data, count, type - 1);
		if (rem < 0)
			return rem;

		data += (count - rem);
		count = rem;
	}

	return rem;
}
#endif // CONFIG_BLUEDROID

static int btchr_open(struct inode *inode_p, struct file *file_p)
{
	struct btusb_data *data;
	struct hci_dev *hdev;

	AICBT_DBG("%s: BT sdio char device is opening", __func__);

	hdev = hci_dev_get(0);
	if (!hdev) {
		AICBT_DBG("%s: Failed to get hci dev[NULL]", __func__);
		return -ENODEV;
	}
	data = GET_DRV_DATA(hdev);

	atomic_inc(&hdev->promisc);
	/*
	 * As bt device is not re-opened when hotplugged out, we cannot
	 * trust on file's private data(may be null) when other file ops
	 * are invoked.
	 */
	file_p->private_data = data;

	mutex_lock(&btchr_mutex);
	hci_dev_open(0);
	mutex_unlock(&btchr_mutex);

	aic_clear_queue();
	return nonseekable_open(inode_p, file_p);
}

static int btchr_close(struct inode *inode_p, struct file *file_p)
{
	struct btusb_data *data;
	struct hci_dev *hdev;

	AICBT_INFO("%s: BT sdio char device is closing", __func__);

	data = file_p->private_data;
	file_p->private_data = NULL;

#if CONFIG_BLUEDROID
	/*
	 * If the upper layer closes bt char interfaces, no reset
	 * action required even bt device hotplugged out.
	 */
	bt_reset = 0;
#endif

	hdev = hci_dev_get(0);
	if (hdev) {
		atomic_set(&hdev->promisc, 0);
		mutex_lock(&btchr_mutex);
		hci_dev_close(0);
		mutex_unlock(&btchr_mutex);
	}

	return 0;
}

void bt_data_dump(char *tag, void *data, unsigned long len)
{
	unsigned long i = 0;
	u8 *data_ = (uint8_t *)data;

	pr_info("%s %s len:(%lu)\r\n", __func__, tag, len);

	for (i = 0; i < len; i += 16) {
		pr_info("%02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
			data_[0 + i], data_[1 + i], data_[2 + i], data_[3 + i],
			data_[4 + i], data_[5 + i], data_[6 + i], data_[7 + i],
			data_[8 + i], data_[9 + i], data_[10 + i], data_[11 + i],
			data_[12 + i], data_[13 + i], data_[14 + i], data_[15 + i]);
	}
}

static ssize_t btchr_read(struct file *file_p, char __user *buf_p, size_t count,
			  loff_t *pos_p)
{
	struct hci_dev *hdev;
	struct sk_buff *skb;
	ssize_t ret = 0;

	while (count) {
		hdev = hci_dev_get(0);
		if (!hdev) {
			/*
			 * Note: Only when BT device hotplugged out, we wil get
			 * into such situation. In order to keep the upper layer
			 * stack alive (blocking the read), we should never return
			 * EFAULT or break the loop.
			 */
			AICBT_ERR("%s: Failed to get hci dev[Null]", __func__);
		}

		ret = wait_event_interruptible(btchr_read_wait, !is_queue_empty());
		if (ret < 0) {
			AICBT_ERR("%s: wait event is signaled %d", __func__, (int)ret);
			break;
		}

		skb = aic_dequeue_try(count);

		if (skb) {
			ret = sdio_put_user(skb, buf_p, count);
			if (ret < 0)
				AICBT_ERR("%s: Failed to put data to user space", __func__);
			kfree_skb(skb);
			break;
		}
	}

	return ret;
}

#ifdef CONFIG_SUPPORT_VENDOR_APCF
/**
 * btchr_external_write - Submit an HCI packet from another kernel component
 * @buff: Buffer containing the HCI packet type followed by packet data
 * @len: Number of bytes in @buff
 */
void btchr_external_write(char *buff, int len)
{
	struct hci_dev *hdev;
	struct sk_buff *skb;
	int i;
	struct btusb_data *data;

	AICBT_INFO("%s \r\n", __func__);
	for (i = 0; i < len; i++)
		pr_info("0x%x ", (u8)buff[i]);
	pr_info("\r\n");
	hdev = hci_dev_get(0);
	if (!hdev) {
		AICBT_WARN("%s: Failed to get hci dev[Null]", __func__);
		return;
	}
	/* Never trust on btusb_data, as bt device may be hotplugged out */
	data = GET_DRV_DATA(hdev);
	if (!data) {
		AICBT_WARN("%s: Failed to get bt sdio driver data[Null]", __func__);
		return;
	}
	vendor_apcf_sent_done++;

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb)
		return;
	skb_reserve(skb, -1); // Add this line
	skb->dev = (void *)hdev;
	memcpy((__u8 *)skb->data, (__u8 *)buff, len);
	skb_put(skb, len);
	bt_cb(skb)->pkt_type = *((__u8 *)skb->data);
	skb_pull(skb, 1);
	data->hdev->send(skb);
}
EXPORT_SYMBOL_GPL(btchr_external_write);
#endif // CONFIG_SUPPORT_VENDOR_APCF

static ssize_t btchr_write(struct file *file_p, const char __user *buf_p,
			   size_t count, loff_t *pos_p)
{
	struct btusb_data *data = file_p->private_data;
	struct hci_dev *hdev;
	struct sk_buff *skb;
	int err = 0;

	hdev = hci_dev_get(0);
	if (!hdev) {
		AICBT_WARN("%s: Failed to get hci dev[Null]", __func__);
		/*
		 * Note: we bypass the data from the upper layer if bt device
		 * is hotplugged out. Fortunatelly, H4 or H5 HCI stack does
		 * NOT check btchr_write's return value. However, returning
		 * count instead of EFAULT is preferable.
		 */
		/* return -EFAULT; */
		return count;
	}

	if (count > HCI_MAX_FRAME_SIZE)
		return -EINVAL;

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, -1);

	if (copy_from_user(skb_put(skb, count), buf_p, count)) {
		AICBT_ERR("%s: Failed to get data from user space", __func__);
		kfree_skb(skb);
		return -EFAULT;
	}

	skb->dev = (void *)hdev;
	bt_cb(skb)->pkt_type = *((__u8 *)skb->data);

	err = rwnx_sdio_bt_send_req(g_rwnx_plat->sdiodev->rwnx_hw, skb->len, skb);
	if (err < 0)
		pr_err("%s rwnx_sdio_bt_send_req error %d", __func__, err);

	kfree_skb(skb);
	return count;
}

static unsigned int btchr_poll(struct file *file_p, poll_table *wait)
{
	struct btusb_data *data = file_p->private_data;
	struct hci_dev *hdev;

	poll_wait(file_p, &btchr_read_wait, wait);

	hdev = hci_dev_get(0);
	if (!hdev) {
		AICBT_ERR("%s: Failed to get hci dev[Null]", __func__);
		// mdelay(URB_CANCELING_DELAY_MS);
		return POLLERR | POLLHUP;
		return POLLOUT | POLLWRNORM;
	}

	if (!is_queue_empty())
		return POLLIN | POLLRDNORM;

	return POLLOUT | POLLWRNORM;
}

static long btchr_ioctl(struct file *file_p, unsigned int cmd,
			unsigned long arg)
{
	int ret = 0;
	struct hci_dev *hdev;
	struct btusb_data *data;

	pr_info("%s cmd support %d\n", __func__, cmd);

	if (check_set_dlfw_state_value(1) != 1) {
		AICBT_ERR("%s bt controller is disconnecting!", __func__);
		return 0;
	}

	hdev = hci_dev_get(0);
	if (!hdev) {
		AICBT_ERR("%s device is NULL!", __func__);
		set_dlfw_state_value(0);
		return 0;
	}

	AICBT_INFO(" %s DOWN_FW_CFG with Cmd:%d", __func__, cmd);
	switch (cmd) {
	case DOWN_FW_CFG:
		AICBT_INFO(" %s DOWN_FW_CFG", __func__);
		set_bit(HCI_UP, &hdev->flags);
		set_dlfw_state_value(0);
		wake_up_interruptible(&bt_dlfw_wait);
		return 1;
	case DWFW_CMPLT:
		AICBT_INFO(" %s DWFW_CMPLT", __func__);
	case SET_ISO_CFG:
		AICBT_INFO("%s SET_ISO_CFG", __func__);
		if (copy_from_user(&hdev->voice_setting, (__u16 *)arg,
				   sizeof(__u16))) {
			AICBT_INFO(" voice settings err");
		}
		AICBT_INFO(" voice settings = %d", hdev->voice_setting);
	case GET_USB_INFO:
		AICBT_INFO(" %s GET_USB_INFO", __func__);
		set_bit(HCI_UP, &hdev->flags);
		set_dlfw_state_value(0);
		wake_up_interruptible(&bt_dlfw_wait);
		return 1;
	case RESET_CONTROLLER:
		AICBT_INFO(" %s RESET_CONTROLLER", __func__);
		return 1;
	default:
		AICBT_ERR("%s:Failed with wrong Cmd:%d", __func__, cmd);
		goto failed;
	}
failed:
	set_dlfw_state_value(0);
	wake_up_interruptible(&bt_dlfw_wait);
	return ret;
}

typedef u32 compat_uptr_t;
static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)uptr;
}

#ifdef CONFIG_COMPAT
static long compat_btchr_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	return btchr_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif
static const struct file_operations bt_chrdev_ops = {
	.open = btchr_open,
	.release = btchr_close,
	.read = btchr_read,
	.write = btchr_write,
	.poll = btchr_poll,
	.unlocked_ioctl = btchr_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_btchr_ioctl,
#endif
};

int btchr_init(void)
{
	int res = 0;
	struct device *dev;

	AICBT_INFO("Register sdio char device interface for BT driver");
	/*
	 * btchr mutex is used to sync between
	 * 1) downloading patch and opening bt char driver
	 * 2) the file operations of bt char driver
	 */
	mutex_init(&btchr_mutex);

	skb_queue_head_init(&btchr_readq);
	init_waitqueue_head(&btchr_read_wait);
	init_waitqueue_head(&bt_dlfw_wait);

	bt_char_class = class_create(THIS_MODULE, BT_CHAR_DEVICE_NAME);
	if (IS_ERR(bt_char_class)) {
		AICBT_ERR("Failed to create bt char class");
		return PTR_ERR(bt_char_class);
	}

	res = alloc_chrdev_region(&bt_devid, 0, 1, BT_CHAR_DEVICE_NAME);
	if (res < 0) {
		AICBT_ERR("Failed to allocate bt char device");
		goto err_alloc;
	}

	dev =
		device_create(bt_char_class, NULL, bt_devid, NULL, BT_CHAR_DEVICE_NAME);
	if (IS_ERR(dev)) {
		AICBT_ERR("Failed to create bt char device");
		res = PTR_ERR(dev);
		goto err_create;
	}

	cdev_init(&bt_char_dev, &bt_chrdev_ops);
	res = cdev_add(&bt_char_dev, bt_devid, 1);
	if (res < 0) {
		AICBT_ERR("Failed to add bt char device");
		goto err_add;
	}

	return 0;

err_add:
	device_destroy(bt_char_class, bt_devid);
err_create:
	unregister_chrdev_region(bt_devid, 1);
err_alloc:
	class_destroy(bt_char_class);
	return res;
}

void btchr_exit(void)
{
	AICBT_INFO("Unregister sdio char device interface for BT driver");

	device_destroy(bt_char_class, bt_devid);
	cdev_del(&bt_char_dev);
	unregister_chrdev_region(bt_devid, 1);
	class_destroy(bt_char_class);
}

int hdev_init(void)
{
	struct hci_dev *hdev;
	int err = 0;

	hdev = hci_alloc_dev();

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		hdev = NULL;
		return err;
	}

	spin_lock_init(&queue_lock);

	return 0;
}

void hdev_exit(void)
{
	struct hci_dev *hdev;

	hdev = ghdev;
	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
}
