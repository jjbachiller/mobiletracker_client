#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <linux/if_ether.h>
#include <linux/wireless.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <curl/curl.h>
#include <netpacket/packet.h>
#include <json/json.h>
//// Para el driver
#include <wait.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <linux/if.h>
//Para leer ficheros de configuración libconfig-9
#include <libconfig.h>
////
#include "teslasensor.h"
#include "osdep/byteorder.h"

#define ARPHRD_IEEE80211        801
#define ARPHRD_IEEE80211_PRISM  802
#define ARPHRD_IEEE80211_FULL   803

typedef enum {
        DT_NULL = 0,
        DT_WLANNG,
        DT_HOSTAP,
        DT_MADWIFI,
        DT_MADWIFING,
        DT_BCM43XX,
        DT_ORINOCO,
        DT_ZD1211RW,
        DT_ACX,
        DT_MAC80211_RT,
        DT_AT76USB,
        DT_IPW2200

} DRIVER_TYPE;

static const char * szaDriverTypes[] = {
        [DT_NULL] = "Unknown",
        [DT_WLANNG] = "Wlan-NG",
        [DT_HOSTAP] = "HostAP",
        [DT_MADWIFI] = "Madwifi",
        [DT_MADWIFING] = "Madwifi-NG",
        [DT_BCM43XX] = "BCM43xx",
        [DT_ORINOCO] = "Orinoco",
        [DT_ZD1211RW] = "ZD1211RW",
        [DT_ACX] = "ACX",
        [DT_MAC80211_RT] = "Mac80211-Radiotap",
        [DT_AT76USB] = "Atmel 76_usb",
        [DT_IPW2200] = "ipw2200"
};

struct wifi_dev {
    int fd_in, arptype_in;
    int fd_out, arptype_out;
    int fd_main;

    DRIVER_TYPE drivertype; /* inited to DT_UNKNOWN on allocation by wi_alloc */
    
    unsigned char pl_mac[6];
    
    //Para el driver
    char *main_if;
};

/////////////////////////////////// RADIOTAP y RADIOTAP PARSER
/*
 * Radiotap header iteration
 *   implemented in src/radiotap-parser.c
 *
 * call __ieee80211_radiotap_iterator_init() to init a semi-opaque iterator
 * struct ieee80211_radiotap_iterator (no need to init the struct beforehand)
 * then loop calling __ieee80211_radiotap_iterator_next()... it returns -1
 * if there are no more args in the header, or the next argument type index
 * that is present.  The iterator's this_arg member points to the start of the
 * argument associated with the current argument index that is present,
 * which can be found in the iterator's this_arg_index member.  This arg
 * index corresponds to the IEEE80211_RADIOTAP_... defines.
 */


int ieee80211_radiotap_iterator_init(
	struct ieee80211_radiotap_iterator * iterator,
	struct ieee80211_radiotap_header * radiotap_header,
	int max_length)
{
	if(iterator == NULL)
		return (-EINVAL);

	if(radiotap_header == NULL)
		return (-EINVAL);
	/* Linux only supports version 0 radiotap format */

	if (radiotap_header->it_version)
		return (-EINVAL);

	/* sanity check for allowed length and radiotap length field */

	if (max_length < (le16_to_cpu(radiotap_header->it_len)))
		return (-EINVAL);

	iterator->rtheader = radiotap_header;
	iterator->max_length = le16_to_cpu(radiotap_header->it_len);
	iterator->arg_index = 0;
	iterator->bitmap_shifter = le32_to_cpu(radiotap_header->it_present);
	iterator->arg = ((u8 *)radiotap_header) +
			sizeof (struct ieee80211_radiotap_header);
	iterator->this_arg = 0;

	/* find payload start allowing for extended bitmap(s) */

	if (unlikely(iterator->bitmap_shifter &
	    IEEE80211_RADIOTAP_PRESENT_EXTEND_MASK)) {
		while (le32_to_cpu(*((u32 *)iterator->arg)) &
		    IEEE80211_RADIOTAP_PRESENT_EXTEND_MASK) {
			iterator->arg += sizeof (u32);

			/*
			 * check for insanity where the present bitmaps
			 * keep claiming to extend up to or even beyond the
			 * stated radiotap header length
			 */

			if ((((void*)iterator->arg) - ((void*)iterator->rtheader)) >
			    iterator->max_length)
				return (-EINVAL);

		}

		iterator->arg += sizeof (u32);

		/*
		 * no need to check again for blowing past stated radiotap
		 * header length, becuase ieee80211_radiotap_iterator_next
		 * checks it before it is dereferenced
		 */

	}

	/* we are all initialized happily */

	return (0);
}


/**
 * ieee80211_radiotap_iterator_next - return next radiotap parser iterator arg
 * @iterator: radiotap_iterator to move to next arg (if any)
 *
 * Returns: next present arg index on success or negative if no more or error
 *
 * This function returns the next radiotap arg index (IEEE80211_RADIOTAP_...)
 * and sets iterator->this_arg to point to the payload for the arg.  It takes
 * care of alignment handling and extended present fields.  interator->this_arg
 * can be changed by the caller.  The args pointed to are in little-endian
 * format.
 */

int ieee80211_radiotap_iterator_next(
	struct ieee80211_radiotap_iterator * iterator)
{

	/*
	 * small length lookup table for all radiotap types we heard of
	 * starting from b0 in the bitmap, so we can walk the payload
	 * area of the radiotap header
	 *
	 * There is a requirement to pad args, so that args
	 * of a given length must begin at a boundary of that length
	 * -- but note that compound args are allowed (eg, 2 x u16
	 * for IEEE80211_RADIOTAP_CHANNEL) so total arg length is not
	 * a reliable indicator of alignment requirement.
	 *
	 * upper nybble: content alignment for arg
	 * lower nybble: content length for arg
	 */

	static const u8 rt_sizes[] = {
		[IEEE80211_RADIOTAP_TSFT] = 0x88,
		[IEEE80211_RADIOTAP_FLAGS] = 0x11,
		[IEEE80211_RADIOTAP_RATE] = 0x11,
		[IEEE80211_RADIOTAP_CHANNEL] = 0x24,
		[IEEE80211_RADIOTAP_FHSS] = 0x22,
		[IEEE80211_RADIOTAP_DBM_ANTSIGNAL] = 0x11,
		[IEEE80211_RADIOTAP_DBM_ANTNOISE] = 0x11,
		[IEEE80211_RADIOTAP_LOCK_QUALITY] = 0x22,
		[IEEE80211_RADIOTAP_TX_ATTENUATION] = 0x22,
		[IEEE80211_RADIOTAP_DB_TX_ATTENUATION] = 0x22,
		[IEEE80211_RADIOTAP_DBM_TX_POWER] = 0x11,
		[IEEE80211_RADIOTAP_ANTENNA] = 0x11,
		[IEEE80211_RADIOTAP_DB_ANTSIGNAL] = 0x11,
		[IEEE80211_RADIOTAP_DB_ANTNOISE] = 0x11
		/*
		 * add more here as they are defined in
		 * include/net/ieee80211_radiotap.h
		 */
	};

	/*
	 * for every radiotap entry we can at
	 * least skip (by knowing the length)...
	 */

	while (iterator->arg_index < (int)sizeof (rt_sizes)) {
		int hit = 0;

		if (!(iterator->bitmap_shifter & 1))
			goto next_entry; /* arg not present */

		/*
		 * arg is present, account for alignment padding
		 *  8-bit args can be at any alignment
		 * 16-bit args must start on 16-bit boundary
		 * 32-bit args must start on 32-bit boundary
		 * 64-bit args must start on 64-bit boundary
		 *
		 * note that total arg size can differ from alignment of
		 * elements inside arg, so we use upper nybble of length
		 * table to base alignment on
		 *
		 * also note: these alignments are ** relative to the
		 * start of the radiotap header **.  There is no guarantee
		 * that the radiotap header itself is aligned on any
		 * kind of boundary.
		 */

		if ((((void*)iterator->arg)-((void*)iterator->rtheader)) &
		    ((rt_sizes[iterator->arg_index] >> 4) - 1))
			iterator->arg_index +=
				(rt_sizes[iterator->arg_index] >> 4) -
				((((void*)iterator->arg) -
				((void*)iterator->rtheader)) &
				((rt_sizes[iterator->arg_index] >> 4) - 1));

		/*
		 * this is what we will return to user, but we need to
		 * move on first so next call has something fresh to test
		 */

		iterator->this_arg_index = iterator->arg_index;
		iterator->this_arg = iterator->arg;
		hit = 1;

		/* internally move on the size of this arg */

		iterator->arg += rt_sizes[iterator->arg_index] & 0x0f;

		/*
		 * check for insanity where we are given a bitmap that
		 * claims to have more arg content than the length of the
		 * radiotap section.  We will normally end up equalling this
		 * max_length on the last arg, never exceeding it.
		 */

		if ((((void*)iterator->arg) - ((void*)iterator->rtheader)) >
		    iterator->max_length)
			return (-EINVAL);

	next_entry:

		iterator->arg_index++;
		if (unlikely((iterator->arg_index & 31) == 0)) {
			/* completed current u32 bitmap */
			if (iterator->bitmap_shifter & 1) {
				/* b31 was set, there is more */
				/* move to next u32 bitmap */
				iterator->bitmap_shifter = le32_to_cpu(
					*iterator->next_bitmap);
				iterator->next_bitmap++;
			} else {
				/* no more bitmaps: end */
				iterator->arg_index = sizeof (rt_sizes);
			}
		} else { /* just try the next bit */
			iterator->bitmap_shifter >>= 1;
		}

		/* if we found a valid arg earlier, return it now */

		if (hit)
			return (iterator->this_arg_index);

	}

	/* we don't know how to handle any more args, we're done */

	return (-1);
}

///////////////////////////////////////////////////

int getFrequencyFromChannel(int channel)
{
	static int frequencies[] = {
		-1, // No channel 0
		2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457, 2462, 2467, 2472, 2484,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // Nothing from channel 15 to 34 (exclusive)
		5170, 5175, 5180, 5185, 5190, 5195, 5200, 5205, 5210, 5215, 5220, 5225, 5230, 5235, 5240, 5245,
		5250, 5255, 5260, 5265, 5270, 5275, 5280, 5285, 5290, 5295, 5300, 5305, 5310, 5315, 5320, 5325,
		5330, 5335, 5340, 5345, 5350, 5355, 5360, 5365, 5370, 5375, 5380, 5385, 5390, 5395, 5400, 5405,
		5410, 5415, 5420, 5425, 5430, 5435, 5440, 5445, 5450, 5455, 5460, 5465, 5470, 5475, 5480, 5485,
		5490, 5495, 5500, 5505, 5510, 5515, 5520, 5525, 5530, 5535, 5540, 5545, 5550, 5555, 5560, 5565,
		5570, 5575, 5580, 5585, 5590, 5595, 5600, 5605, 5610, 5615, 5620, 5625, 5630, 5635, 5640, 5645,
		5650, 5655, 5660, 5665, 5670, 5675, 5680, 5685, 5690, 5695, 5700, 5705, 5710, 5715, 5720, 5725,
		5730, 5735, 5740, 5745, 5750, 5755, 5760, 5765, 5770, 5775, 5780, 5785, 5790, 5795, 5800, 5805,
		5810, 5815, 5820, 5825, 5830, 5835, 5840, 5845, 5850, 5855, 5860, 5865, 5870, 5875, 5880, 5885,
		5890, 5895, 5900, 5905, 5910, 5915, 5920, 5925, 5930, 5935, 5940, 5945, 5950, 5955, 5960, 5965,
		5970, 5975, 5980, 5985, 5990, 5995, 6000, 6005, 6010, 6015, 6020, 6025, 6030, 6035, 6040, 6045,
		6050, 6055, 6060, 6065, 6070, 6075, 6080, 6085, 6090, 6095, 6100
	};

	return (channel > 0 && channel <= 221) ? frequencies[channel] : -1;
}

/**
 * Return the channel from the frequency (in Mhz)
 */
int getChannelFromFrequency(int frequency)
{
	if (frequency >= 2412 && frequency <= 2472)
		return (frequency - 2407) / 5;
	else if (frequency == 2484)
		return 14;
	else if (frequency >= 5000 && frequency <= 6100)
		return (frequency - 5000) / 5;
	else
		return -1;
}

unsigned long calc_crc_osdep( unsigned char * buf, int len)
{
    unsigned long crc = 0xFFFFFFFF;

    for( ; len > 0; len--, buf++ )
        crc = crc_tbl_osdep[(crc ^ *buf) & 0xFF] ^ ( crc >> 8 );

    return( ~crc );
}

/* CRC checksum verification routine */

