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
#include <linux/if_vlan.h>

#if defined(CONFIG_KSZ_SWITCH_EMBEDDED)
#include <linux/spi/spi.h>
#include <linux/crc32.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <net/ipv6.h>

/* Need to predefine get_sysfs_data. */

#ifndef get_sysfs_data
struct ksz_port;

static void get_sysfs_data_(struct net_device *dev,
	struct semaphore **proc_sem, struct ksz_port **port);

#define get_sysfs_data		get_sysfs_data_
#endif

//static void copy_old_skb(struct sk_buff *old, struct sk_buff *skb);
#define DO_NOT_USE_COPY_SKB

#if defined(CONFIG_HAVE_KSZ9897)
#include "../micrel/spi-ksz9897.c"
#elif defined(CONFIG_HAVE_KSZ8795)
#include "../micrel/spi-ksz8795.c"
#elif defined(CONFIG_SMI_KSZ8895)
#include "../micrel/smi-ksz8895.c"
#elif defined(CONFIG_HAVE_KSZ8895)
#include "../micrel/spi-ksz8895.c"
#elif defined(CONFIG_HAVE_KSZ8863)
#include "../micrel/spi-ksz8863.c"
#elif defined(CONFIG_HAVE_KSZ8463)
#include "../micrel/spi-ksz8463.c"
#else
#error "One of KSZ9897, KSZ8795, SMI_KSZ8895, KSZ8895, KSZ8863 or KSZ8463 should be defined."
#endif	// CONFIG_HAVE_KSZ9897

#elif defined(CONFIG_HAVE_KSZ9897)	// CONFIG_KSZ_SWITCH_EMBEDDED
#include "../micrel/ksz_cfg_9897.h"
#elif defined(CONFIG_HAVE_KSZ8795)
#include "../micrel/ksz_cfg_8795.h"
#elif defined(CONFIG_HAVE_KSZ8895)
#include "../micrel/ksz_cfg_8895.h"
#elif defined(CONFIG_HAVE_KSZ8863)
#include "../micrel/ksz_cfg_8863.h"
#elif defined(CONFIG_HAVE_KSZ8463)
#include "../micrel/ksz_cfg_8463.h"
#else
#error "One of KSZ9897, KSZ8795, SMI_KSZ8895, KSZ8895, KSZ8863 or KSZ8463 should be defined."
#endif	// CONFIG_KSZ_SWITCH_EMBEDDED

#ifndef CONFIG_KSZ_SWITCH_EMBEDDED
#include "../micrel/ksz_spi_net.h"
#endif	// CONFIG_KSZ_SWITCH_EMBEDDED

#endif	// CONFIG_KSZ_SWITCH
