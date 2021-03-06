#ifndef _TESLASENSOR_H_
#define _TESLASENSOR_H_

//Valores a sacar de BD

#define SHOP_ID "520d44ed64ec6158db000003"
#define SENSOR_SECRET "54ec692a71d65120d28145b9369b6aabfc9e25c6"
#define CEREBELLUM "http://192.168.1.18:3000/feed"
#define SEED_SECOND 0
//Fin valores a sacar de BD

struct HTTP_msg {
  const char *readptr;
  int sizeleft;
};

struct tx_info {
        unsigned int     ti_rate;
};

struct rx_info {
        uint64_t ri_mactime;
        int32_t ri_power;
        int32_t ri_noise;
        uint32_t ri_channel;
        uint32_t ri_freq;
        uint32_t ri_rate;
        uint32_t ri_antenna;
} __packed;

const unsigned long int crc_tbl_osdep[256] =
{
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

//////////////////////// RADIOTAP
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#ifndef unlikely
#define	unlikely(x) (x)
#endif

/**
 * struct ieee80211_radiotap_iterator - tracks walk thru present radiotap args
 * @rtheader: pointer to the radiotap header we are walking through
 * @max_length: length of radiotap header in cpu byte ordering
 * @this_arg_index: IEEE80211_RADIOTAP_... index of current arg
 * @this_arg: pointer to current radiotap arg
 * @arg_index: internal next argument index
 * @arg: internal next argument pointer
 * @next_bitmap: internal pointer to next present u32
 * @bitmap_shifter: internal shifter for curr u32 bitmap, b0 set == arg present
 */

struct ieee80211_radiotap_iterator {
	struct ieee80211_radiotap_header *rtheader;
	int max_length;
	int this_arg_index;
	u8 * this_arg;

	int arg_index;
	u8 * arg;
	u32 *next_bitmap;
	u32 bitmap_shifter;
};

int ieee80211_radiotap_iterator_init(
	struct ieee80211_radiotap_iterator * iterator,
	struct ieee80211_radiotap_header * radiotap_header,
	int max_length);

int ieee80211_radiotap_iterator_next(
	struct ieee80211_radiotap_iterator * iterator);

/* Radiotap header version (from official NetBSD feed) */
#define IEEE80211RADIOTAP_VERSION	"1.5"
/* Base version of the radiotap packet header data */
#define PKTHDR_RADIOTAP_VERSION		0

/* A generic radio capture format is desirable. There is one for
 * Linux, but it is neither rigidly defined (there were not even
 * units given for some fields) nor easily extensible.
 *
 * I suggest the following extensible radio capture format. It is
 * based on a bitmap indicating which fields are present.
 *
 * I am trying to describe precisely what the application programmer
 * should expect in the following, and for that reason I tell the
 * units and origin of each measurement (where it applies), or else I
 * use sufficiently weaselly language ("is a monotonically nondecreasing
 * function of...") that I cannot set false expectations for lawyerly
 * readers.
 */

/* XXX tcpdump/libpcap do not tolerate variable-length headers,
 * yet, so we pad every radiotap header to 64 bytes. Ugh.
 */
#define IEEE80211_RADIOTAP_HDRLEN	64

/* The radio capture header precedes the 802.11 header.
 * All data in the header is little endian on all platforms.
 */
struct ieee80211_radiotap_header {
	u8 it_version;		/* Version 0. Only increases
				 * for drastic changes,
				 * introduction of compatible
				 * new fields does not count.
				 */
	u8 it_pad;
	u16 it_len;		/* length of the whole
				 * header in bytes, including
				 * it_version, it_pad,
				 * it_len, and data fields.
				 */
	u32 it_present;		/* A bitmap telling which
				 * fields are present. Set bit 31
				 * (0x80000000) to extend the
				 * bitmap by another 32 bits.
				 * Additional extensions are made
				 * by setting bit 31.
				 */
};

#define IEEE80211_RADIOTAP_PRESENT_EXTEND_MASK 0x80000000

/* Name                                 Data type    Units
 * ----                                 ---------    -----
 *
 * IEEE80211_RADIOTAP_TSFT              __le64       microseconds
 *
 *      Value in microseconds of the MAC's 64-bit 802.11 Time
 *      Synchronization Function timer when the first bit of the
 *      MPDU arrived at the MAC. For received frames, only.
 *
 * IEEE80211_RADIOTAP_CHANNEL           2 x __le16   MHz, bitmap
 *
 *      Tx/Rx frequency in MHz, followed by flags (see below).
 *
 * IEEE80211_RADIOTAP_FHSS              __le16       see below
 *
 *      For frequency-hopping radios, the hop set (first byte)
 *      and pattern (second byte).
 *
 * IEEE80211_RADIOTAP_RATE              u8           500kb/s
 *
 *      Tx/Rx data rate
 *
 * IEEE80211_RADIOTAP_DBM_ANTSIGNAL     s8           decibels from
 *                                                   one milliwatt (dBm)
 *
 *      RF signal power at the antenna, decibel difference from
 *      one milliwatt.
 *
 * IEEE80211_RADIOTAP_DBM_ANTNOISE      s8           decibels from
 *                                                   one milliwatt (dBm)
 *
 *      RF noise power at the antenna, decibel difference from one
 *      milliwatt.
 *
 * IEEE80211_RADIOTAP_DB_ANTSIGNAL      u8           decibel (dB)
 *
 *      RF signal power at the antenna, decibel difference from an
 *      arbitrary, fixed reference.
 *
 * IEEE80211_RADIOTAP_DB_ANTNOISE       u8           decibel (dB)
 *
 *      RF noise power at the antenna, decibel difference from an
 *      arbitrary, fixed reference point.
 *
 * IEEE80211_RADIOTAP_LOCK_QUALITY      __le16       unitless
 *
 *      Quality of Barker code lock. Unitless. Monotonically
 *      nondecreasing with "better" lock strength. Called "Signal
 *      Quality" in datasheets.  (Is there a standard way to measure
 *      this?)
 *
 * IEEE80211_RADIOTAP_TX_ATTENUATION    __le16       unitless
 *
 *      Transmit power expressed as unitless distance from max
 *      power set at factory calibration.  0 is max power.
 *      Monotonically nondecreasing with lower power levels.
 *
 * IEEE80211_RADIOTAP_DB_TX_ATTENUATION __le16       decibels (dB)
 *
 *      Transmit power expressed as decibel distance from max power
 *      set at factory calibration.  0 is max power.  Monotonically
 *      nondecreasing with lower power levels.
 *
 * IEEE80211_RADIOTAP_DBM_TX_POWER      s8           decibels from
 *                                                   one milliwatt (dBm)
 *
 *      Transmit power expressed as dBm (decibels from a 1 milliwatt
 *      reference). This is the absolute power level measured at
 *      the antenna port.
 *
 * IEEE80211_RADIOTAP_FLAGS             u8           bitmap
 *
 *      Properties of transmitted and received frames. See flags
 *      defined below.
 *
 * IEEE80211_RADIOTAP_ANTENNA           u8           antenna index
 *
 *      Unitless indication of the Rx/Tx antenna for this packet.
 *      The first antenna is antenna 0.
 *
 * IEEE80211_RADIOTAP_RX_FLAGS          __le16       bitmap
 *
 *     Properties of received frames. See flags defined below.
 *
 * IEEE80211_RADIOTAP_TX_FLAGS          __le16       bitmap
 *
 *     Properties of transmitted frames. See flags defined below.
 *
 * IEEE80211_RADIOTAP_RTS_RETRIES       u8           data
 *
 *     Number of rts retries a transmitted frame used.
 *
 * IEEE80211_RADIOTAP_DATA_RETRIES      u8           data
 *
 *     Number of unicast retries a transmitted frame used.
 *
 */
enum ieee80211_radiotap_type {
	IEEE80211_RADIOTAP_TSFT = 0,
	IEEE80211_RADIOTAP_FLAGS = 1,
	IEEE80211_RADIOTAP_RATE = 2,
	IEEE80211_RADIOTAP_CHANNEL = 3,
	IEEE80211_RADIOTAP_FHSS = 4,
	IEEE80211_RADIOTAP_DBM_ANTSIGNAL = 5,
	IEEE80211_RADIOTAP_DBM_ANTNOISE = 6,
	IEEE80211_RADIOTAP_LOCK_QUALITY = 7,
	IEEE80211_RADIOTAP_TX_ATTENUATION = 8,
	IEEE80211_RADIOTAP_DB_TX_ATTENUATION = 9,
	IEEE80211_RADIOTAP_DBM_TX_POWER = 10,
	IEEE80211_RADIOTAP_ANTENNA = 11,
	IEEE80211_RADIOTAP_DB_ANTSIGNAL = 12,
	IEEE80211_RADIOTAP_DB_ANTNOISE = 13,
	IEEE80211_RADIOTAP_RX_FLAGS = 14,
	IEEE80211_RADIOTAP_TX_FLAGS = 15,
	IEEE80211_RADIOTAP_RTS_RETRIES = 16,
	IEEE80211_RADIOTAP_DATA_RETRIES = 17,
	IEEE80211_RADIOTAP_EXT = 31
};

/* Channel flags. */
#define	IEEE80211_CHAN_TURBO	0x0010	/* Turbo channel */
#define	IEEE80211_CHAN_CCK	0x0020	/* CCK channel */
#define	IEEE80211_CHAN_OFDM	0x0040	/* OFDM channel */
#define	IEEE80211_CHAN_2GHZ	0x0080	/* 2 GHz spectrum channel. */
#define	IEEE80211_CHAN_5GHZ	0x0100	/* 5 GHz spectrum channel */
#define	IEEE80211_CHAN_PASSIVE	0x0200	/* Only passive scan allowed */
#define	IEEE80211_CHAN_DYN	0x0400	/* Dynamic CCK-OFDM channel */
#define	IEEE80211_CHAN_GFSK	0x0800	/* GFSK channel (FHSS PHY) */

/* For IEEE80211_RADIOTAP_FLAGS */
#define	IEEE80211_RADIOTAP_F_CFP	0x01	/* sent/received
						 * during CFP
						 */
#define	IEEE80211_RADIOTAP_F_SHORTPRE	0x02	/* sent/received
						 * with short
						 * preamble
						 */
#define	IEEE80211_RADIOTAP_F_WEP	0x04	/* sent/received
						 * with WEP encryption
						 */
#define	IEEE80211_RADIOTAP_F_FRAG	0x08	/* sent/received
						 * with fragmentation
						 */
#define	IEEE80211_RADIOTAP_F_FCS	0x10	/* frame includes FCS */
#define	IEEE80211_RADIOTAP_F_DATAPAD	0x20	/* frame has padding between
						 * 802.11 header and payload
						 * (to 32-bit boundary)
						 */
/* For IEEE80211_RADIOTAP_RX_FLAGS */
#define IEEE80211_RADIOTAP_F_RX_BADFCS	0x0001	/* frame failed crc check */

/* For IEEE80211_RADIOTAP_TX_FLAGS */
#define IEEE80211_RADIOTAP_F_TX_FAIL	0x0001	/* failed due to excessive
						 * retries */
#define IEEE80211_RADIOTAP_F_TX_CTS	0x0002	/* used cts 'protection' */
#define IEEE80211_RADIOTAP_F_TX_RTS	0x0004	/* used rts/cts handshake */
#define IEEE80211_RADIOTAP_F_TX_NOACK	0x0008	/* frame should not be ACKed */
#define IEEE80211_RADIOTAP_F_TX_NOSEQ	0x0010	/* sequence number handled
						 * by userspace */

/* Ugly macro to convert literal channel numbers into their mhz equivalents
 * There are certianly some conditions that will break this (like feeding it '30')
 * but they shouldn't arise since nothing talks on channel 30. */
#define ieee80211chan2mhz(x) \
	(((x) <= 14) ? \
	(((x) == 14) ? 2484 : ((x) * 5) + 2407) : \
	((x) + 1000) * 5)
	
//// Estructuras donde se almacena la información

/* linked list of detected access points */
#define MAX_IE_ELEMENT_SIZE 256
#define NB_PWR  5       /* size of signal power ring buffer */
#define NB_PRB 10       /* size of probed ESSID ring buffer */

const unsigned char llcnull[4] = {0, 0, 0, 0};

#define NULL_MAC  (unsigned char*)"\x00\x00\x00\x00\x00\x00"
#define BROADCAST (unsigned char*)"\xFF\xFF\xFF\xFF\xFF\xFF"
#define	STD_OPN		0x0001
#define	STD_WEP		0x0002
#define	STD_WPA		0x0004
#define	STD_WPA2	0x0008

#define STD_FIELD	(STD_OPN | STD_WEP | STD_WPA | STD_WPA2)

#define	ENC_WEP		0x0010
#define	ENC_TKIP	0x0020
#define	ENC_WRAP	0x0040
#define	ENC_CCMP	0x0080
#define ENC_WEP40	0x1000
#define	ENC_WEP104	0x0100

#define ENC_FIELD	(ENC_WEP | ENC_TKIP | ENC_WRAP | ENC_CCMP | ENC_WEP40 | ENC_WEP104)

#define	AUTH_OPN	0x0200
#define	AUTH_PSK	0x0400
#define	AUTH_MGT	0x0800

#define AUTH_FIELD	(AUTH_OPN | AUTH_PSK | AUTH_MGT)

#define STD_QOS         0x2000

//BSSID const. length of 6 bytes; can be together with all the other types
#define IVS2_BSSID	0x0001

//ESSID var. length; alone, or with BSSID
#define IVS2_ESSID	0x0002

//wpa structure, const. length; alone, or with BSSID
#define IVS2_WPA	0x0004

//IV+IDX+KEYSTREAM, var. length; alone or with BSSID
#define IVS2_XOR	0x0008

/* [IV+IDX][i][l][XOR_1]..[XOR_i][weight]                                                        *
 * holds i possible keystreams for the same IV with a length of l for each keystream (l max 32)  *
 * and an array "int weight[16]" at the end                                                      */
#define IVS2_PTW        0x0010

//unencrypted packet
#define IVS2_CLR        0x0020

struct pcap_pkthdr
{
    int tv_sec;
    int tv_usec;
    uint caplen;
    uint len;
};

struct pkt_buf
{
    struct pkt_buf  *next;      /* next packet in list */
    unsigned char   *packet;    /* packet */
    unsigned short  length;     /* packet length */
    struct timeval  ctime;      /* capture time */
};

struct AP_info
{
    struct AP_info *prev;     /* prev. AP in list         */
    struct AP_info *next;     /* next  AP in list         */

    time_t tinit, tlast;      /* first and last time seen */

    int channel;              /* AP radio channel         */
    int max_speed;            /* AP maximum speed in Mb/s */
    int avg_power;            /* averaged signal power    */
    int best_power;           /* best signal power    */
    int power_index;          /* index in power ring buf. */
    int power_lvl[NB_PWR];    /* signal power ring buffer */
//    int preamble;             /* 0 = long, 1 = short      */
//    int security;             /* ENC_*, AUTH_*, STD_*     */
//    int beacon_logged;        /* We need 1 beacon per AP  */
    int ssid_length;          /* length of ssid           */

    unsigned long nb_bcn;     /* total number of beacons  */
    unsigned long nb_pkt;     /* total number of packets  */
    unsigned long nb_data;    /* number of  data packets  */
//    unsigned long nb_data_old;/* number of data packets/sec*/
    int nb_dataps;  /* number of data packets/sec*/
    struct timeval tv;        /* time for data per second */

    unsigned char bssid[6];   /* the access point's MAC   */
//    unsigned char essid[MAX_IE_ELEMENT_SIZE];
                              /* ascii network identifier */
//    unsigned long long timestamp;
    						  /* Timestamp to calculate uptime   */

//    unsigned char lanip[4];   /* last detected ip address */
                              /* if non-encrypted network */

//    unsigned char **uiv_root; /* unique iv root structure */
                              /* if wep-encrypted network */

//    int    rx_quality;        /* percent of captured beacons */
    int    fcapt;             /* amount of captured frames   */
    int    fmiss;             /* amount of missed frames     */
    unsigned int    last_seq; /* last sequence number        */
    //struct timeval ftimef;    /* time of first frame         */
    struct timeval ftimel;    /* time of last frame          */
    struct timeval ftimer;    /* time of restart             */
};

// struct WPA_hdsk
// {
//     uchar stmac[6];				 /* supplicant MAC               */
//     uchar snonce[32];			 /* supplicant nonce             */
//     uchar anonce[32];			 /* authenticator nonce          */
//     uchar keymic[16];			 /* eapol frame MIC              */
//     uchar eapol[256];			 /* eapol frame contents         */
//     int eapol_size;				 /* eapol frame size             */
//     int keyver;					 /* key version (TKIP / AES)     */
//     int state;					 /* handshake completion         */
// };

/* linked list of detected clients */

struct ST_info
{
    struct ST_info *prev;    /* the prev client in list   */
    struct ST_info *next;    /* the next client in list   */
    struct AP_info *base;    /* AP this client belongs to */
    time_t tinit, tlast;     /* first and last time seen  */
    unsigned long nb_pkt;    /* total number of packets   */
    unsigned char stmac[6];  /* the client's MAC address  */
    //char *manuf;             /* the client's manufacturer */
    int probe_index;         /* probed ESSIDs ring index  */
    char probes[NB_PRB][MAX_IE_ELEMENT_SIZE];
                             /* probed ESSIDs ring buffer */
    int ssid_length[NB_PRB]; /* ssid lengths ring buffer  */
    int power;               /* last signal power         */
    int rate_to;             /* last bitrate to station   */
    int rate_from;           /* last bitrate from station */
    struct timeval ftimer;   /* time of restart           */
    int missed;              /* number of missed packets  */
    unsigned int lastseq;    /* last seen sequence number */
    //struct WPA_hdsk wpa;     /* WPA handshake data        */
    //int qos_to_ds;           /* does it use 802.11e to ds */
    //int qos_fr_ds;           /* does it receive 802.11e   */
};

/* linked list of detected macs through ack, cts or rts frames */

struct NA_info
{
    struct NA_info *prev;    /* the prev client in list   */
    struct NA_info *next;    /* the next client in list   */
    time_t tinit, tlast;     /* first and last time seen  */
    unsigned char namac[6];  /* the stations MAC address  */
    int power;               /* last signal power         */
    int channel;             /* captured on channel       */
    int ack;                 /* number of ACK frames      */
    int ack_old;             /* old number of ACK frames  */
    int ackps;               /* number of ACK frames/s    */
    int cts;                 /* number of CTS frames      */
    int rts_r;               /* number of RTS frames (rx) */
    int rts_t;               /* number of RTS frames (tx) */
    int other;               /* number of other frames    */
    struct timeval tv;       /* time for ack per second   */
};

struct DEBUG_info
{
  struct DEBUG_info *prev;
  struct DEBUG_info *next;
  
  int ToFromDS;
  
  unsigned char addr1[6];
  unsigned char addr2[6];
  unsigned char addr3[6];
  unsigned char addr4[6];
};

struct PWR_in_second
{
    struct PWR_in_second *prev;
    struct PWR_in_second *next;
  
    int power;
    int tipo;
    time_t second;
    struct DEBUG_info *debug;
};

struct CLI_info
{
    struct CLI_info *prev;
    struct CLI_info *next;
    
    int is_ap;
    int is_connected; //Is the STA connected?. 0 for all APs
    
    unsigned char mac[6];
    struct PWR_in_second *pwr;
};

struct tesla
{
    struct AP_info *ap_1st, *ap_end;
    struct ST_info *st_1st, *st_end;
    struct NA_info *na_1st, *na_end;
    
    struct CLI_info *clients;
    
    unsigned char prev_bssid[6];
    unsigned char f_bssid[6];
    unsigned char f_netmask[6];    
}
T;

struct teslaconf
{
  const char *shop_id;
  char *sensor_secret;
  char *tesla_server;
  int seed_second;
}
TCONF;
#endif