int check_crc_buf_osdep( unsigned char *buf, int len )
{
    unsigned long crc;

    if (len<0)
    	return 0;

    crc = calc_crc_osdep(buf, len);
    buf+=len;
    return( ( ( crc       ) & 0xFF ) == buf[0] &&
            ( ( crc >>  8 ) & 0xFF ) == buf[1] &&
            ( ( crc >> 16 ) & 0xFF ) == buf[2] &&
            ( ( crc >> 24 ) & 0xFF ) == buf[3] );
}

int set_monitor( struct wifi_dev *dev, int fd )
{
    int unused;
    struct iwreq wrq;
    char *iface = "wlan0";

    memset( &wrq, 0, sizeof( struct iwreq ) );
    strncpy( wrq.ifr_name, iface, IFNAMSIZ );
    wrq.u.mode = IW_MODE_MONITOR;

    if( ioctl( fd, SIOCSIWMODE, &wrq ) < 0 )
    {
        perror( "ioctl(SIOCSIWMODE) failed" );
        return( 1 );
    }

    /* couple of iwprivs to enable the prism header */

    if( ! fork() )  /* hostap */
    {
        close( 0 ); close( 1 ); close( 2 ); unused = chdir( "/" );
        execlp( "iwpriv", "iwpriv", iface, "monitor_type", "1", NULL );
        exit( 1 );
    }
    wait( NULL );

    if( ! fork() )  /* r8180 */
    {
        close( 0 ); close( 1 ); close( 2 ); unused = chdir( "/" );
        execlp( "iwpriv", "iwpriv", iface, "prismhdr", "1", NULL );
        exit( 1 );
    }
    wait( NULL );

    if( ! fork() )  /* prism54 */
    {
        close( 0 ); close( 1 ); close( 2 ); unused = chdir( "/" );
        execlp( "iwpriv", "iwpriv", iface, "set_prismhdr", "1", NULL );
        exit( 1 );
    }
    wait( NULL );

    return( 0 );
}

static int opensysfs(struct wifi_dev *dev, char *iface, int fd) {
    int fd2;
    char buf[256];

    /* ipw2200 injection */
    snprintf(buf, 256, "/sys/class/net/%s/device/inject", iface);
    fd2 = open(buf, O_WRONLY);

    /* bcm43xx injection */
    if (fd2 == -1)
    snprintf(buf, 256, "/sys/class/net/%s/device/inject_nofcs", iface);
    fd2 = open(buf, O_WRONLY);

    if (fd2 == -1)
        return -1;

    dup2(fd2, fd);
    close(fd2);

    printf("Tengo que poner sysfs_inject a uno en el device\n");
    //dev->sysfs_inject=1;
    return 0;
}

static int openraw(struct wifi_dev *dev, int fd, int *arptype,
		   unsigned char *mac)
{
    struct ifreq ifr;
    //struct ifreq ifr2;
    struct iwreq wrq;
    //struct iwreq wrq2;
    struct packet_mreq mr;
    struct sockaddr_ll sll;
    //struct sockaddr_ll sll2;
    
    char *iface = "wlan0";

    /* find the interface index */

    memset( &ifr, 0, sizeof( ifr ) );
    strncpy( ifr.ifr_name, iface, sizeof( ifr.ifr_name ) - 1 );

    if( ioctl( fd, SIOCGIFINDEX, &ifr ) < 0 )
    {
        printf("Interface %s: \n", iface);
        perror( "ioctl(SIOCGIFINDEX) failed" );
        return( 1 );
    }

    memset( &sll, 0, sizeof( sll ) );
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = ifr.ifr_ifindex;

    /* lookup the hardware type */

    if( ioctl( fd, SIOCGIFHWADDR, &ifr ) < 0 )
    {
        printf("Interface %s: \n", iface);
        perror( "ioctl(SIOCGIFHWADDR) failed" );
        return( 1 );
    }

    /* lookup iw mode */
    memset( &wrq, 0, sizeof( struct iwreq ) );
    strncpy( wrq.ifr_name, iface, IFNAMSIZ );

    if( ioctl( fd, SIOCGIWMODE, &wrq ) < 0 )
    {
        /* most probably not supported (ie for rtap ipw interface) *
         * so just assume its correctly set...                     */
        wrq.u.mode = IW_MODE_MONITOR;
    }

    if( ( ifr.ifr_hwaddr.sa_family != ARPHRD_IEEE80211 &&
          ifr.ifr_hwaddr.sa_family != ARPHRD_IEEE80211_PRISM &&
          ifr.ifr_hwaddr.sa_family != ARPHRD_IEEE80211_FULL) ||
        ( wrq.u.mode != IW_MODE_MONITOR) )
    {
        if (set_monitor( dev, fd ))
        {
            ifr.ifr_flags &= ~(IFF_UP | IFF_BROADCAST | IFF_RUNNING);

            if( ioctl( fd, SIOCSIFFLAGS, &ifr ) < 0 )
            {
                perror( "ioctl(SIOCSIFFLAGS) failed" );
                return( 1 );
            }

            if (set_monitor( dev, fd ))
            {
                printf("Error setting monitor mode on %s\n",iface);
                return( 1 );
            }
        }
    }

    /* Is interface st to up, broadcast & running ? */
    if((ifr.ifr_flags | IFF_UP | IFF_BROADCAST | IFF_RUNNING) != ifr.ifr_flags)
    {
        /* Bring interface up*/
        ifr.ifr_flags |= IFF_UP | IFF_BROADCAST | IFF_RUNNING;

        if( ioctl( fd, SIOCSIFFLAGS, &ifr ) < 0 )
        {
            perror( "ioctl(SIOCSIFFLAGS) failed" );
            return( 1 );
        }
    }
    /* bind the raw socket to the interface */

    if( bind( fd, (struct sockaddr *) &sll,
              sizeof( sll ) ) < 0 )
    {
        printf("Interface %s: \n", iface);
        perror( "bind(ETH_P_ALL) failed" );
        return( 1 );
    }

    /* lookup the hardware type */

    if( ioctl( fd, SIOCGIFHWADDR, &ifr ) < 0 )
    {
        printf("Interface %s: \n", iface);
        perror( "ioctl(SIOCGIFHWADDR) failed" );
        return( 1 );
    }

    memcpy( mac, (unsigned char*)ifr.ifr_hwaddr.sa_data, 6);

    *arptype = ifr.ifr_hwaddr.sa_family;

    if( ifr.ifr_hwaddr.sa_family != ARPHRD_IEEE80211 &&
        ifr.ifr_hwaddr.sa_family != ARPHRD_IEEE80211_PRISM &&
        ifr.ifr_hwaddr.sa_family != ARPHRD_IEEE80211_FULL )
    {
        if( ifr.ifr_hwaddr.sa_family == 1 )
            fprintf( stderr, "\nARP linktype is set to 1 (Ethernet) " );
        else
            fprintf( stderr, "\nUnsupported hardware link type %4d ",
                     ifr.ifr_hwaddr.sa_family );

        fprintf( stderr, "- expected ARPHRD_IEEE80211,\nARPHRD_IEEE80211_"
                         "FULL or ARPHRD_IEEE80211_PRISM instead.  Make\n"
                         "sure RFMON is enabled: run 'airmon-ng start %s"
                         " <#>'\nSysfs injection support was not found "
                         "either.\n\n", iface );
        return( 1 );
    }

    /* enable promiscuous mode */

    memset( &mr, 0, sizeof( mr ) );
    mr.mr_ifindex = sll.sll_ifindex;
    mr.mr_type    = PACKET_MR_PROMISC;

    if( setsockopt( fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
                    &mr, sizeof( mr ) ) < 0 )
    {
        perror( "setsockopt(PACKET_MR_PROMISC) failed" );
        return( 1 );
    }

    return( 0 );
}

