/*
 * Integration of the KSZ9897 for the Freescale Fast Ethernet Controller (FEC)
 * based on the modifications made to drivers/net/ethernet/cadence/macb.c_str
 * from https://github.com/Microchip-Ethernet/EVB-KSZ9477/
 
 * Bogdan Vacaliuc (vacaliucb@ornl.gov) and Edison Fernandez (edison.fernandez@ridgerun.com)
 *
 * //<add the following to fec.h, before struct fec_enet_private>
 * #if defined(CONFIG_KSZ_SWITCH)
 * #include "ksz_fec.h"
 * #endif
 *
 * //<add the following to fec.c, after fec.h>
 * #if defined(CONFIG_KSZ_SWITCH)
 * #include "ksz_fec.c"
 * #endif
 *
 * //<then modify various items in fec.c>
 */
#ifdef CONFIG_KSZ_SWITCH

#if !defined(get_sysfs_data) || defined(CONFIG_KSZ_SWITCH_EMBEDDED)
static void get_sysfs_data_(struct net_device *dev,
	struct semaphore **proc_sem, struct ksz_port **port)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct sw_priv *hw_priv;

	hw_priv = (struct sw_priv *)fep->parent;
	*port = &fep->port;
	*proc_sem = &hw_priv->proc_sem;
}  /* get_sysfs_data */
#endif

#ifndef get_sysfs_data
#define get_sysfs_data		get_sysfs_data_
#endif

#if !defined(CONFIG_KSZ_SWITCH_EMBEDDED)
#define USE_SPEED_LINK
#define USE_MIB

#if defined(CONFIG_HAVE_KSZ9897)
#include "../micrel/ksz_sw_sysfs_9897.c"
#elif defined(CONFIG_HAVE_KSZ8795)
#include "../micrel/ksz_sw_sysfs_8795.c"
#elif defined(CONFIG_HAVE_KSZ8895)
#include "../micrel/ksz_sw_sysfs_8895.c"
#elif defined(CONFIG_HAVE_KSZ8863)
#include "../micrel/ksz_sw_sysfs.c"
#elif defined(CONFIG_HAVE_KSZ8463)
#include "../micrel/ksz_sw_sysfs.c"
#else
#error "One of KSZ9897, KSZ8795, SMI_KSZ8895, KSZ8895, KSZ8863 or KSZ8463 should be defined."
#endif

#ifdef CONFIG_1588_PTP
#include "../micrel/ksz_ptp_sysfs.c"
#endif
#ifdef CONFIG_KSZ_DLR
#include "../micrel/ksz_dlr_sysfs.c"
#endif
#endif	// CONFIG_KSZ_SWITCH_EMBEDDED

static inline int sw_is_switch(struct ksz_sw *sw)
{
	return sw != NULL;
}

//static void copy_old_skb(struct sk_buff *old, struct sk_buff *skb)
//{
//	skb->dev = old->dev;
//	skb->sk = old->sk;
//	skb->protocol = old->protocol;
//	skb->ip_summed = old->ip_summed;
//	skb->csum = old->csum;
//	skb_shinfo(skb)->tx_flags = skb_shinfo(old)->tx_flags;
//	skb_set_network_header(skb, ETH_HLEN);
//
//	dev_kfree_skb_any(old);
//}  /* copy_old_skb */

static int ksz_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct ksz_sw *sw = fep->port.sw;

	if (sw_is_switch(sw))
		sw->net_ops->add_vid(sw, vid);
	return 0;
}

static int ksz_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct ksz_sw *sw = fep->port.sw;

	if (sw_is_switch(sw))
		sw->net_ops->kill_vid(sw, vid);
	return 0;
}

#define SIOCDEVDEBUG			(SIOCDEVPRIVATE + 10)

static void stop_dev_queues(struct ksz_sw *sw, struct net_device *hw_dev,
			    struct fec_enet_private *fep, int q)
{
	if (sw_is_switch(sw)) {
		struct net_device *dev;
		int p;
		int dev_count = sw->dev_count + sw->dev_offset;

		for (p = 0; p < dev_count; p++) {
			dev = sw->netdev[p];
			if (!dev || dev == hw_dev)
				continue;
			if (netif_running(dev) || dev == fep->netdev) {
				netif_stop_subqueue(dev, q);
			}
		}
	}
}

static void wake_dev_queues(struct ksz_sw *sw, struct net_device *hw_dev, int q)
{
	if (sw_is_switch(sw)) {
		struct net_device *dev;
		int p;
		int dev_count = sw->dev_count + sw->dev_offset;

		for (p = 0; p < dev_count; p++) {
			dev = sw->netdev[p];
			if (!dev || dev == hw_dev)
				continue;
			if (netif_running(dev)) {
				if (q >= 0) {
					if (__netif_subqueue_stopped(dev, q))
						netif_wake_subqueue(dev, q);
				} else
					netif_tx_wake_all_queues(dev);
			}
		}
		wake_up_interruptible(&sw->queue);
	}
}

