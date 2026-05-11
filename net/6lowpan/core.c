/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * (C) 2015 Pengutronix, Alexander Aring <aar@pengutronix.de>
 */

#include <linux/module.h>

#include <net/6lowpan.h>

void lowpan_netdev_setup(struct net_device *dev, enum lowpan_lltypes lltype)
{
	dev->addr_len = EUI64_ADDR_LEN;
	dev->type = ARPHRD_6LOWPAN;
	dev->mtu = IPV6_MIN_MTU;
	dev->priv_flags |= IFF_NO_QUEUE;

	lowpan_priv(dev)->lltype = lltype;
}
EXPORT_SYMBOL(lowpan_netdev_setup);

static int lowpan_event(struct notifier_block *unused,
			unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int i;

	if (dev->type != ARPHRD_6LOWPAN)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_DOWN:
		for (i = 0; i < LOWPAN_IPHC_CTX_TABLE_SIZE; i++)
			clear_bit(LOWPAN_IPHC_CTX_FLAG_ACTIVE,
				  &lowpan_priv(dev)->ctx.table[i].flags);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block lowpan_notifier = {
	.notifier_call = lowpan_event,
};

static int __init lowpan_module_init(void)
{
	request_module_nowait("ipv6");

	request_module_nowait("nhc_dest");
	request_module_nowait("nhc_fragment");
	request_module_nowait("nhc_hop");
	request_module_nowait("nhc_ipv6");
	request_module_nowait("nhc_mobility");
	request_module_nowait("nhc_routing");
	request_module_nowait("nhc_udp");

	return 0;
}
module_init(lowpan_module_init);

MODULE_LICENSE("GPL");