static void print_driver_type(struct wifi_dev * dev) {
  
    char strbuf[512];
    char *iface="wlan0";
    FILE *f;
    char athXraw[] = "athXraw";
    int unused, n;
    pid_t pid;
    FILE *acpi;
    char r_file[128], buf[128];
    struct ifreq ifr;
    char * unused_str;
    DIR *net_ifaces;
    struct dirent *this_iface;

    //strncpy(iface, "wlan0", strlen("wlan0"));

      /* mac80211 stack detection */
    memset(strbuf, 0, sizeof(strbuf));
    snprintf(strbuf, sizeof(strbuf) - 1,
            "ls /sys/class/net/%s/phy80211/subsystem >/dev/null 2>/dev/null", iface);

    if (system(strbuf) == 0)
        dev->drivertype = DT_MAC80211_RT;

    /* IPW2200 detection */
    memset(strbuf, 0, sizeof(strbuf));
    snprintf(strbuf, sizeof(strbuf) - 1,
            "ls /sys/class/net/%s/device/inject >/dev/null 2>/dev/null", iface);

    if (system(strbuf) == 0)
        dev->drivertype = DT_IPW2200;

    /* BCM43XX detection */
    memset(strbuf, 0, sizeof(strbuf));
    snprintf(strbuf, sizeof(strbuf) - 1,
            "ls /sys/class/net/%s/device/inject_nofcs >/dev/null 2>/dev/null", iface);

    if (system(strbuf) == 0)
        dev->drivertype = DT_BCM43XX;

    /* check if wlan-ng or hostap or r8180 */
    if( strlen(iface) == 5 &&
        memcmp(iface, "wlan", 4 ) == 0 )
    {
        memset( strbuf, 0, sizeof( strbuf ) );
        snprintf( strbuf,  sizeof( strbuf ) - 1,
                  "wlancfg show %s 2>/dev/null | "
                  "grep p2CnfWEPFlags >/dev/null",
                  iface);

        if( system( strbuf ) == 0 )
        {
/*
	    if (uname( & checklinuxversion ) >= 0)
            {
                // uname succeeded 
                if (strncmp(checklinuxversion.release, "2.6.", 4) == 0
                    && strncasecmp(checklinuxversion.sysname, "linux", 5) == 0)
                {
                    // Linux kernel 2.6
                    kver = atoi(checklinuxversion.release + 4);

                    if (kver > 11)
                    {
                        // That's a kernel > 2.6.11, cannot inject
                        dev->inject_wlanng = 0;
                    }
                }
            }
*/
            dev->drivertype = DT_WLANNG;
/*            dev->wlanctlng = wiToolsPath("wlanctl-ng"); */
        }

        memset( strbuf, 0, sizeof( strbuf ) );
        snprintf( strbuf,  sizeof( strbuf ) - 1,
                  "iwpriv %s 2>/dev/null | "
                  "grep antsel_rx >/dev/null",
                  iface);

        if( system( strbuf ) == 0 )
            dev->drivertype=DT_HOSTAP;

        memset( strbuf, 0, sizeof( strbuf ) );
        snprintf( strbuf,  sizeof( strbuf ) - 1,
                    "iwpriv %s 2>/dev/null | "
                    "grep  GetAcx111Info  >/dev/null",
                    iface);

        if( system( strbuf ) == 0 )
            dev->drivertype=DT_ACX;
    }

    /* enable injection on ralink */

    if( strcmp( iface, "ra0" ) == 0 ||
        strcmp( iface, "ra1" ) == 0 ||
        strcmp( iface, "rausb0" ) == 0 ||
        strcmp( iface, "rausb1" ) == 0 )
    {
        memset( strbuf, 0, sizeof( strbuf ) );
        snprintf( strbuf,  sizeof( strbuf ) - 1,
                  "iwpriv %s rfmontx 1 >/dev/null 2>/dev/null",
                  iface );
/*        unused = system( strbuf ); */
    }

    /* check if newer athXraw interface available */

    if( ( strlen( iface ) >= 4 || strlen( iface ) <= 6 )
        && memcmp( iface, "ath", 3 ) == 0 )
    {
        dev->drivertype = DT_MADWIFI;
        memset( strbuf, 0, sizeof( strbuf ) );

        snprintf(strbuf, sizeof( strbuf ) -1,
                  "/proc/sys/net/%s/%%parent", iface);

        f = fopen(strbuf, "r");

        if (f != NULL)
        {
            // It is madwifi-ng
            dev->drivertype=DT_MADWIFING;
            fclose( f );

            /* should we force prism2 header? */

            sprintf((char *) strbuf, "/proc/sys/net/%s/dev_type", iface);
            f = fopen( (char *) strbuf,"w");
            if (f != NULL) {
                fprintf(f, "802\n");
                fclose(f);
            }

            /* Force prism2 header on madwifi-ng */
        }
        else
        {
            // Madwifi-old
            memset( strbuf, 0, sizeof( strbuf ) );
            snprintf( strbuf,  sizeof( strbuf ) - 1,
                      "sysctl -w dev.%s.rawdev=1 >/dev/null 2>/dev/null",
                      iface );

            if( system( strbuf ) == 0 )
            {

                athXraw[3] = iface[3];

                memset( strbuf, 0, sizeof( strbuf ) );
                snprintf( strbuf,  sizeof( strbuf ) - 1,
                          "ifconfig %s up", athXraw );
                unused = system( strbuf );

#if 0 /* some people reported problems when prismheader is enabled */
                memset( strbuf, 0, sizeof( strbuf ) );
                snprintf( strbuf,  sizeof( strbuf ) - 1,
                         "sysctl -w dev.%s.rawdev_type=1 >/dev/null 2>/dev/null",
                         iface );
                unused = system( strbuf );
#endif

                iface = athXraw;
            }
        }
    }

    /* test if orinoco */

    if( memcmp( iface, "eth", 3 ) == 0 )
    {
        if( ( pid = fork() ) == 0 )
        {
            close( 0 ); close( 1 ); close( 2 ); unused = chdir( "/" );
            execlp( "iwpriv", "iwpriv", iface, "get_port3", NULL );
            exit( 1 );
        }

        waitpid( pid, &n, 0 );

        if( WIFEXITED(n) && WEXITSTATUS(n) == 0 )
            dev->drivertype=DT_ORINOCO;

        memset( strbuf, 0, sizeof( strbuf ) );
        snprintf( strbuf,  sizeof( strbuf ) - 1,
                  "iwpriv %s 2>/dev/null | "
                  "grep get_scan_times >/dev/null",
                  iface);

        if( system( strbuf ) == 0 )
            dev->drivertype=DT_AT76USB;
    }

    /* test if zd1211rw */

    if( memcmp( iface, "eth", 3 ) == 0 )
    {
        if( ( pid = fork() ) == 0 )
        {
            close( 0 ); close( 1 ); close( 2 ); unused = chdir( "/" );
            execlp( "iwpriv", "iwpriv", iface, "get_regdomain", NULL );
            exit( 1 );
        }

        waitpid( pid, &n, 0 );

        if( WIFEXITED(n) && WEXITSTATUS(n) == 0 )
            dev->drivertype=DT_ZD1211RW;
    }

    if( dev->drivertype == DT_IPW2200 )
    {
        snprintf(r_file, sizeof(r_file),
            "/sys/class/net/%s/device/rtap_iface", iface);
        if ((acpi = fopen(r_file, "r")) == NULL)
            goto close_out;
        memset(buf, 0, 128);
        unused_str = fgets(buf, 128, acpi);
        buf[127]='\x00';
        //rtap iface doesn't exist
        if(strncmp(buf, "-1", 2) == 0)
        {
            //repoen for writing
            fclose(acpi);
            if ((acpi = fopen(r_file, "w")) == NULL)
                goto close_out;
            fputs("1", acpi);
            //reopen for reading
            fclose(acpi);
            if ((acpi = fopen(r_file, "r")) == NULL)
                goto close_out;
            unused_str = fgets(buf, 128, acpi);
        }
        fclose(acpi);

        //use name in buf as new iface and set original iface as main iface
        dev->main_if = (char*) malloc(strlen(iface)+1);
        memset(dev->main_if, 0, strlen(iface)+1);
        strncpy(dev->main_if, iface, strlen(iface));

        iface=(char*)malloc(strlen(buf)+1);
        memset(iface, 0, strlen(buf)+1);
        strncpy(iface, buf, strlen(buf));
    }

    /* test if rtap interface and try to find real interface */
    if( memcmp( iface, "rtap", 4) == 0 && dev->main_if == NULL)
    {
        memset( &ifr, 0, sizeof( ifr ) );
        strncpy( ifr.ifr_name, iface, sizeof( ifr.ifr_name ) - 1 );

        n = 0;

        if( ioctl( dev->fd_out, SIOCGIFINDEX, &ifr ) < 0 )
        {
            //create rtap interface
            n = 1;
        }

        net_ifaces = opendir("/sys/class/net");
        if ( net_ifaces != NULL )
        {
            while (net_ifaces != NULL && ((this_iface = readdir(net_ifaces)) != NULL))
            {
                if (this_iface->d_name[0] == '.')
                    continue;

                snprintf(r_file, sizeof(r_file),
                    "/sys/class/net/%s/device/rtap_iface", this_iface->d_name);
                if ((acpi = fopen(r_file, "r")) == NULL)
                    continue;
                if (acpi != NULL)
                {
                    dev->drivertype = DT_IPW2200;

                    memset(buf, 0, 128);
                    unused_str = fgets(buf, 128, acpi);
                    if(n==0) //interface exists
                    {
                        if (strncmp(buf, iface, 5) == 0)
                        {
                            fclose(acpi);
                            if (net_ifaces != NULL)
                            {
                                closedir(net_ifaces);
                                net_ifaces = NULL;
                            }
                            dev->main_if = (char*) malloc(strlen(this_iface->d_name)+1);
                            strcpy(dev->main_if, this_iface->d_name);
                            break;
                        }
                    }
                    else //need to create interface
                    {
                        if (strncmp(buf, "-1", 2) == 0)
                        {
                            //repoen for writing
                            fclose(acpi);
                            if ((acpi = fopen(r_file, "w")) == NULL)
                                continue;
                            fputs("1", acpi);
                            //reopen for reading
                            fclose(acpi);
                            if ((acpi = fopen(r_file, "r")) == NULL)
                                continue;
                            unused_str = fgets(buf, 128, acpi);
                            if (strncmp(buf, iface, 5) == 0)
                            {
                                if (net_ifaces != NULL)
                                {
                                    closedir(net_ifaces);
                                    net_ifaces = NULL;
                                }
                                dev->main_if = (char*) malloc(strlen(this_iface->d_name)+1);
                                strcpy(dev->main_if, this_iface->d_name);
                                fclose(acpi);
                                break;
                            }
                        }
                    }
                    fclose(acpi);
                }
            }
            if (net_ifaces != NULL)
                closedir(net_ifaces);
        }
    }

    if(0)
    fprintf(stderr, "Interface %s -> driver: %s\n", iface,
        szaDriverTypes[dev->drivertype]);

    if (openraw(dev, dev->fd_out, &dev->arptype_out, dev->pl_mac) != 0) {
        goto close_out;
    }

    // don't use the same file descriptor for in and out on bcm43xx,
    //   as you read from the interface, but write into a file in /sys/...
     
    if(!(dev->drivertype == DT_BCM43XX) && !(dev->drivertype == DT_IPW2200))
        dev->fd_in = dev->fd_out;
    else
    {
        // if bcm43xx or ipw2200, swap both fds
        n=dev->fd_out;
        dev->fd_out=dev->fd_in;
        dev->fd_in=n;
    }

    dev->arptype_in = dev->arptype_out;

  return;
close_out:
  printf("Me mandan salir\n");
}

static int get_channel(struct wifi_dev *dev)
{
    struct iwreq wrq;
    int fd, frequency;
    int chan=0;

    memset( &wrq, 0, sizeof( struct iwreq ) );
/*
    if(dev->main_if)
        strncpy( wrq.ifr_name, dev->main_if, IFNAMSIZ );
    else
        strncpy( wrq.ifr_name, wi_get_ifname(wi), IFNAMSIZ );
*/
    strncpy( wrq.ifr_name, "wlan0", strlen("wlan0") );

    fd = dev->fd_in;

    if( ioctl( fd, SIOCGIWFREQ, &wrq ) < 0 )
        return( -1 );

    frequency = wrq.u.freq.m;
    if (frequency > 100000000)
        frequency/=100000;
    else if (frequency > 1000000)
        frequency/=1000;

    if (frequency > 1000)
        chan = getChannelFromFrequency(frequency);
    else chan = frequency;

    return chan;
}

static int my_read(struct wifi_dev *dev, unsigned char *buf, int count,
		      struct rx_info *ri)
{
    unsigned char tmpbuf[4096];

    int caplen, n, got_signal, got_noise, got_channel, fcs_removed;

    caplen = n = got_signal = got_noise = got_channel = fcs_removed = 0;

    if((unsigned)count > sizeof(tmpbuf))
        return( -1 );

    if( ( caplen = read( dev->fd_in, tmpbuf, count ) ) < 0 )
    {
        if( errno == EAGAIN )
            return( 0 );

        perror( "read failed" );
        return( -1 );
    }

    memset( buf, 0, sizeof( buf ) );
	
    /* XXX */
    if (ri) 
    	memset(ri, 0, sizeof(*ri));

    if( dev->arptype_in == ARPHRD_IEEE80211_PRISM )
    {
        /* skip the prism header */
        if( tmpbuf[7] == 0x40 )
        {
            /* prism54 uses a different format */
            if(ri)
            {
                ri->ri_power = tmpbuf[0x33];
                ri->ri_noise = *(unsigned int *)( tmpbuf + 0x33 + 12 );
                ri->ri_rate = (*(unsigned int *)( tmpbuf + 0x33 + 24 ))*500000;

                got_signal = 1;
                got_noise = 1;
            }

            n = 0x40;
        }
        else
        {
            if(ri)
            {
                ri->ri_mactime = *(u_int64_t*)( tmpbuf + 0x5C - 48 );
                ri->ri_channel = *(unsigned int *)( tmpbuf + 0x5C - 36 );
                ri->ri_power = *(unsigned int *)( tmpbuf + 0x5C );
                ri->ri_noise = *(unsigned int *)( tmpbuf + 0x5C + 12 );
                ri->ri_rate = (*(unsigned int *)( tmpbuf + 0x5C + 24 ))*500000;

//                if( ! memcmp( iface[i], "ath", 3 ) )
                if( dev->drivertype == DT_MADWIFI )
                    ri->ri_power -= *(int *)( tmpbuf + 0x68 );
                if( dev->drivertype == DT_MADWIFING )
                    ri->ri_power -= *(int *)( tmpbuf + 0x68 );

                got_channel = 1;
                got_signal = 1;
                got_noise = 1;
            }

            n = *(int *)( tmpbuf + 4 );
        }

        if( n < 8 || n >= caplen )
            return( 0 );
    }

    if( dev->arptype_in == ARPHRD_IEEE80211_FULL )
    { 
        struct ieee80211_radiotap_iterator iterator;
        struct ieee80211_radiotap_header *rthdr;

	rthdr = (struct ieee80211_radiotap_header *) tmpbuf;

        if (ieee80211_radiotap_iterator_init(&iterator, rthdr, caplen) < 0)
            return (0);

        // go through the radiotap arguments we have been given
        // by the driver

        while (ri && (ieee80211_radiotap_iterator_next(&iterator) >= 0)) {
            switch (iterator.this_arg_index) {

            case IEEE80211_RADIOTAP_TSFT:
                ri->ri_mactime = le64_to_cpu(*((uint64_t*)iterator.this_arg));
                break;

            case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
            	if(!got_signal) {
					if( *iterator.this_arg < 127 )
						ri->ri_power = *iterator.this_arg;
					else
						ri->ri_power = *iterator.this_arg - 255;

					got_signal = 1;
				}
                break;

            case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
                if(!got_signal) {
                    if( *iterator.this_arg < 127 )
                        ri->ri_power = *iterator.this_arg;
                    else
                        ri->ri_power = *iterator.this_arg - 255;

                    got_signal = 1;
                }
                break;

            case IEEE80211_RADIOTAP_DBM_ANTNOISE:
            	if(!got_noise) {
					if( *iterator.this_arg < 127 )
						ri->ri_noise = *iterator.this_arg;
					else
						ri->ri_noise = *iterator.this_arg - 255;

					got_noise = 1;
				}
                break;

            case IEEE80211_RADIOTAP_DB_ANTNOISE:
                if(!got_noise) {
                    if( *iterator.this_arg < 127 )
                        ri->ri_noise = *iterator.this_arg;
                    else
                        ri->ri_noise = *iterator.this_arg - 255;

                    got_noise = 1;
                }
                break;

            case IEEE80211_RADIOTAP_ANTENNA:
                ri->ri_antenna = *iterator.this_arg;
                break;

            case IEEE80211_RADIOTAP_CHANNEL:
                ri->ri_channel = *iterator.this_arg;
                got_channel = 1;
                break;

            case IEEE80211_RADIOTAP_RATE:
                ri->ri_rate = (*iterator.this_arg) * 500000;
                break;

            case IEEE80211_RADIOTAP_FLAGS:
                //is the CRC visible at the end?
                // remove
                 
                if ( *iterator.this_arg &
                    IEEE80211_RADIOTAP_F_FCS )
                {
                    fcs_removed = 1;
                    caplen -= 4;
                }

                if ( *iterator.this_arg &
                    IEEE80211_RADIOTAP_F_RX_BADFCS )
                    return( 0 );

                break;

            }
        }

        n = le16_to_cpu(rthdr->it_len);

        if( n <= 0 || n >= caplen )
            return( 0 );
    }
    
    caplen -= n;

    //detect fcs at the end, even if the flag wasn't set and remove it
    if( fcs_removed == 0 && check_crc_buf_osdep( tmpbuf+n, caplen - 4 ) == 1 )
    {
        caplen -= 4;
    }

    memcpy( buf, tmpbuf + n, caplen );

    if(ri && !got_channel)
        ri->ri_channel = get_channel(dev);

    return( caplen );
}

static int my_read2(struct wifi_dev *dev, unsigned char *buf, int count,
		      struct rx_info *ri)
{
    unsigned char tmpbuf[4096];