static int fec_set_mac_address(struct net_device *ndev, void *p);
static void set_multicast_list(struct net_device *ndev);

static void promisc_reset_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct fec_enet_private *fep = container_of(dwork, struct fec_enet_private, promisc_reset);

	fep->hw_promisc = 0;
	fep->netdev->flags &= IFF_PROMISC;
	set_multicast_list(fep->netdev);
}

static int ksz_fec_set_mac_address(struct net_device *dev, void *addr)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct ksz_sw *sw = fep->port.sw;
	struct sockaddr *mac = addr;

	if (!memcmp(dev->dev_addr, mac->sa_data, ETH_ALEN))
		return fec_set_mac_address(dev, addr);
	memcpy(dev->dev_addr, mac->sa_data, ETH_ALEN);
	if (sw_is_switch(sw)) {
		struct fec_enet_private *hfep = fep->hw_priv;
		u8 hw_promisc = hfep->hw_promisc;
		u8 promisc;

		promisc = sw->net_ops->set_mac_addr(sw, dev, hw_promisc,
			fep->port.first_port);
		if (promisc != hfep->hw_promisc) {

			/* A hack to accept changed KSZ9897 IBA response. */
			if (!hfep->hw_promisc && 2 == promisc) {
				promisc = 1;
				schedule_delayed_work(&hfep->promisc_reset, 10);
			}
			hfep->hw_promisc = promisc;

			/* Turn on/off promiscuous mode. */
			if (hfep->hw_promisc <= 1 && hw_promisc <= 1) {
				dev->flags |= IFF_PROMISC;
				set_multicast_list(dev);
			}
		}
	}
	if (fep == fep->hw_priv)
		return fec_set_mac_address(dev, addr);
	return 0;
}

static int ksz_eth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	u32 max_mtu;

	do {
		struct ksz_sw *sw = fep->port.sw;

		/* 3906 bytes seems to be the transmit limit of the MAC.
		 * So the MTU is about 3906 - 5 = 3901 - 18 = 3883.
		 * 1536 bytes seems to be the receive limit of the MAC.
		 * Up to 1527 bytes can be received although the receive
		 * oversize counter is increased for frames more than that.
		 * Enable jumbo frame does allow receive up to 3902 bytes.
		 * The maximum ping size is 3854 between KSZ9897 and KSZ9563.
		 * 3854 + 42 + 1 = 3897
		 * With PTP off the size can be increased by 4 bytes.
		 * The effective MTU is 3882.
		 * However, receive sometimes has overrun issue and it seems
		 * the maximum size is 0xC00 = 3072, so the MTU is 3072 - 18 -
		 * 2 - 6 = 3046.
		 */
		max_mtu = 3046;
		if (sw_is_switch(sw)) {
			int mtu = sw->mtu - ETH_HLEN - ETH_FCS_LEN;

			mtu -= sw->net_ops->get_mtu(sw);
			if (max_mtu < mtu)
				max_mtu = mtu;
		}
	} while (0);

	new_mtu = new_mtu > max_mtu ? max_mtu : new_mtu;
	return eth_change_mtu(dev, new_mtu);
}

static struct ksz_sw *check_avail_switch(struct net_device *netdev, int id)
{
	int phy_mode;
	char phy_id[MII_BUS_ID_SIZE];
	char bus_id[MII_BUS_ID_SIZE];
	struct ksz_sw *sw = NULL;
	struct phy_device *phydev = NULL;

	/* Check whether MII switch exists. */
	phy_mode = PHY_INTERFACE_MODE_MII;
	snprintf(bus_id, MII_BUS_ID_SIZE, "sw.%d", id);
	snprintf(phy_id, MII_BUS_ID_SIZE, PHY_ID_FMT, bus_id, 0);
	phydev = phy_attach(netdev, phy_id, phy_mode);
	if (!IS_ERR(phydev)) {
		struct phy_priv *phydata = phydev->priv;

		sw = phydata->port->sw;

		/*
		 * In case multiple devices mode is used and this phydev is not
		 * attached again.
		 */
		if (sw)
			phydev->interface = sw->interface;
		phy_detach(phydev);
	}
	return sw;
}

#ifndef CONFIG_KSZ_NO_MDIO_BUS
static int mdio_read(struct net_device *dev, int phy_id, int reg_num)
{
	struct mii_bus *bus = dev->phydev->mdio.bus;
	int err;
	int val_out = 0xffff;

	mutex_lock(&bus->mdio_lock);
	err = bus->read(bus, phy_id, reg_num);
	mutex_unlock(&bus->mdio_lock);
	if (err >= 0)
		val_out = err;

	return val_out;
}

