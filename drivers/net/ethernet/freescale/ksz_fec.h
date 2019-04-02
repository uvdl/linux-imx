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
#if defined(CONFIG_KSZ_SWITCH_EMBEDDED)
#error "CONFIG_KSZ_SWITCH_EMBEDDED is not supported."
#elif defined(CONFIG_HAVE_KSZ9897)	// CONFIG_KSZ_SWITCH_EMBEDDED
#include "../micrel/ksz_cfg_9897.h"
#else
#error "Only KSZ9897 is supported."
#endif	// CONFIG_KSZ_SWITCH_EMBEDDED
#endif	// CONFIG_KSZ_SWITCH