	int caplen, n, got_signal, got_noise, got_channel, fcs_removed;

	caplen = n = got_signal = got_noise = got_channel = fcs_removed = 0;
	
    if((unsigned)count > sizeof(tmpbuf))
        return( -1 );

    printf("Antes de read\n");
    
    if( ( caplen = read( dev->fd_in, tmpbuf, count ) ) < 0 )
    {
        if( errno == EAGAIN )
            return( 0 );

        perror( "read failed" );
        return( -1 );
    }
    
    printf("Despues de read\n");
    
    switch (dev->drivertype) {
    case DT_MADWIFI:
        caplen -= 4;    /* remove the FCS for madwifi-old! only (not -ng)*/
        break;
    default:
        break;
    }

    memset( buf, 0, sizeof( buf ) );

    /* XXX */
    if (ri)
    	memset(ri, 0, sizeof(*ri));

    if( dev->arptype_in == ARPHRD_IEEE80211_PRISM )
    {
        /* skip the prism header */
        if( tmpbuf[7] == 0x40 )
        {
            /* prism54 uses a different format */
            if(ri)
            {
                ri->ri_power = tmpbuf[0x33];
                ri->ri_noise = *(unsigned int *)( tmpbuf + 0x33 + 12 );
                ri->ri_rate = (*(unsigned int *)( tmpbuf + 0x33 + 24 ))*500000;

                got_signal = 1;
                got_noise = 1;
            }

            n = 0x40;
        }
        else
        {
            if(ri)
            {
                ri->ri_mactime = *(u_int64_t*)( tmpbuf + 0x5C - 48 );
                ri->ri_channel = *(unsigned int *)( tmpbuf + 0x5C - 36 );
                ri->ri_power = *(unsigned int *)( tmpbuf + 0x5C );
                ri->ri_noise = *(unsigned int *)( tmpbuf + 0x5C + 12 );
                ri->ri_rate = (*(unsigned int *)( tmpbuf + 0x5C + 24 ))*500000;

//                if( ! memcmp( iface[i], "ath", 3 ) )
                if( dev->drivertype == DT_MADWIFI )
                    ri->ri_power -= *(int *)( tmpbuf + 0x68 );
                if( dev->drivertype == DT_MADWIFING )
                    ri->ri_power -= *(int *)( tmpbuf + 0x68 );

                got_channel = 1;
                got_signal = 1;
                got_noise = 1;
            }

            n = *(int *)( tmpbuf + 4 );
        }

        if( n < 8 || n >= caplen )
            return( 0 );
    }

    if( dev->arptype_in == ARPHRD_IEEE80211_FULL )
    {
        struct ieee80211_radiotap_iterator iterator;
        struct ieee80211_radiotap_header *rthdr;

        rthdr = (struct ieee80211_radiotap_header *) tmpbuf;

        if (ieee80211_radiotap_iterator_init(&iterator, rthdr, caplen) < 0)
            return (0);

        /* go through the radiotap arguments we have been given
         * by the driver
         */

        while (ri && (ieee80211_radiotap_iterator_next(&iterator) >= 0)) {

            switch (iterator.this_arg_index) {

            case IEEE80211_RADIOTAP_TSFT:
                ri->ri_mactime = le64_to_cpu(*((uint64_t*)iterator.this_arg));
                break;

            case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
            	if(!got_signal) {
					if( *iterator.this_arg < 127 )
						ri->ri_power = *iterator.this_arg;
					else
						ri->ri_power = *iterator.this_arg - 255;

					got_signal = 1;
				}
                break;

            case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
                if(!got_signal) {
                    if( *iterator.this_arg < 127 )
                        ri->ri_power = *iterator.this_arg;
                    else
                        ri->ri_power = *iterator.this_arg - 255;

                    got_signal = 1;
                }
                break;

            case IEEE80211_RADIOTAP_DBM_ANTNOISE:
            	if(!got_noise) {
					if( *iterator.this_arg < 127 )
						ri->ri_noise = *iterator.this_arg;
					else
						ri->ri_noise = *iterator.this_arg - 255;

					got_noise = 1;
				}
                break;

            case IEEE80211_RADIOTAP_DB_ANTNOISE:
                if(!got_noise) {
                    if( *iterator.this_arg < 127 )
                        ri->ri_noise = *iterator.this_arg;
                    else
                        ri->ri_noise = *iterator.this_arg - 255;

                    got_noise = 1;
                }
                break;

            case IEEE80211_RADIOTAP_ANTENNA:
                ri->ri_antenna = *iterator.this_arg;
                break;

            case IEEE80211_RADIOTAP_CHANNEL:
                ri->ri_channel = *iterator.this_arg;
                got_channel = 1;
                break;

            case IEEE80211_RADIOTAP_RATE:
                ri->ri_rate = (*iterator.this_arg) * 500000;
                break;

            case IEEE80211_RADIOTAP_FLAGS:
                /* is the CRC visible at the end?
                 * remove
                 */
                if ( *iterator.this_arg &
                    IEEE80211_RADIOTAP_F_FCS )
                {
                    fcs_removed = 1;
                    caplen -= 4;
                }

                if ( *iterator.this_arg &
                    IEEE80211_RADIOTAP_F_RX_BADFCS )
                    return( 0 );

                break;

            }
        }

        n = le16_to_cpu(rthdr->it_len);

        if( n <= 0 || n >= caplen )
            return( 0 );
    }

    caplen -= n;

    //detect fcs at the end, even if the flag wasn't set and remove it
    if( fcs_removed == 0 && check_crc_buf_osdep( tmpbuf+n, caplen - 4 ) == 1 )
    {
        caplen -= 4;
    }

    memcpy( buf, tmpbuf + n, caplen );

    if(ri && !got_channel)
        ri->ri_channel = get_channel(dev);

    return( caplen );
}

int open_card(struct wifi_dev * dev) {
    /* open raw socks */
    if( ( dev->fd_in = socket( PF_PACKET, SOCK_RAW,
                              htons( ETH_P_ALL ) ) ) < 0 )
    {
        perror( "socket(PF_PACKET) failed" );
        if( getuid() != 0 )
            fprintf( stderr, "This program requires root privileges.\n" );
        return( 1 );
    }

    if( ( dev->fd_main = socket( PF_PACKET, SOCK_RAW,
                              htons( ETH_P_ALL ) ) ) < 0 )
    {
        perror( "socket(PF_PACKET) failed" );
        if( getuid() != 0 )
            fprintf( stderr, "This program requires root privileges.\n" );
        return( 1 );
    }
    
    if( ( dev->fd_out = socket( PF_PACKET, SOCK_RAW,
                               htons( ETH_P_ALL ) ) ) < 0 )
    {
        perror( "socket(PF_PACKET) failed" );
        //goto close_in;
    }
    return( 0 );
}

