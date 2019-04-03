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

static inline int sw_is_switch(struct ksz_sw *sw)
{
	return sw != NULL;
}

static void set_multicast_list(struct net_device *ndev);

static void promisc_reset_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct fec_enet_private *fep = container_of(dwork, struct fec_enet_private, promisc_reset);

	fep->hw_promisc = 0;
	fep->netdev->flags &= IFF_PROMISC;
	set_multicast_list(fep->netdev);
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
//#if defined(CONFIG_HAVE_KSZ9897)
	// sw->net_ops->get_ready = get_net_ready;
//#endif
	sw->net_ops->setup_special(sw, port_count, mib_port_count, dev_count);
}

static void prep_sw_dev(struct ksz_sw *sw, struct platform_device *pdev, int i,
	int port_count, int mib_port_count, char *dev_name)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	int phy_mode;
	char phy_id[MII_BUS_ID_SIZE];
	char bus_id[MII_BUS_ID_SIZE];
	struct phy_device *phydev;

	fep->phy_addr = sw->net_ops->setup_dev(sw, ndev, dev_name, &fep->port,
		i, port_count, mib_port_count);

	phy_mode = fep->phy_interface;
	snprintf(bus_id, MII_BUS_ID_SIZE, "sw.%d", 0);
	snprintf(phy_id, MII_BUS_ID_SIZE, PHY_ID_FMT, bus_id, fep->phy_addr);
	phydev = phy_attach(ndev, phy_id, phy_mode);
	if (!IS_ERR(phydev)) {
		ndev->phydev = phydev;
		fep->mii_bus = phydev->mdio.bus;
		fep->mii_bus->priv = fep;
		fep->mii_bus->parent = &pdev->dev;
	}
	//phy_detach(phydev);
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

static int __maybe_unused ksz_fec_sw_init(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct ksz_sw *sw;
	int ret;
	int port_count;
	int dev_count;
	int mib_port_count;
	char dev_label[IFNAMSIZ];

	ret = ksz_fec_sw_chk(fep);
	if (ret)
        return ret;

	sw = fep->port.sw;

	prep_sw_first(sw, &port_count, &mib_port_count, &dev_count, dev_label);

	/* The main switch phydev will not be attached. */
	if (sw->dev_offset) {
		struct phy_device *phydev = sw->phy[0];

		phydev->interface = fep->phy_interface;
	}

	/* Save the base device name. */
	strlcpy(dev_label, ndev->name, IFNAMSIZ);

	prep_sw_dev(sw, pdev, 0, port_count, mib_port_count, dev_label);

	/* Only the main one needs to set adjust_link for configuration. */
	if (ndev->phydev->mdio.bus)
		ndev->phydev->adjust_link = fec_enet_adjust_link;

	//fep->link = 0;
	//fep->speed = 0;
	//fep->full_duplex = -1;

	INIT_DELAYED_WORK(&fep->promisc_reset, promisc_reset_work);

	if (dev_count > 1) {
		netdev_err(ndev, "dev_count > 1; if kernel panics in worker_thread then more code is needed here...\n");
	}

	return 0;
}

#endif	// CONFIG_KSZ_SWITCH
