/*
 * THE NEMESIS PROJECT
 * Copyright (C) 1999, 2000, 2001 Mark Grimes <mark@stateful.net>
 * Copyright (C) 2001 - 2003 Jeff Nathan <jeff@snort.org>
 * Copyright (C) 2019 Joachim Nilsson <troglobit@gmail.com>
 *
 * nemesis-dhcp.c (DHCP Packet Injector)
 */

#include "nemesis-dhcp.h"
#include "nemesis.h"
#if defined(WIN32)
#include <pcap.h>
#endif

static ETHERhdr etherhdr;
static IPhdr    iphdr;
static UDPhdr   udphdr;
static DHCPhdr  dhcphdr;
static struct file pd, ipod;
static char    *payloadfile    = NULL; /* payload file name */
static char    *ipoptionsfile  = NULL; /* IP options file name */
static char    *device         = NULL; /* Ethernet device */

#if defined(WIN32)
static char *ifacetmp = NULL;
#endif

static void dhcp_cmdline(int, char **);
static int  dhcp_exit(int);
static void dhcp_initdata(void);
static void dhcp_usage(char *);
static void dhcp_validatedata(void);
static void dhcp_verbose(void);

void nemesis_dhcp(int argc, char **argv)
{
	const char *module = "DHCP Packet Injection";
	libnet_t *l;

	nemesis_maketitle(title, module, version);

	if (argc > 1 && !strncmp(argv[1], "help", 4))
		dhcp_usage(argv[0]);

	if (nemesis_seedrand() < 0)
		fprintf(stderr, "ERROR: Unable to seed random number generator.\n");

	dhcp_initdata();
	dhcp_cmdline(argc, argv);

	l = libnet_init(got_link ? LIBNET_LINK_ADV : LIBNET_RAW4, device, errbuf);
	if (!l)
		dhcp_exit(1);

	if (got_link) {
		if ((nemesis_check_link(&etherhdr, l)) < 0) {
			fprintf(stderr, "ERROR: cannot retrieve hardware address of %s.\n", device);
			dhcp_exit(1);
		}
	}

	dhcp_validatedata();
	dhcp_verbose();

	if (got_payload) {
#if defined(WIN32)
		if (builddatafromfile(DHCP_LINKBUFFSIZE, &pd, payloadfile, PAYLOADMODE) < 0)
#else
		if (builddatafromfile(((got_link == 1) ? DHCP_LINKBUFFSIZE : DHCP_RAWBUFFSIZE),
		                      &pd, payloadfile, PAYLOADMODE) < 0)
#endif
			dhcp_exit(1);
	}

	if (got_ipoptions) {
		if (builddatafromfile(OPTIONSBUFFSIZE, &ipod, ipoptionsfile, OPTIONSMODE) < 0)
			dhcp_exit(1);
	}

	if (builddhcp(&etherhdr, &iphdr, &udphdr, &dhcphdr, &pd, &ipod, l) < 0) {
		puts("\nDHCP Injection Failure");
		dhcp_exit(1);
	}

	dhcp_exit(0);
}