static void close_card(struct wifi_dev * dev) {
	if (dev->fd_in)
		close(dev->fd_in);
	if (dev->fd_out)
		close(dev->fd_out);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////								//////////////////////////
///////////////////	PARA IMPRIMIR UN PAQUETE DE AERODUMP-NC			//////////////////////////
///////////////////								//////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

int list_add_packet(struct pkt_buf **list, int length, unsigned char* packet)
{
    struct pkt_buf *next = *list;

    if(length <= 0) return 1;
    if(packet == NULL) return 1;
    if(list == NULL) return 1;

    *list = (struct pkt_buf*) malloc(sizeof(struct pkt_buf));
    if( *list == NULL ) return 1;
    (*list)->packet = (unsigned char*) malloc(length);
    if( (*list)->packet == NULL ) return 1;

    memcpy((*list)->packet,  packet, length);
    (*list)->next = next;
    (*list)->length = length;
    gettimeofday( &((*list)->ctime), NULL);

    return 0;
}

int remove_namac(unsigned char* mac)
{
    struct NA_info *na_cur = NULL;
    struct NA_info *na_prv = NULL;

    if(mac == NULL)
        return( -1 );

    na_cur = T.na_1st;
    na_prv = NULL;

    while( na_cur != NULL )
    {
        if( ! memcmp( na_cur->namac, mac, 6 ) )
            break;

        na_prv = na_cur;
        na_cur = na_cur->next;
    }

    /* if it's known, remove it */
    if( na_cur != NULL )
    {
        /* first in linked list */
        if(na_cur == T.na_1st)
        {
            T.na_1st = na_cur->next;
        }
        else
        {
            na_prv->next = na_cur->next;
        }
        free(na_cur);
        na_cur=NULL;
    }

    return( 0 );
}

int dump_add_packet( unsigned char *h80211, int caplen, struct rx_info *ri)
{
    int seq, i, n, msd;
    unsigned char bssid[6];
    unsigned char stmac[6];
    unsigned char namac[6];
    unsigned char *p, c;
    
    struct pcap_pkthdr pkh;
    
    struct AP_info *ap_cur = NULL;
    struct ST_info *st_cur = NULL;
    struct NA_info *na_cur = NULL;
    struct AP_info *ap_prv = NULL;
    struct ST_info *st_prv = NULL;
    struct NA_info *na_prv = NULL;

    
    // skip packets smaller than a 802.11 header
    if( caplen < 24 )
        goto write_packet;

    // skip (uninteresting) control frames
    if( ( h80211[0] & 0x0C ) == 0x04 ) {
        printf("Frame de control\n");
	exit(0);
        goto write_packet;
    }

    // grab the sequence number (Byte 22 y 23 de la cabecera)
    seq = ((h80211[22]>>4)+(h80211[23]<<4));

    // Localizamos la dirección MAC del AP: (Miramos el tipo de Frame en el campo Duration / ID que está en el primer byte de la cabecera)
    switch( h80211[1] & 3 )
    {
        case  0: memcpy( bssid, h80211 + 16, 6 ); break;  //Adhoc (Address 3)
        case  1: memcpy( bssid, h80211 +  4, 6 ); break;  //ToDS (Address 1)
        case  2: memcpy( bssid, h80211 + 10, 6 ); break;  //FromDS (Address 2)
        case  3: memcpy( bssid, h80211 + 10, 6 ); break;  //WDS -> Transmitter taken as BSSID (Address 2)
    }

    if( memcmp(T.f_bssid, NULL_MAC, 6) != 0 )
    {
        if( memcmp(T.f_netmask, NULL_MAC, 6) != 0 )
        {
	    //Por si queremos filtrar ciertas mac's
            //if(is_filtered_netmask(bssid)) return(1);
        }
        else
        {
	    printf("Entro en la movida del f_bssid que no se que es\n");
	    exit(0);
            if( memcmp(T.f_bssid, bssid, 6) != 0 ) return(1);
        }
    }

    // Actualizamos la lista de APs
    ap_cur = T.ap_1st;
    ap_prv = NULL;

    while( ap_cur != NULL )
    {
        if( ! memcmp( ap_cur->bssid, bssid, 6 ) )
            break;

        ap_prv = ap_cur;
        ap_cur = ap_cur->next;
    }

    /* Si es un nuevo AP lo inicializamos y lo añadimos */

    if( ap_cur == NULL )
    {
        if( ! ( ap_cur = (struct AP_info *) malloc(
                         sizeof( struct AP_info ) ) ) )
        {
            perror( "malloc failed" );
            return( 1 );
        }

        /* Si es una mac desconcida, la quitamos */
        //remove_namac(bssid);

        memset( ap_cur, 0, sizeof( struct AP_info ) );

        if( T.ap_1st == NULL )
            T.ap_1st = ap_cur;
        else
            ap_prv->next  = ap_cur;

        memcpy( ap_cur->bssid, bssid, 6 );
		//if (ap_cur->manuf == NULL) {
		  //Por si queremos sacar el fabricante de la tarjeta
		  //ap_cur->manuf = get_manufacturer(ap_cur->bssid[0], ap_cur->bssid[1], ap_cur->bssid[2]);
		//}

        ap_cur->prev = ap_prv;

        ap_cur->tinit = time( NULL );
        ap_cur->tlast = time( NULL );

        ap_cur->avg_power   = -1;
        ap_cur->best_power  = -1;
        ap_cur->power_index = -1;

        for( i = 0; i < NB_PWR; i++ )
            ap_cur->power_lvl[i] = -1;

        ap_cur->channel    = -1;
        ap_cur->max_speed  = -1;
        //ap_cur->security   = 0;

        //ap_cur->uiv_root = uniqueiv_init();

        //ap_cur->nb_dataps = 0;
        //ap_cur->nb_data_old = 0;
        gettimeofday(&(ap_cur->tv), NULL);

        T.ap_end = ap_cur;

        ap_cur->nb_bcn     = 0;

        //ap_cur->rx_quality = 0;
        ap_cur->fcapt      = 0;
        ap_cur->fmiss      = 0;
        ap_cur->last_seq   = 0;
        //gettimeofday( &(ap_cur->ftimef), NULL);
        //gettimeofday( &(ap_cur->ftimel), NULL);
        //gettimeofday( &(ap_cur->ftimer), NULL);

        ap_cur->ssid_length = 0;
        //ap_cur->timestamp = 0;
    }

    /* Actualizamos la última vez que se vio el AP */

    ap_cur->tlast = time( NULL );

    /* only update power if packets comes from
     * the AP: either type == mgmt and SA != BSSID,
     * or FromDS == 1 and ToDS == 0 */

    if( ( ( h80211[1] & 3 ) == 0 &&
            memcmp( h80211 + 10, bssid, 6 ) == 0 ) ||
        ( ( h80211[1] & 3 ) == 2 ) )//Es un paquete que viene del AP
    {
        ap_cur->power_index = ( ap_cur->power_index + 1 ) % NB_PWR;
        ap_cur->power_lvl[ap_cur->power_index] = ri->ri_power;

        ap_cur->avg_power = 0;

        for( i = 0, n = 0; i < NB_PWR; i++ )
        {
            if( ap_cur->power_lvl[i] != -1 )
            {
                ap_cur->avg_power += ap_cur->power_lvl[i];
                n++;
            }
        }

        if( n > 0 )
        {
            ap_cur->avg_power /= n;
            if( ap_cur->avg_power > ap_cur->best_power )
            {
                ap_cur->best_power = ap_cur->avg_power;
            }
        }
        else
            ap_cur->avg_power = -1;

        //if(ap_cur->fcapt == 0 && ap_cur->fmiss == 0) gettimeofday( &(ap_cur->ftimef), NULL);
        if(ap_cur->last_seq != 0) ap_cur->fmiss += (seq - ap_cur->last_seq - 1);
        ap_cur->last_seq = seq;
        ap_cur->fcapt++;
        //gettimeofday( &(ap_cur->ftimel), NULL);

//         if(ap_cur->fcapt >= QLT_COUNT) update_rx_quality();
    }

    //Beacon: Publicitar SSID
    if( h80211[0] == 0x80 )
    {
        ap_cur->nb_bcn++;
    }

    ap_cur->nb_pkt++;

    /* Localizamos la MAC de la station en la cabecera 802.11 */

    switch( h80211[1] & 3 )
    {
        case  0: //Adhoc la mac de la estación = Address 2

            /* if management, check that SA != BSSID */

            if( memcmp( h80211 + 10, bssid, 6 ) == 0 )
                goto skip_station;

            memcpy( stmac, h80211 + 10, 6 );
            break;

	case  1:  //ToDS: Del AP al STA: Address 2

            /* ToDS packet, must come from a client */

            memcpy( stmac, h80211 + 10, 6 );
            break;

        case  2:

	    //FromDS: Paquete del STA al AP: Address 1
	    
            /* FromDS packet, reject broadcast MACs */

            if( (h80211[4]%2) != 0 ) goto skip_station;
            memcpy( stmac, h80211 +  4, 6 ); break;

        default: goto skip_station;
    }

    /* Actualizamos la lista enlazada de stations */

    st_cur = T.st_1st;
    st_prv = NULL;

    while( st_cur != NULL )
    {
        if( ! memcmp( st_cur->stmac, stmac, 6 ) )
            break;

        st_prv = st_cur;
        st_cur = st_cur->next;
    }

    /* Si es una station nueva, la inicializamos y la añadimos */

    if( st_cur == NULL )
    {
        if( ! ( st_cur = (struct ST_info *) malloc(
                         sizeof( struct ST_info ) ) ) )
        {
            perror( "malloc failed" );
            return( 1 );
        }

        /* Si es una mac desconocida, la eliminamos */
        remove_namac(stmac);

        memset( st_cur, 0, sizeof( struct ST_info ) );

        if( T.st_1st == NULL )
            T.st_1st = st_cur;
        else
            st_prv->next  = st_cur;

        memcpy( st_cur->stmac, stmac, 6 );

		//if (st_cur->manuf == NULL) {
			//Obtener el fabricante de la tarjeta
			//st_cur->manuf = get_manufacturer(st_cur->stmac[0], st_cur->stmac[1], st_cur->stmac[2]);
		//}

        st_cur->prev = st_prv;

        st_cur->tinit = time( NULL );
        st_cur->tlast = time( NULL );

        st_cur->power = -1;
        st_cur->rate_to = -1;
        st_cur->rate_from = -1;

        st_cur->probe_index = -1;
        st_cur->missed  = 0;
        st_cur->lastseq = 0;
        //gettimeofday( &(st_cur->ftimer), NULL);

        for( i = 0; i < NB_PRB; i++ )
        {
            memset( st_cur->probes[i], 0, sizeof(
                    st_cur->probes[i] ) );
            st_cur->ssid_length[i] = 0;
        }

        T.st_end = st_cur;
    }

    if( st_cur->base == NULL ||
        memcmp( ap_cur->bssid, BROADCAST, 6 ) != 0 )
        st_cur->base = ap_cur;

    //update bitrate to station
    if( (st_cur != NULL) && ( h80211[1] & 3 ) == 2 )
        st_cur->rate_to = ri->ri_rate;

    /* update the last time seen */
    st_cur->tlast = time( NULL );

    /* only update power if packets comes from the
     * client: either type == Mgmt and SA != BSSID,
     * or FromDS == 0 and ToDS == 1 */
    if( ( ( h80211[1] & 3 ) == 0 &&
            memcmp( h80211 + 10, bssid, 6 ) != 0 ) ||
        ( ( h80211[1] & 3 ) == 1 ) )
    {
        st_cur->power = ri->ri_power;
        st_cur->rate_from = ri->ri_rate;

        if(st_cur->lastseq != 0)
        {
            msd = seq - st_cur->lastseq - 1;
            if(msd > 0 && msd < 1000)
                st_cur->missed += msd;
        }
        st_cur->lastseq = seq;
    }

    st_cur->nb_pkt++;

skip_station:

    /* packet parsing: Probe Request */

    if( h80211[0] == 0x40 && st_cur != NULL )
    {
        p = h80211 + 24;

        while( p < h80211 + caplen )
        {
            if( p + 2 + p[1] > h80211 + caplen )
                break;

            if( p[0] == 0x00 && p[1] > 0 && p[2] != '\0' &&
                ( p[1] > 1 || p[2] != ' ' ) )
            {
//                n = ( p[1] > 32 ) ? 32 : p[1];
                n = p[1];

                for( i = 0; i < n; i++ )
                    if( p[2 + i] > 0 && p[2 + i] < ' ' )
                        goto skip_probe;

                /* got a valid ASCII probed ESSID, check if it's
                   already in the ring buffer */

                for( i = 0; i < NB_PRB; i++ )
                    if( memcmp( st_cur->probes[i], p + 2, n ) == 0 )
                        goto skip_probe;

                st_cur->probe_index = ( st_cur->probe_index + 1 ) % NB_PRB;
                memset( st_cur->probes[st_cur->probe_index], 0, 256 );
                memcpy( st_cur->probes[st_cur->probe_index], p + 2, n ); //twice?!
                st_cur->ssid_length[st_cur->probe_index] = n;

                for( i = 0; i < n; i++ )
                {
                    c = p[2 + i];
                    if( c == 0 || ( c > 126 && c < 160 ) ) c = '.';  //could also check ||(c>0 && c<32)
                    st_cur->probes[st_cur->probe_index][i] = c;
                }
            }

            p += 2 + p[1];
        }
    }

skip_probe:
//
write_packet:

    if(ap_cur != NULL)
    {
//         if( h80211[0] == 0x80 && T.one_beacon){
//             if( !ap_cur->beacon_logged )
//                 ap_cur->beacon_logged = 1;
//             else return ( 0 );
//         }
    }

//     if(ap_cur != NULL)
//     {
//         if(ap_cur->security != 0 && T.f_encrypt != 0 && ((ap_cur->security & T.f_encrypt) == 0))
//         {
//             return(1);
//         }
//     }

    /* this changes the local ap_cur, st_cur and na_cur variables and should be the last check befor the actual write */
    if(caplen < 24 && caplen >= 10 && h80211[0])
    {
        /* RTS || CTS || ACK || CF-END || CF-END&CF-ACK*/
        //(h80211[0] == 0xB4 || h80211[0] == 0xC4 || h80211[0] == 0xD4 || h80211[0] == 0xE4 || h80211[0] == 0xF4)

        /* use general control frame detection, as the structure is always the same: mac(s) starting at [4] */
        if(h80211[0] & 0x04)
        {
            p=h80211+4;
            while(p <= h80211+16 && p<=h80211+caplen)
            {
                memcpy(namac, p, 6);

                if(memcmp(namac, NULL_MAC, 6) == 0)
                {
                    p+=6;
                    continue;
                }

                if(memcmp(namac, BROADCAST, 6) == 0)
                {
                    p+=6;
                    continue;
                }

//                 if(T.hide_known)
//                 {
//                     /* check AP list */
//                     ap_cur = T.ap_1st;
//                     ap_prv = NULL;
// 
//                     while( ap_cur != NULL )
//                     {
//                         if( ! memcmp( ap_cur->bssid, namac, 6 ) )
//                             break;
// 
//                         ap_prv = ap_cur;
//                         ap_cur = ap_cur->next;
//                     }
// 
//                     /* if it's an AP, try next mac */
// 
//                     if( ap_cur != NULL )
//                     {
//                         p+=6;
//                         continue;
//                     }
// 
//                     /* check ST list */
//                     st_cur = T.st_1st;
//                     st_prv = NULL;
// 
//                     while( st_cur != NULL )
//                     {
//                         if( ! memcmp( st_cur->stmac, namac, 6 ) )
//                             break;
// 
//                         st_prv = st_cur;
//                         st_cur = st_cur->next;
//                     }
// 
//                     /* if it's a client, try next mac */
// 
//                     if( st_cur != NULL )
//                     {
//                         p+=6;
//                         continue;
//                     }
//                 }

                /* not found in either AP list or ST list, look through NA list */
                na_cur = T.na_1st;
                na_prv = NULL;

                while( na_cur != NULL )
                {
                    if( ! memcmp( na_cur->namac, namac, 6 ) )
                        break;

                    na_prv = na_cur;
                    na_cur = na_cur->next;
                }

                /* update our chained list of unknown stations */
                /* if it's a new mac, add it */

                if( na_cur == NULL )
                {
                    if( ! ( na_cur = (struct NA_info *) malloc(
                                    sizeof( struct NA_info ) ) ) )
                    {
                        perror( "malloc failed" );
                        return( 1 );
                    }

                    memset( na_cur, 0, sizeof( struct NA_info ) );

                    if( T.na_1st == NULL )
                        T.na_1st = na_cur;
                    else
                        na_prv->next  = na_cur;

                    memcpy( na_cur->namac, namac, 6 );

                    na_cur->prev = na_prv;

                    gettimeofday(&(na_cur->tv), NULL);
                    na_cur->tinit = time( NULL );
                    na_cur->tlast = time( NULL );

                    na_cur->power   = -1;
                    na_cur->channel = -1;
                    na_cur->ack     = 0;
                    na_cur->ack_old = 0;
                    na_cur->ackps   = 0;
                    na_cur->cts     = 0;
                    na_cur->rts_r   = 0;
                    na_cur->rts_t   = 0;
                }

                /* update the last time seen & power*/

                na_cur->tlast = time( NULL );
                na_cur->power = ri->ri_power;
                na_cur->channel = ri->ri_channel;

                switch(h80211[0] & 0xF0)
                {
                    case 0xB0:
                        if(p == h80211+4)
                            na_cur->rts_r++;
                        if(p == h80211+10)
                            na_cur->rts_t++;
                        break;

                    case 0xC0:
                        na_cur->cts++;
                        break;

                    case 0xD0:
                        na_cur->ack++;
                        break;

                    default:
                        na_cur->other++;
                        break;
                }

                /*grab next mac (for rts frames)*/
                p+=6;
            }
        }
    }

//     if( T.f_cap != NULL &&  caplen >= 10)
//     {
//         pkh.caplen = pkh.len = caplen;
// 
//         gettimeofday( &tv, NULL );
// 
//         pkh.tv_sec  =   tv.tv_sec;
//         pkh.tv_usec = ( tv.tv_usec & ~0x1ff ) + ri->ri_power + 64;
// 
//         n = sizeof( pkh );
// 
//         if( fwrite( &pkh, 1, n, G.f_cap ) != (size_t) n )
//         {
//             perror( "fwrite(packet header) failed" );
//             return( 1 );
//         }
// 
//         fflush( stdout );
// 
//         n = pkh.caplen;
// 
//         if( fwrite( h80211, 1, n, G.f_cap ) != (size_t) n )
//         {
//             perror( "fwrite(packet data) failed" );
//             return( 1 );
//         }
// 
//         fflush( stdout );
//     }

    return( 0 );
}

void print_clients() {

  struct CLI_info *cli_cur;
  struct PWR_in_second *pwr_cur;
  struct DEBUG_info *deb_cur;
  struct time_t *ltime;
  char pwr_buf[100];
  char estado[50];
  
  cli_cur = T.clients;
  
  while ( cli_cur != NULL ) 
  {
    if (cli_cur->is_ap) {
      if (cli_cur->is_connected) {
	memcpy(estado, "AP\0", 3);
      } else {
	memcpy(estado, "AP raro\0", 8);
      }
    } else {
      if (cli_cur->is_connected) {
	memcpy(estado, "STA conected!\0", 14);
      } else {
	memcpy(estado, "STA alone\0", 10);
      }      
    }
    printf("%s: %02X:%02X:%02X:%02X:%02X:%02X\n", estado,
		cli_cur->mac[0], cli_cur->mac[1],
		cli_cur->mac[2], cli_cur->mac[3],
		cli_cur->mac[4], cli_cur->mac[5] );
    pwr_cur = cli_cur->pwr;
    
    while ( pwr_cur != NULL )
    {
      deb_cur = pwr_cur->debug;
//      if (pwr_cur->tipo != 0)
//      {
//      if (deb_cur->ToFromDS != 0) 
//      {
	printf("\t Tipo: %d TF: %d PWR: %d ", pwr_cur->tipo, deb_cur->ToFromDS, pwr_cur->power);
      
	ltime = localtime( &pwr_cur->second );

	strftime(pwr_buf, sizeof(pwr_buf), "%a %Y-%m-%d %H:%M:%S %Z", ltime);
	printf(" --> %s\n", pwr_buf);
      
/*
	printf("Debug-------------------------------\n");
	printf("Tipo: %d TF: %d A1: %02X:%02X:%02X:%02X:%02X:%02X A2: %02X:%02X:%02X:%02X:%02X:%02X A3: %02X:%02X:%02X:%02X:%02X:%02X A4: %02X:%02X:%02X:%02X:%02X:%02X\n",
	     pwr_cur->tipo, deb_cur->ToFromDS, deb_cur->addr1[0], deb_cur->addr1[1], deb_cur->addr1[2], deb_cur->addr1[3], deb_cur->addr1[4], deb_cur->addr1[5], 
	     deb_cur->addr2[0], deb_cur->addr2[1], deb_cur->addr2[2], deb_cur->addr2[3], deb_cur->addr2[4], deb_cur->addr2[5],
	     deb_cur->addr3[0], deb_cur->addr3[1], deb_cur->addr3[2], deb_cur->addr3[3], deb_cur->addr3[4], deb_cur->addr3[5],
	     deb_cur->addr4[0], deb_cur->addr4[1], deb_cur->addr4[2], deb_cur->addr4[3], deb_cur->addr4[4], deb_cur->addr4[5] );
*/
//      }
//      }
      pwr_cur = pwr_cur->next;
    }
    
    cli_cur = cli_cur->next;
  }
}

int register_client( unsigned char *h80211, int caplen, struct rx_info *ri)
{
    unsigned char mac[6];
    unsigned char *p, c;
    int is_ap, is_connected;
    unsigned char addr1[6], addr2[6], addr3[6], addr4[6];
    
    struct CLI_info *cli_cur, *cli_prv;
    struct PWR_in_second *pwr_cur, *pwr_prv;
    struct DEBUG_info *deb_cur, *deb_prv;
  
    
    p=h80211+4;
    memcpy(addr1, p, 6);
    p=h80211+10;
    memcpy(addr2, p, 6);
    p=h80211+16;
    memcpy(addr3, p, 6);
    p=h80211+18;
    memcpy(addr4, p, 6);
    
    
    memcpy(mac, addr2, 6);
    
    switch( h80211[1] & 3 )
    {
        case  0: 
		 if (( memcmp(addr3, BROADCAST, 6) != 0 ) && ( memcmp(addr3, NULL_MAC, 6) != 0 ) )
		    memcpy( mac, addr3, 6 ); 
		 if (memcmp( addr2, addr3, 6 ) == 0) 
		 {
		   is_ap = 1;
		   is_connected = 0;
		 }
		 else
		 {
		    is_ap = 0;
		    //??? is connected??
		    is_connected = 0;
		 }
		 break;  //Adhoc
        case  1: 
		 is_ap = 0; 
		 //??? is connected???
		 is_connected = 0;
		 break;  //ToDS
        case  2: 
		 is_ap = 1; 
		 is_connected = 0;
		 break;  //FromDS
        case  3: is_ap = 0; 
		 is_connected = 0;
		 break;  //WDS -> Transmitter taken as BSSID
    }
    
    cli_cur = T.clients;
	    
    while( cli_cur != NULL )
    {
         if( ! memcmp( cli_cur->mac, mac, 6 ) )
                break;

	  cli_prv = cli_cur;
          cli_cur = cli_cur->next;
    }

    // Si es una nueva MAC => Actualizamos nuestra lista enlazada de clientes

    if( cli_cur == NULL )
    {

	if( ! ( cli_cur = (struct CLI_info *) malloc(
                              sizeof( struct CLI_info ) ) ) )
        {
              perror( "malloc failed" );
              return( 1 );
        }

        memset( cli_cur, 0, sizeof( struct CLI_info ) );

        if( T.clients == NULL )
	  T.clients = cli_cur;
        else
          cli_prv->next  = cli_cur;

        memcpy( cli_cur->mac, mac, 6 );
	cli_cur->pwr = NULL;
	//FIXME: Así solo se registra lo que se encuentre primero
	cli_cur->is_ap = is_ap;
	cli_cur->is_connected = is_connected;
    }
    //Si ha cambiado el estado de la STA.
    if (cli_cur->is_connected != is_connected)
      cli_cur->is_connected = is_connected;
    pwr_cur = cli_cur->pwr;
    pwr_prv = NULL;
    //Obtenemos la potencía registrada la última vez
    while (pwr_cur != NULL)
    {
	pwr_prv = pwr_cur;
	pwr_cur = pwr_cur->next;
    }

     if ( ( pwr_prv == NULL ) || (pwr_prv->power != ri->ri_power) && !cli_cur->is_ap) 
     {
	if ( ! ( pwr_cur = (struct PWR_in_second *) malloc(
			      sizeof( struct PWR_in_second ) ) ) ) 
	{
	    perror( "malloc failed" );
	    return( 1 );
	}
	
	memset( pwr_cur, 0, sizeof( struct PWR_in_second) );
		
	if ( cli_cur->pwr == NULL )
	  cli_cur->pwr = pwr_cur;
	else
	  pwr_prv->next = pwr_cur;
		
	pwr_cur->power = ri->ri_power;
	pwr_cur->second = time( NULL );
	pwr_cur->debug = NULL;
    
	//Tipo de trama: 0 Management, 1 Control, 2 Data, 3 unknown
	if( ( h80211[0] & 0x0C ) == 0x00 )
	  pwr_cur->tipo = 0;
	else
	  if( ( h80211[0] & 0x0C ) == 0x04 )
	    pwr_cur->tipo = 1;
	  else
	    if( ( h80211[0] & 0x0C ) == 0x08 )
	      pwr_cur->tipo = 2;
	    else 
	      pwr_cur->tipo = 3;
	
	//Si es una STA la teníamos marcada como no conectada y envía datos al AP, la marcamos como conectada.
	if ( !cli_cur->is_ap && pwr_cur->tipo == 2 && !cli_cur->is_connected)
	  cli_cur->is_connected = 1;
	
	//Debug
	deb_cur = pwr_cur->debug;
	deb_prv = NULL;
	//Obtenemos la última traza;
	while (deb_cur != NULL)
	{
	  deb_prv = deb_cur;
	  deb_cur = deb_cur->next;
	}
    
	if ( ! ( deb_cur = (struct DEBUG_info *) malloc(
			  sizeof( struct DEBUG_info ) ) ) )
	{
	    perror( "malloc failed" );
	    return( 1 );
	}
    
	memset( deb_cur, 0, sizeof( struct DEBUG_info) );
    
	if ( pwr_cur->debug == NULL)
	  pwr_cur->debug = deb_cur;
	else
	  deb_prv->next = deb_cur;
    
	deb_cur->ToFromDS = h80211[1] & 3;
	memcpy(deb_cur->addr1, addr1, 6);
	memcpy(deb_cur->addr2, addr2, 6);
	memcpy(deb_cur->addr3, addr3, 6);
	memcpy(deb_cur->addr4, addr4, 6);  
    } else {
      //Si es un AP, solo guardamos la media de los PWR recibidos (no se va a mover)
      if (cli_cur->is_ap)
	pwr_prv->power = (pwr_prv->power + ri->ri_power) / 2;
    }

}

int dump_packet_to_client(unsigned char *h80211, unsigned char mac[6], int is_ap, int is_connected, struct rx_info *ri)
{
    struct CLI_info *cli_cur = NULL;
    struct CLI_info *cli_prv = NULL;
    struct PWR_in_second *pwr_cur = NULL;
    struct PWR_in_second *pwr_prv = NULL;
    struct DEBUG_info *deb_cur = NULL;
    struct DEBUG_info *deb_prv = NULL;
    
    cli_cur = T.clients;

    while( cli_cur != NULL )
    {
         if( ! memcmp( cli_cur->mac, mac, 6 ) )
                break;

	  cli_prv = cli_cur;
          cli_cur = cli_cur->next;
    }

    // Si es una nueva MAC => Actualizamos nuestra lista enlazada de clientes

    if( cli_cur == NULL )
    {

	if( ! ( cli_cur = (struct CLI_info *) malloc(
                              sizeof( struct CLI_info ) ) ) )
        {
              perror( "malloc failed" );
              return( 1 );
        }

        memset( cli_cur, 0, sizeof( struct CLI_info ) );

        if( T.clients == NULL )
	  T.clients = cli_cur;
        else
          cli_prv->next  = cli_cur;

        memcpy( cli_cur->mac, mac, 6 );
	cli_cur->pwr = NULL;
	cli_cur->is_ap = is_ap;
	cli_cur->is_connected = is_connected;
    }

    //Si ha cambiado el estado de la STA de desconectada a conectada, lo salvamos.
    if (cli_cur->is_connected == 0 &&  is_connected == 1)
      cli_cur->is_connected = 1;
    pwr_cur = cli_cur->pwr;
    pwr_prv = NULL;

    //Obtenemos la potencía registrada la última vez
    while (pwr_cur != NULL)
    {
	pwr_prv = pwr_cur;
	pwr_cur = pwr_cur->next;
    }

    //Si es una estación o es la primera vez que se detecta el AP registramos el nuevo valor
    if ( ( cli_cur->pwr == NULL ) || (!cli_cur->is_ap) )
    {

	if ( ! ( pwr_cur = (struct PWR_in_second *) malloc(
			      sizeof( struct PWR_in_second ) ) ) ) 
	{
	    perror( "malloc failed" );
	    return( 1 );
	}

	memset( pwr_cur, 0, sizeof( struct PWR_in_second) );
	if ( cli_cur->pwr == NULL )
	  cli_cur->pwr = pwr_cur;
	else 
	  pwr_prv->next = pwr_cur;

	pwr_cur->power = ri->ri_power;
	pwr_cur->second = time( NULL );
	pwr_cur->debug = NULL;
	
	//Tipo de trama: 0 Management, 1 Control, 2 Data, 3 unknown
	if ( ( h80211[0] & 0x0C ) == 0x00 )
	  pwr_cur->tipo = 0;
	else
	  if( ( h80211[0] & 0x0C ) == 0x04 )
	    pwr_cur->tipo = 1;
	  else
	    if( ( h80211[0] & 0x0C ) == 0x08 )
	      pwr_cur->tipo = 2;
	    else 
	      pwr_cur->tipo = 3;
	
	//Si es una STA la teníamos marcada como no conectada y envía datos al AP, la marcamos como conectada.
	if ( !cli_cur->is_ap && pwr_cur->tipo == 2 && !cli_cur->is_connected)
	  cli_cur->is_connected = 1;
	
	//Debug
	deb_cur = pwr_cur->debug;
	deb_prv = NULL;
	//Obtenemos la última traza;
	while (deb_cur != NULL)
	{
	  deb_prv = deb_cur;
	  deb_cur = deb_cur->next;
	}
    
	if ( ! ( deb_cur = (struct DEBUG_info *) malloc(
			  sizeof( struct DEBUG_info ) ) ) )
	{
	    perror( "malloc failed" );
	    return( 1 );
	}
    
	memset( deb_cur, 0, sizeof( struct DEBUG_info) );
    
	if ( pwr_cur->debug == NULL)
	  pwr_cur->debug = deb_cur;
	else
	  deb_prv->next = deb_cur;
    
	deb_cur->ToFromDS = h80211[1] & 3;

	memcpy(deb_cur->addr1, h80211+4, 6);
	memcpy(deb_cur->addr2, h80211+10, 6);
	memcpy(deb_cur->addr3, h80211+16, 6);
	memcpy(deb_cur->addr4, h80211+24, 6);  
    } else {
      //Si es un AP, solo guardamos la media de los PWR recibidos (no se va a mover)
      if (cli_cur->is_ap)
	pwr_prv->power = (pwr_prv->power + ri->ri_power) / 2;
    }    
}



int save_packet_to_file(char *h80211, int caplen, unsigned char mac[6], int mi_tipo, int tiny, int power)
{
  FILE *fp;
  int len;
  unsigned char linea[150];
  struct time_t *ltime;  
  time_t second;
  unsigned char tmp_mac[6];

  fp = fopen("/tmp/packets.txt", "a");
  if (fp== NULL) {
    printf("Error abriendo el fichero");
  }
  sprintf(linea, "\n\n**********************************************************************************\n\n\0");
  len = strlen(linea);
  fwrite(linea, len, 1, fp);  
  second = time( NULL );
  ltime = localtime( &second );  
  strftime(linea, sizeof(linea), "Hora: %Y-%m-%d %H:%M:%S\n", ltime);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);  
  
  sprintf(linea, "\nmi_tipo: %d y tiny: %d\n", mi_tipo, tiny);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);

  sprintf(linea, "\nlongitud del mensaje: %d\n", caplen);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);

  
  switch( h80211[1] & 3 )
  {
    case  0: sprintf(linea, "ToDS: 0 y FromDS: 0 -------- Adhoc\n\n ADDRESS 2\n\n"); break;  //Adhoc (Address 3)
    case  1: sprintf(linea, "ToDS: 1 y FromDS: 0 -------- ToDS\n\n ADDRESS 2 \n\n"); break;  //ToDS (Address 1)
    case  2: sprintf(linea, "ToDS: 0 y FromDS: 1 -------- FromDS\n\n ADDRESS 2 \n\n"); break;  //FromDS (Address 2)
    case  3: sprintf(linea, "ToDS: 1 y FromDS: 1 -------- WDS\n\n ADDRESS 4 \n\n"); break;  //WDS -> Transmitter taken as BSSID (Address 2)
  }
  len = strlen(linea);
  fwrite(linea, len, 1, fp);    
  
  memcpy(tmp_mac, h80211 +  4, 6 );
  sprintf(linea, "\n Addr1: %02X:%02X:%02X:%02X:%02X:%02X\n", tmp_mac[0], tmp_mac[1], tmp_mac[2], tmp_mac[3], tmp_mac[4], tmp_mac[5]);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);    
  memcpy(tmp_mac, h80211 +  10, 6 );
  sprintf(linea, "Addr2: %02X:%02X:%02X:%02X:%02X:%02X\n", tmp_mac[0], tmp_mac[1], tmp_mac[2], tmp_mac[3], tmp_mac[4], tmp_mac[5]);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);    
  memcpy(tmp_mac, h80211 +  16, 6 );
  sprintf(linea, "Addr3: %02X:%02X:%02X:%02X:%02X:%02X\n", tmp_mac[0], tmp_mac[1], tmp_mac[2], tmp_mac[3], tmp_mac[4], tmp_mac[5]);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);    
  memcpy(tmp_mac, h80211 +  24, 6 );
  sprintf(linea, "Addr4: %02X:%02X:%02X:%02X:%02X:%02X\n", tmp_mac[0], tmp_mac[1], tmp_mac[2], tmp_mac[3], tmp_mac[4], tmp_mac[5]);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);    
  sprintf(linea, "\n\nPOWER: %d\n", power);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);      
  if (mi_tipo == 0) {
    sprintf(linea, "\nEnviado sin clasificar\n");    
    len = strlen(linea);
    fwrite(linea, len, 1, fp);    
  } else {
    if (tiny == 1)
      sprintf(linea, "\nEnviado como punto no asociado\n");
    else
      if (mi_tipo == 1)
	sprintf(linea, "\nEnviado como AP\n");
      else
	sprintf(linea, "\nEnviado como STA conectada\n");
    len = strlen(linea);
    fwrite(linea, len, 1, fp);
  }
  sprintf(linea, "\n MAC Enviada: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);  
  len = strlen(linea);
  fwrite(linea, len, 1, fp);  
  sprintf(linea, "Byte 0: %02X\0\n", h80211[0]);
  len = strlen(linea);
  fwrite(linea, len, 1, fp);
  switch ( h80211[0] & 12 ) {
    case 0:
	    sprintf(linea, "Tipo: Management Packet\n");
	    break;
    case 4:
	    sprintf(linea, "Tipo: Control Packet\n");
	    break;
    case 8:
	    sprintf(linea, "Tipo: Data Packet\n");
	    break;
    default: 
	    sprintf(linea, "Tipo: DESCONOCIDO\n");
  }
  len = strlen(linea);
  fwrite(linea, len, 1, fp);
  switch ( h80211[0] & 240 ) {
    case 0:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Association Request\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (0)\n");
	    break;
    case 128:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Association Response\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (128)\n");
	    break;
    case 64:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Reassociation request\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (64)\n");
	    break;
    case 192:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Association Request\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (192)\n");
	    break;
    case 32:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Reassociation Response\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (32)\n");
	    break;
    case 160:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Probe Response\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (192)\n");
	    break;
    case 16:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Beacon\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (16)\n");
	    break;
    case 144:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: ATIM\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (144)\n");
	    break;
    case 80:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Disassociation\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (80)\n");
	    break;
    case 208:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Authentication\n");
	    else if ( h80211[0] & 12 == 4 )
	      sprintf(linea, "Subtipo: RTS\n");	    	    
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (208)\n");
	    break;
    case 48:
	    if ( (h80211[0] & 12) == 0 )
	      sprintf(linea, "Subtipo: Deauthentication\n");
	    else if ( h80211[0] & 12 == 4 )
	      sprintf(linea, "Subtipo: CTS\n");	    
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (48)\n");
	    break;
    case 176:
	    if ( (h80211[0] & 12) == 4 )
	      sprintf(linea, "Subtipo: ACK\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (176)\n");
	    break;
    case 112:
	    if ( (h80211[0] & 12) == 4 )
	      sprintf(linea, "Subtipo: CF End\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (112)\n");
	    break;
    case 240:
	    if ( (h80211[0] & 12) == 4 )
	      sprintf(linea, "Subtipo: CF End + CF ACK\n");
	    else
	      sprintf(linea, "Subtipo: DESCONOCIDO (240)\n");
	    break;
    default:
	    sprintf(linea, "Subtipo: TOTALMENTE DESCONOCIDO\n");
  }
  len = strlen(linea);
  fwrite(linea, len, 1, fp);  
  //fwrite("\n", sizeof(char), 1, fp);
  fclose(fp);
  return(0);
}