static void mdio_write(struct net_device *dev, int phy_id, int reg_num, int val)
{
	struct mii_bus *bus = dev->phydev->mdio.bus;

	mutex_lock(&bus->mdio_lock);
	bus->write(bus, phy_id, reg_num, (u16) val);
	mutex_unlock(&bus->mdio_lock);
}

static void __maybe_unused ksz_fec_adjust_link(struct net_device *dev)
{}
#endif	// CONFIG_KSZ_NO_MDIO_BUS

static u8 get_priv_state(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	return fep->state;
}  /* get_priv_state */

static void set_priv_state(struct net_device *dev, u8 state)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	fep->state = state;
}  /* set_priv_state */

static struct ksz_port *get_priv_port(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	return &fep->port;
}  /* get_priv_port */

#if defined(CONFIG_HAVE_KSZ9897)
static int get_net_ready(struct net_device *dev)
{
	struct fec_enet_private *priv = netdev_priv(dev);

	return priv->hw_priv->ready;
}
#endif

static void prep_sw_first(struct ksz_sw *sw, int *port_count,
	int *mib_port_count, int *dev_count, char *dev_name)
{
	*port_count = 1;
	*mib_port_count = 1;
	*dev_count = 1;
	dev_name[0] = '\0';
	sw->net_ops->get_state = get_priv_state;
	sw->net_ops->set_state = set_priv_state;
	sw->net_ops->get_priv_port = get_priv_port;
#if defined(CONFIG_HAVE_KSZ9897)
	sw->net_ops->get_ready = get_net_ready;
#endif
	sw->net_ops->setup_special(sw, port_count, mib_port_count, dev_count);
}

static void prep_sw_dev(struct ksz_sw *sw, struct platform_device *pdev, int i,
	int port_count, int mib_port_count, char *dev_name)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
#ifndef CONFIG_KSZ_NO_MDIO_BUS
	int phy_mode;
	char phy_id[MII_BUS_ID_SIZE];
	char bus_id[MII_BUS_ID_SIZE];
	struct phy_device *phydev;
#endif

	fep->phy_addr = sw->net_ops->setup_dev(sw, ndev, dev_name, &fep->port,
		i, port_count, mib_port_count);

#ifndef CONFIG_KSZ_NO_MDIO_BUS
	phy_mode = fep->phy_interface;
	snprintf(bus_id, MII_BUS_ID_SIZE, "sw.%d", 0);
	snprintf(phy_id, MII_BUS_ID_SIZE, PHY_ID_FMT, bus_id, fep->phy_addr);
	phydev = phy_attach(ndev, phy_id, phy_mode);
	if (!IS_ERR(phydev)) {
		ndev->phydev = phydev;
		fep->mii_if.phy_id_mask = 0x1f;
		fep->mii_if.reg_num_mask = 0x1f;
		fep->mii_if.dev = ndev;
		fep->mii_if.mdio_read = mdio_read;
		fep->mii_if.mdio_write = mdio_write;
		fep->mii_if.phy_id = fep->phy_addr;
		if ((phydev->drv->features & PHY_GBIT_FEATURES) ==
		     PHY_GBIT_FEATURES)
			fep->mii_if.supports_gmii = 1;
	}
#endif
}  /* prep_sw_dev */

static int ksz_fec_sw_chk(struct fec_enet_private *fep)
{
	struct ksz_sw *sw;

	sw = fep->port.sw;
	if (!sw) {
		sw = check_avail_switch(fep->netdev, 0);
		if (!sw_is_switch(sw))
			return -ENXIO;
	}
	fep->port.sw = sw;
	return 0;
}

static void fec_enet_adjust_link(struct net_device *ndev);
static int fec_enet_rx_napi(struct napi_struct *napi, int budget);

static const struct net_device_ops fec_netdev_ops;
static const struct ethtool_ops fec_enet_ethtool_ops;