static void dhcp_initdata(void)
{
	static uint8_t opts[] = {
		53, 1, 1,	                                 /* Option 53: DHCP Disco   */
		12, 7, 0x6e, 0x65, 0x6d, 0x65, 0x73, 0x69, 0x73, /* Option 12: Host name    */
		60, 7, 0x6e, 0x65, 0x6d, 0x65, 0x73, 0x69, 0x73, /* Option 60: Vendor ID    */
		61, 7, 0x01, 0xde, 0xc0, 0xde, 0xc0, 0xff, 0xee, /* Option 61: Client ID    */
		55, 5, 1, 3, 6, 15, 33,				 /* Option 55: Param req.   */
		255,						 /* EOF */
	};
	uint8_t chaddr[6] = { 0xde, 0xc0, 0xde, 0xc0, 0xff, 0xee};

	etherhdr.ether_type = ETHERTYPE_IP;    /* Ethernet type IP */
	memset(etherhdr.ether_shost, 0, 6);    /* Ethernet source address */
	memset(etherhdr.ether_dhost, 0xff, 6); /* Ethernet destination address */

	iphdr.ip_src.s_addr = 0;
	iphdr.ip_dst.s_addr = 0xffffffff;
	iphdr.ip_tos        = IPTOS_LOWDELAY;          /* IP type of service */
	iphdr.ip_id         = 0;
	iphdr.ip_p          = IPPROTO_UDP;
	iphdr.ip_off        = 0;                       /* IP fragmentation offset */
	iphdr.ip_ttl        = 128;                     /* IP TTL */

	udphdr.uh_sport     = 68;                      /* UDP source port */
	udphdr.uh_dport     = 67;                      /* UDP destination port */

	dhcphdr.dhcp_opcode = LIBNET_DHCP_REQUEST;     /* Request or reply */
	dhcphdr.dhcp_htype  = 1;		       /* HW type: Ethernet */
	dhcphdr.dhcp_hlen   = 6;		       /* Length of MAC address */
	dhcphdr.dhcp_hopcount = 0;		       /* Used by proxy/relay agent */
	dhcphdr.dhcp_xid    = libnet_get_prand(PRu32); /* Transaction ID */
	dhcphdr.dhcp_secs   = 0;		       /* Seconds since bootstrap */
	dhcphdr.dhcp_flags  = 0x8000;		       /* DHCP flags, unused for BOOTP */
	dhcphdr.dhcp_cip    = 0;		       /* Client's (current) IP */
	dhcphdr.dhcp_yip    = 0;		       /* Your IP (from server) */
	dhcphdr.dhcp_sip    = 0;		       /* Server's IP */
	dhcphdr.dhcp_gip    = 0;		       /* Gateway IP (relay?) */
	memcpy(dhcphdr.dhcp_chaddr, chaddr, sizeof(dhcphdr.dhcp_chaddr));

	pd.file_buf    = opts;
	pd.file_len    = NELEMS(opts);
	ipod.file_buf  = NULL;
	ipod.file_len  = 0;
}

static void dhcp_validatedata(void)
{
}

static void dhcp_usage(char *arg)
{
	nemesis_printtitle(title);

	printf("DHCP usage:\n"
	       "  %s [-v (verbose)] [options]\n"
	       "\n", arg);
	printf("DHCP options:\n"
	       "  -i <ID>      DHCP ID\n"
	       "  -g <FLAGS>   DHCP flags\n"
	       "  -q <NUM>     Number of Questions\n"
	       "  -b <NUM>     Number of Answer     RRs (resource records)\n"
	       "  -A <NUM>     Number of Authority  RRs\n"
	       "  -r <NUM>     Number of Additional RRs\n"
	       "  -P <FILE>    Raw DHCP payload file\n"
	       "\n");
	printf("UDP options (without -k):\n"
	       "  -x <PORT>    Source port\n"
	       "  -y <PORT>    Destination port\n"
	       "\n");
	printf("IP options:\n"
	       "  -S <ADDR>    Source IP address\n"
	       "  -D <ADDR>    Destination IP address\n"
	       "  -I <ID>      IP ID\n"
	       "  -T <TTL>     IP TTL\n"
	       "  -t <TOS>     IP TOS\n"
	       "  -F <OPT>     IP fragmentation options: -F[D],[M],[R],[offset]\n"
	       "  -O <FILE>    Raw IP options file\n"
	       "\n");
	printf("Data Link Options:\n"
#if defined(WIN32)
	       "  -d <IFNUM>   Network interface number\n"
#else
	       "  -d <IFNAME>  Network interface name\n"
#endif
	       "  -H <MAC>     Source MAC address\n"
	       "  -M <MAC>     Destination MAC address\n");
#if defined(WIN32)
	printf("  -Z           List available network interfaces by number\n");
#endif
	putchar('\n');
	dhcp_exit(1);
}