int dump_packet_old ( unsigned char *h80211, int caplen, struct rx_info *ri)
{
    unsigned char *p;
    unsigned char bssid[6];
    unsigned char mac[6];
    int mi_tipo = 0;
    int tiny = 0;
      
    // El paquete es más pequeño que la cabecera
    if( caplen < 24 )
        goto write_packet;

    // Frame de control
    if( ( h80211[0] & 0x0C ) == 0x04 ) {
        goto write_packet;
    }

    // Localizamos la dirección MAC del AP: (Miramos el tipo de Frame en el campo Duration / ID que está en el primer byte de la cabecera)
    switch( h80211[1] & 3 )
    {
        case  0: memcpy( bssid, h80211 + 16, 6 ); break;  //Adhoc (Address 3)
        case  1: memcpy( bssid, h80211 +  4, 6 ); break;  //ToDS (Address 1)
        case  2: memcpy( bssid, h80211 + 10, 6 ); break;  //FromDS (Address 2)
        case  3: memcpy( bssid, h80211 + 10, 6 ); break;  //WDS -> Transmitter taken as BSSID (Address 2)
    }

    if( ( ( h80211[1] & 3 ) == 0 &&
            memcmp( h80211 + 10, bssid, 6 ) == 0 ) ||
        ( ( h80211[1] & 3 ) == 2 ) )//Es un paquete que viene del AP
    {
      memcpy( mac, h80211 + 10, 6 );
      mi_tipo = 1;
      //Es un paquete del AP (is_ap = 1 & is_connected = 0)
      dump_packet_to_client(h80211, mac, 1, 0, ri);
    }

    // Localizamos la MAC de la station en la cabecera 802.11
    switch( h80211[1] & 3 )
    {
        case  0: //Adhoc la mac de la estación = Address 2

            /* if management, check that SA != BSSID */
            if( memcmp( h80211 + 10, bssid, 6 ) == 0 )
                goto write_packet;

            memcpy( mac, h80211 + 10, 6 );
            break;

	case  1:  //ToDS: Del AP al STA: Address 2

            /* ToDS packet, must come from a client */

            memcpy( mac, h80211 + 10, 6 );
            break;

        case  2:

	    //FromDS: Paquete del STA al AP: Address 1
	    
            /* FromDS packet, reject broadcast MACs */

            if( (h80211[4]%2) != 0 ) goto write_packet;
            //memcpy( mac, h80211 +  4, 6 ); break;

        default: goto write_packet;
    }

    /* only update power if packets comes from the
     * client: either type == Mgmt and SA != BSSID,
     * or FromDS == 0 and ToDS == 1 */
    if( ( ( h80211[1] & 3 ) == 0 &&
            memcmp( h80211 + 10, bssid, 6 ) != 0 ) ||
        ( ( h80211[1] & 3 ) == 1 ) )
    {
      memcpy( mac, h80211 + 10, 6 );
      mi_tipo = 2;
      //Es un paquete de una STA conectada (is_ap = 1 & is_connected = 0)
      dump_packet_to_client(h80211, mac, 0, 1, ri);      
    }
    
write_packet:    
    
    /* this changes the local ap_cur, st_cur and na_cur variables and should be the last check befor the actual write */
    if(caplen < 24 && caplen >= 10 && h80211[0])
    {
	tiny = 1;
        /* RTS || CTS || ACK || CF-END || CF-END&CF-ACK*/
        //(h80211[0] == 0xB4 || h80211[0] == 0xC4 || h80211[0] == 0xD4 || h80211[0] == 0xE4 || h80211[0] == 0xF4)

        /* use general control frame detection, as the structure is always the same: mac(s) starting at [4] */
        if(h80211[0] & 0x04)
        {
            p=h80211+4;
            while(p <= h80211+16 && p<=h80211+caplen)
            {
                memcpy(mac, p, 6);

                if(memcmp(mac, NULL_MAC, 6) == 0)
                {
                    p+=6;
                    continue;
                }

                if(memcmp(mac, BROADCAST, 6) == 0)
                {
                    p+=6;
                    continue;
                }

		//Es un paquete de una sta desconocida (is_ap = 0 & is_connected = 0)
		dump_packet_to_client(h80211, mac, 0, 0, ri);
                /*grab next mac (for rts frames)*/
                p+=6;
            }
        }
    }
    save_packet_to_file(h80211, caplen, mac, mi_tipo, tiny, ri->ri_power);
    return( 0 );
}