static int __maybe_unused ksz_fec_sw_init(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct ksz_sw *sw;
	int err;
	int i;
	int port_count;
	int dev_count;
	int mib_port_count;
	char dev_label[IFNAMSIZ];
	struct fec_enet_private *hw_priv;
	struct net_device *dev;
	struct net_device *main_dev;
	netdev_features_t features;

	sw = fep->port.sw;
	if (!sw) {
		err = ksz_fec_sw_chk(fep);
		if (err)
			return err;
	}

	/* This is the main private structure holding hardware information. */
	hw_priv = fep;
	hw_priv->parent = sw->dev;
	main_dev = ndev;
	pdev = fep->pdev;
	features = main_dev->features;

	prep_sw_first(sw, &port_count, &mib_port_count, &dev_count, dev_label);

	/* The main switch phydev will not be attached. */
	if (sw->dev_offset) {
		struct phy_device *phydev = sw->phy[0];

		phydev->interface = fep->phy_interface;
	}

	/* Save the base device name. */
	strlcpy(dev_label, hw_priv->netdev->name, IFNAMSIZ);

	prep_sw_dev(sw, pdev, 0, port_count, mib_port_count, dev_label);

	/* Only the main one needs to set adjust_link for configuration. */
	if (ndev->phydev->mdio.bus)
		ndev->phydev->adjust_link = fec_enet_adjust_link;

	fep->link = 0;
	fep->speed = 0;
	fep->full_duplex = -1;

	/* Point to real private structure holding hardware information. */
	fep->hw_priv = hw_priv;
	INIT_DELAYED_WORK(&hw_priv->promisc_reset, promisc_reset_work);

	for (i = 1; i < dev_count; i++) {
		dev = alloc_etherdev_mqs(
			sizeof(*fep) + fep->len_fec_stats,
			hw_priv->num_tx_queues, hw_priv->num_rx_queues);
		if (!dev)
			break;

		fep = netdev_priv(dev);
		fep->pdev = pdev;
		fep->netdev = dev;
		fep->num_tx_queues = hw_priv->num_tx_queues;
		fep->num_rx_queues = hw_priv->num_rx_queues;

		fep->hw_priv = hw_priv;
		dev->phydev = &fep->dummy_phy;
		dev->phydev->duplex = 1;
		dev->phydev->speed = SPEED_1000;

		//spin_lock_init(&fep->lock);

		dev->netdev_ops = &fec_netdev_ops;
		netif_napi_add(dev, &fep->napi, fec_enet_rx_napi, NAPI_POLL_WEIGHT);
		dev->ethtool_ops = &fec_enet_ethtool_ops;

		dev->base_addr = main_dev->base_addr;
		memcpy(dev->dev_addr, main_dev->dev_addr, ETH_ALEN);

		dev->hw_features = main_dev->hw_features;
		dev->features = features;

		SET_NETDEV_DEV(dev, &pdev->dev);

		prep_sw_dev(sw, pdev, i, port_count, mib_port_count, dev_label);
		if (ndev->phydev->mdio.bus)
			ndev->phydev->adjust_link = fec_enet_adjust_link;

		err = register_netdev(dev);
		if (err) {
			free_netdev(dev);
			break;
		}

		netif_carrier_off(dev);
	}

	/*
	 * Adding sysfs support is optional for network device.  It is more
	 * convenient to locate eth0 more or less than spi<bus>.<select>,
	 * especially when the bus number is auto assigned which results in a
	 * very big number.
	 */
	err = init_sw_sysfs(sw, &hw_priv->sysfs, &main_dev->dev);

#ifdef CONFIG_1588_PTP
	if (sw->features & PTP_HW)
		err = init_ptp_sysfs(&hw_priv->ptp_sysfs, &main_dev->dev);
#endif
#ifdef CONFIG_KSZ_DLR
	if (sw->features & DLR_HW)
		err = init_dlr_sysfs(&main_dev->dev);
#endif

	return 0;
}

static void ksz_fec_sw_exit(struct fec_enet_private *fep)
{
	struct net_device *ndev = fep->netdev;
	struct ksz_sw *sw = fep->port.sw;
	int i;

#ifdef CONFIG_KSZ_DLR
	if (sw->features & DLR_HW)
		exit_dlr_sysfs(&ndev->dev);
#endif
#ifdef CONFIG_1588_PTP
	if (sw->features & PTP_HW)
		exit_ptp_sysfs(&fep->ptp_sysfs, &ndev->dev);
#endif
	exit_sw_sysfs(sw, &fep->sysfs, &ndev->dev);
	for (i = 1; i < sw->dev_count + sw->dev_offset; i++) {
		ndev = sw->netdev[i];
		if (!ndev)
			continue;
		fep = netdev_priv(ndev);
		flush_work(&fep->port.link_update);
		unregister_netdev(ndev);
		if (ndev->phydev->mdio.bus)
			phy_detach(ndev->phydev);
		free_netdev(ndev);
	}
}

static int fec_enet_close(struct net_device *ndev);

static void ksz_fec_shutdown(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct fec_enet_private *fep;
	struct ksz_sw *sw;
	int i;
	int dev_count = 1;

	ndev = platform_get_drvdata(pdev);
	if (!ndev)
		return;
	fep = netdev_priv(ndev);
	sw = fep->port.sw;
	if (sw_is_switch(sw))
		dev_count = sw->dev_count + sw->dev_offset;
	for (i = 0; i < dev_count; i++) {
		if (sw_is_switch(sw)) {
			ndev = sw->netdev[i];
			if (!ndev)
				continue;
		}
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			fec_enet_close(ndev);
		}
	}
}
#endif	// CONFIG_KSZ_SWITCH