static void dhcp_cmdline(int argc, char **argv)
{
	int          opt, i;
	uint32_t     addr_tmp[6];
	char        *dhcp_options;
	extern char *optarg;
	extern int   optind;

#if defined(WIN32)
	dhcp_options = "d:D:F:H:I:M:O:P:t:T:x:y:vZ?";
#else
	dhcp_options = "d:D:F:H:I:M:O:P:t:T:x:y:v?";
#endif
	while ((opt = getopt(argc, argv, dhcp_options)) != -1) {
		switch (opt) {
		case 'd': /* Ethernet device */
#if defined(WIN32)
			if (nemesis_getdev(atoi(optarg), &device) < 0) {
				fprintf(stderr, "ERROR: Unable to lookup device: '%d'.\n", atoi(optarg));
				dhcp_exit(1);
			}
#else
			if (strlen(optarg) < 256) {
				if (device)
					free(device);
				device = strdup(optarg);
				got_link = 1;
			} else {
				fprintf(stderr, "ERROR: device %s > 256 characters.\n", optarg);
				dhcp_exit(1);
			}
#endif
			break;

		case 'D': /* destination IP address */
			if ((nemesis_name_resolve(optarg, &iphdr.ip_dst.s_addr)) < 0) {
				fprintf(stderr, "ERROR: Invalid destination IP address: \"%s\".\n", optarg);
				dhcp_exit(1);
			}
			break;

		case 'F': /* IP fragmentation options */
			if (parsefragoptions(&iphdr, optarg) < 0)
				dhcp_exit(1);
			break;

		case 'H': /* Ethernet source address */
			memset(addr_tmp, 0, sizeof(addr_tmp));
			sscanf(optarg, "%02X:%02X:%02X:%02X:%02X:%02X", &addr_tmp[0],
			       &addr_tmp[1], &addr_tmp[2], &addr_tmp[3], &addr_tmp[4], &addr_tmp[5]);
			for (i = 0; i < 6; i++)
				etherhdr.ether_shost[i] = addr_tmp[i];
			break;

		case 'I': /* IP ID */
			iphdr.ip_id = xgetint16(optarg);
			break;

		case 'M': /* Ethernet destination address */
			memset(addr_tmp, 0, sizeof(addr_tmp));
			sscanf(optarg, "%02X:%02X:%02X:%02X:%02X:%02X", &addr_tmp[0],
			       &addr_tmp[1], &addr_tmp[2], &addr_tmp[3], &addr_tmp[4], &addr_tmp[5]);
			for (i = 0; i < 6; i++)
				etherhdr.ether_dhost[i] = addr_tmp[i];
			break;

		case 'O': /* IP options file */
			if (strlen(optarg) < 256) {
				if (ipoptionsfile)
					free(ipoptionsfile);
				ipoptionsfile = strdup(optarg);
				got_ipoptions = 1;
			} else {
				fprintf(stderr, "ERROR: IP options file %s > 256 characters.\n", optarg);
				dhcp_exit(1);
			}
			break;

		case 'P': /* payload file */
			if (strlen(optarg) < 256) {
				if (payloadfile)
					free(payloadfile);
				payloadfile = strdup(optarg);
				got_payload = 1;
			} else {
				fprintf(stderr, "ERROR: payload file %s > 256 characters.\n", optarg);
				dhcp_exit(1);
			}
			break;

		case 'S': /* source IP address */
			if ((nemesis_name_resolve(optarg, &iphdr.ip_src.s_addr)) < 0) {
				fprintf(stderr, "ERROR: Invalid source IP address: \"%s\".\n", optarg);
				dhcp_exit(1);
			}
			break;

		case 't': /* IP type of service */
			iphdr.ip_tos = xgetint8(optarg);
			break;

		case 'T': /* IP time to live */
			iphdr.ip_ttl = xgetint8(optarg);
			break;

		case 'v':
			verbose++;
			if (verbose == 1)
				nemesis_printtitle(title);
			break;

		case 'x': /* UDP source port (68) */
			udphdr.uh_sport = xgetint16(optarg);
			break;

		case 'y': /* UDP destination port (67) */
			udphdr.uh_dport = xgetint16(optarg);
			break;

#if defined(WIN32)
		case 'Z':
			if ((ifacetmp = pcap_lookupdev(errbuf)) == NULL)
				perror(errbuf);

			PrintDeviceList(ifacetmp);
			dns_exit(1); /* FALLTHROUGH */
#endif
		case '?': /* FALLTHROUGH */
		default:
			dhcp_usage(argv[0]);
			break;
		}
	}
	argc -= optind;
	argv += optind;
}

static int dhcp_exit(int code)
{
	if (got_payload)
		free(pd.file_buf);

	if (got_ipoptions)
		free(ipod.file_buf);

	if (device != NULL)
		free(device);

	if (ipoptionsfile != NULL)
		free(ipoptionsfile);

	if (payloadfile != NULL)
		;
	free(payloadfile);

#if defined(WIN32)
	if (ifacetmp != NULL)
		free(ifacetmp);
#endif

	exit(code);
}

static void dhcp_verbose(void)
{
	if (verbose) {
		if (got_link)
			nemesis_printeth(&etherhdr);

		nemesis_printip(&iphdr);
		nemesis_printudp(&udphdr);
	}
}