int dump_packet ( unsigned char *h80211, int caplen, struct rx_info *ri)
{
    unsigned char *p;
    unsigned char bssid[6];
    unsigned char mac[6];
    int mi_tipo = 0;
    int tiny = 0;
    int is_ap = 0;
    int is_connected = 0;
      
    // El paquete es más pequeño que la cabecera
    if( caplen < 24 ) {
	tiny = 1;
        goto write_packet;
    }
    // Frame de control
    if( ( h80211[0] & 0x0C ) == 0x04 ) {
        goto write_packet;
    }

    // Localizamos la dirección MAC del AP: (Miramos el tipo de Frame en el campo Duration / ID que está en el primer byte de la cabecera)
    switch( h80211[1] & 3 )
    {
        case  0: memcpy( bssid, h80211 + 16, 6 ); break;  //Adhoc (Address 3)
        case  1: memcpy( bssid, h80211 +  4, 6 ); break;  //ToDS (Address 1)
        case  2: memcpy( bssid, h80211 + 10, 6 ); break;  //FromDS (Address 2)
        case  3: memcpy( bssid, h80211 + 10, 6 ); break;  //WDS -> Transmitter taken as BSSID (Address 2)
    }

    if( ( ( h80211[1] & 3 ) == 0 &&
            memcmp( h80211 + 10, bssid, 6 ) == 0 ) ||
        ( ( h80211[1] & 3 ) == 2 ) )//Es un paquete que viene del AP
    {
      mi_tipo = 1;
      is_ap = 1;
    }

    // Localizamos la MAC de la station en la cabecera 802.11
    switch( h80211[1] & 3 )
    {
        case  0: //Adhoc la mac de la estación = Address 2

            /* if management, check that SA != BSSID */
            if( memcmp( h80211 + 10, bssid, 6 ) == 0 )
                goto write_packet;
            break;

	case  1:
            break;

        case  2:

            if( (h80211[4]%2) != 0 ) goto write_packet;

        default: goto write_packet;
    }

    if( ( ( h80211[1] & 3 ) == 0 &&
            memcmp( h80211 + 10, bssid, 6 ) != 0 ) ||
        ( ( h80211[1] & 3 ) == 1 ) )
    {
      mi_tipo = 2;      
      if((memcmp(bssid, NULL_MAC, 6) != 0) && (memcmp(bssid, BROADCAST, 6) != 0))
	is_connected = 1;
    }
    
write_packet:    

    //La fuente siempre la podemos obtener de la segunda dirección
    memcpy(mac, h80211+10, 6);

    if(memcmp(mac, NULL_MAC, 6) == 0)
      return(0);
    
    if(memcmp(mac, BROADCAST, 6) == 0)
      return(0);
    
    dump_packet_to_client(h80211, mac, is_ap, is_connected, ri);              
    save_packet_to_file(h80211, caplen, mac, mi_tipo, tiny, ri->ri_power);
    return( 0 );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////								//////////////////////////
///////////////////	FIN IMPRIMIR UN PAQUETE DE AERODUMP-NC			//////////////////////////
///////////////////								//////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////								//////////////////////////
///////////////////	PARA ENVIAR LOS DATOS EN UN JSON X CURL			//////////////////////////
///////////////////								//////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

char *packet_to_json() {
  struct CLI_info *cli_cur;
  struct PWR_in_second *pwr_cur;
  char cur_mac[17];
  struct time_t *ltime;
  char cur_timestamp[100];
  /*Creating a json object*/
  json_object * jobj = json_object_new_object();
  
  json_object *jshop_id = json_object_new_string(TCONF.shop_id);
  json_object *jsensor_secret = json_object_new_string(TCONF.sensor_secret);
  
  json_object_object_add(jobj,"id", jshop_id);
  json_object_object_add(jobj,"key", jsensor_secret);
  
  json_object *jvalues = json_object_new_array();
  
  cli_cur = T.clients;
  while( cli_cur != NULL )
  {
    json_object *value = json_object_new_object();
    sprintf(cur_mac, "%02X:%02X:%02X:%02X:%02X:%02X", cli_cur->mac[0], cli_cur->mac[1], cli_cur->mac[2], cli_cur->mac[3], cli_cur->mac[4], cli_cur->mac[5]);
    json_object *jmac = json_object_new_string(cur_mac);
    json_object_object_add(value, "mac", jmac);
    json_object *jis_ap = json_object_new_int(cli_cur->is_ap);
    json_object_object_add(value, "is_ap", jis_ap);    
    json_object *jis_connected = json_object_new_int(cli_cur->is_connected);
    json_object_object_add(value, "is_connected", jis_connected);
    json_object *jrssi = json_object_new_array();
    //Añadir rssi para esa mac.
    pwr_cur = cli_cur->pwr;
    while ( pwr_cur != NULL )
    {
      json_object *jcur_rssi = json_object_new_object();
      
      json_object *jvalue = json_object_new_int(pwr_cur->power);
      
      ltime = localtime( &pwr_cur->second );
      strftime(cur_timestamp, sizeof(cur_timestamp), "%Y-%m-%d %H:%M:%S", ltime);
      json_object *jtimestamp = json_object_new_string(cur_timestamp);
      
      json_object_object_add(jcur_rssi, "value", jvalue);
      json_object_object_add(jcur_rssi, "timestamp", jtimestamp);
      
      json_object_array_add(jrssi, jcur_rssi);
      pwr_cur = pwr_cur->next;
    }
    json_object_object_add(value, "rssi", jrssi);
    json_object_array_add(jvalues, value);
    cli_cur = cli_cur->next;
  }
  json_object_object_add(jobj, "values", jvalues);
  return json_object_to_json_string(jobj);
  
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp)
{
  struct HTTP_msg *pooh = (struct HTTP_msg *)userp;
 
  if(size*nmemb < 1)
    return 0;
 
  if(pooh->sizeleft) {
    *(char *)ptr = pooh->readptr[0]; /* copy one single byte */ 
    pooh->readptr++;                 /* advance pointer */ 
    pooh->sizeleft--;                /* less data left */ 
    return 1;                        /* we return 1 byte at a time! */ 
  }
 
  return 0;                          /* no more data left to deliver */ 
}

int save_to_file(char *json_msg)
{
  FILE *fp;
  int len;

  fp = fopen("/tmp/rssis.txt", "a");
  if (fp== NULL) {
    printf("Error abriendo el fichero");
  }  
  len = strlen(json_msg);
  printf("Escribiendo : %d", len);
  fwrite(json_msg, len, 1, fp);
  fclose(fp);
}

int send_http_data_old(char *json_msg)
{
  CURL *curl;
  CURLcode res;

  struct HTTP_msg msg;

  msg.readptr = json_msg;
  msg.sizeleft = strlen(json_msg);

  curl = curl_easy_init();
  if(curl) {
    /* First set the URL that is about to receive our POST. */ 
    curl_easy_setopt(curl, CURLOPT_URL, TCONF.tesla_server);

    /* Now specify we want to POST data */ 
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    /* we want to use our own read function */ 
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);

    /* pointer to pass to our read function */ 
    curl_easy_setopt(curl, CURLOPT_READDATA, &msg);

    /* get verbose debug output please */ 
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    /*
      If you use POST to a HTTP 1.1 server, you can send data without knowing
      the size before starting the POST if you use chunked encoding. You
      enable this by adding a header like "Transfer-Encoding: chunked" with
      CURLOPT_HTTPHEADER. With HTTP 1.0 or without chunked transfer, you must
      specify the size in the request.
    */ 
#ifdef USE_CHUNKED
    {
      struct curl_slist *chunk = NULL;

      chunk = curl_slist_append(chunk, "Transfer-Encoding: chunked");
      res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
      /* use curl_slist_free_all() after the *perform() call to free this
         list again */ 
    }
#else
    /* Set the expected POST size. If you want to POST large amounts of data,
       consider CURLOPT_POSTFIELDSIZE_LARGE */ 
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (curl_off_t)msg.sizeleft);

//    struct curl_slist *headers = NULL;
//    curl_slist_append(headers, "Accept: application/json");
//    curl_slist_append(headers, "Content-Type: application/json");
//    curl_slist_append(headers, "charsets: utf-8");
#endif

#ifdef DISABLE_EXPECT
    /*
      Using POST with HTTP 1.1 implies the use of a "Expect: 100-continue"
      header.  You can disable this header with CURLOPT_HTTPHEADER as usual.
      NOTE: if you want chunked transfer too, you need to combine these two
      since you can only set one list of headers with CURLOPT_HTTPHEADER. */ 

    /* A less good option would be to enforce HTTP 1.0, but that might also
       have other implications. */ 
    {
      struct curl_slist *chunk = NULL;

      chunk = curl_slist_append(chunk, "Expect:");
      res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
      /* use curl_slist_free_all() after the *perform() call to free this
         list again */ 
    }
#endif

    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);

    /* always cleanup */ 
    curl_easy_cleanup(curl);
  }
  return 0;
}

int send_http_data(char *json_msg)
{
  CURL *curl;
  CURLcode res;

  struct HTTP_msg msg;

  msg.readptr = json_msg;
  msg.sizeleft = strlen(json_msg);

  //Guardamos las señales en un fichero para hacer trazas
  save_to_file(json_msg);
  
  curl = curl_easy_init();
  if(curl) {
    /* First set the URL that is about to receive our POST. */ 
    curl_easy_setopt(curl, CURLOPT_URL, TCONF.tesla_server);

    /* Now specify we want to POST data */ 
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_msg);

    /* get verbose debug output please */ 
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);//1L);
printf("Antes de enviar\n");
    struct curl_slist *hdr = NULL;    
printf("Antes de curl_slist\n");    
    hdr = curl_slist_append(hdr, "Content-type: application/json");
printf("Antes de setopt\n");
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
printf("Antes de perform\n");    
    res = curl_easy_perform(curl);
printf("Antes de limpiar\n");
printf("petición enviada a: %s\n", TCONF.tesla_server);
    /* always cleanup */ 
    curl_easy_cleanup(curl);
printf("Limpio!");
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////								//////////////////////////
///////////////////	FIN ENVIAR LOS DATOS EN UN JSON X CURL			//////////////////////////
///////////////////								//////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

int read_configuration(const char *c_file) {
 config_t cfg, *global_conf;
 const char *str_conf, *aux;
 int int_conf;

 global_conf = &cfg;
 config_init(global_conf);
    
 if (!config_read_file(global_conf, c_file)) {
    fprintf(stderr, "%s:%d - %s\n",
    config_error_file(global_conf),
    config_error_line(global_conf),
    config_error_text(global_conf));
    config_destroy(global_conf);
    return(EXIT_FAILURE);
 }
 
 if (config_lookup_string(global_conf, "shop_id", &str_conf)) {
   TCONF.shop_id = (char *) malloc(strlen(str_conf));
   strcpy(TCONF.shop_id, str_conf);
   str_conf = NULL;
   printf("shop_id: %s\n", TCONF.shop_id);
 } else TCONF.shop_id = SHOP_ID;
 if (config_lookup_string(global_conf, "sensor_secret", &str_conf)) {
   TCONF.sensor_secret = (char *) malloc(strlen(str_conf));
   strcpy(TCONF.sensor_secret, str_conf);
   str_conf = NULL;
   printf("sensor_secret: %s\n", TCONF.sensor_secret);    
 } else TCONF.sensor_secret = SENSOR_SECRET;
 if (config_lookup_string(global_conf, "tesla_server", &str_conf)) {
   TCONF.tesla_server = (char *) malloc(strlen(str_conf));
   strcpy(TCONF.tesla_server, str_conf);
   str_conf = NULL;
   printf("tesla_server: %s\n", TCONF.tesla_server);    
 } else TCONF.tesla_server = CEREBELLUM;
 if (config_lookup_int(global_conf, "seed_second", &int_conf)) {
   TCONF.seed_second = int_conf;
   printf("seed_second: %d\n", TCONF.seed_second);     
 } else TCONF.seed_second = SEED_SECOND;
 config_destroy(global_conf);

 return 0;  
}

int main( int argc, char *argv[] )
{
  struct wifi_dev 	*wifi_device;
  unsigned char 	*h80211;
  unsigned char		buffer[4096];
  struct rx_info 	*ri;
  int 			pkg_length=0;
  int 			field;
  time_t		now;
  struct tm		*tm;
  char *json_msg;
  
  T.clients = NULL;
  int c;
  const char *config_file = "conf/tesla.cfg";
  //Lectura de parámetros
  while ((c = getopt (argc, argv, "c:")) != -1) {
    switch (c)
    {
      case 'c':
	config_file = optarg;
        break;
      default:
	printf("Usando el archivo de configuración por defecto %s\n", config_file);
    }
  }
 
  read_configuration(config_file);
  /* Allocate wif & private state */
  wifi_device = malloc(sizeof(*wifi_device));
  if (!wifi_device)
    exit(1);
  memset(wifi_device, 0, sizeof(*wifi_device));  
  
  printf("Abriendo interface de red wifi\n");
  open_card(wifi_device);
  printf("Averiguando driver...\n");
  print_driver_type(wifi_device);
  printf("Tipo de driver: %s\n", szaDriverTypes[wifi_device->drivertype]);
  printf("ARP Type IN: %d\n", wifi_device->arptype_in);
  printf("Leyendo paquetes...\n");
  
  
  memset(buffer, 0, sizeof(buffer));
  h80211 = buffer;
  ri = malloc(sizeof(*ri));
  
  while (1) {
    pkg_length = my_read2(wifi_device, h80211, sizeof(buffer), ri);
    /*
    printf("\nLeido paquete de longitud: %d\n\n", pkg_length);
    
    if ( pkg_length <= 10 ) {
      //Descarto paquete:
      printf("Descartado, Tipo: %d ", h80211[1] & 3);
      printf ("Address %02X:%02X:%02X:%02X:%02X:%02X ", h80211[4], h80211[5], h80211[6], h80211[7], h80211[8], h80211[9]);  
      printf("\n");
      continue;
    }
    */
    
    dump_packet(h80211, pkg_length, ri);
    //Enviamos los datos transcurrido un minuto, y limpiamos la cache
    now = time(0);
    if ((tm = localtime (&now)) == NULL) {
        printf ("Error extracting time stuff\n");
        return 1;
    }

    printf("\n\tSegundo %d\n", tm->tm_sec);
    if ( tm->tm_sec == TCONF.seed_second ) 
    {
      json_msg = packet_to_json();
      send_http_data(json_msg);
      printf("Mensaje enviado %s\n", json_msg);
      //Cambiar a convertir a json y enviar por http
      //print_clients();
      printf("Antes de liberar los clientes");
      free(T.clients);
      T.clients = NULL;
      //break;
    }
  }
  printf("Cerrando interface de red wifi\n");  
  close_card(wifi_device);
  printf("Hello Tesla\n");
  return(0);
}