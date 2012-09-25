#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <signal.h>
#include <sys/time.h>
#include <zlib.h>

#include "etherp.h"

static struct option etherp_recv_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "interface", required_argument, NULL, 'I' },
	{ "verbose", no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

static int etherp_verbose = 0;  /**< This program will verbose
                                   if (verbose != 0) */
static int etherp_quit = 0;     /**< if set, exit the reception loop */
static unsigned long long etherp_nb_bytes = 0;

static void etherp_recv_usage(void)
{
	fprintf(stderr, "TODO: usage\n");
}

static void etherp_signal_display_bitrate(int sig)
{
	printf("Receiving at %lld mbit/s (%.2f MB/s)        \r",
	       etherp_nb_bytes * 8 / 1000000, etherp_nb_bytes / 1000000.);
	etherp_nb_bytes = 0;
	fflush(stdout);
}

static void etherp_signal_quit(int sig)
{
	etherp_quit = 1;
}

static int etherp_recv_frames(const char *ifname)
{
	int s;
	void *buf;
	int len;
	unsigned char *etherhead;
	unsigned char *data;
	struct ethhdr *eh;
	struct etherp_hdr *etherph;
	uint32_t id;
	uint32_t last_id = 0;
	unsigned long long nb_bytes = 0;
	unsigned long long nb_frames = 0;
	unsigned long long nb_errors = 0;

	s = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (s == -1) {
		perror("Cannot open the raw socket");
		return errno;
	}

	buf = malloc(ETH_FRAME_LEN + ETH_FCS_LEN);
	etherhead = buf;
	data = buf + ETH_HLEN;
	eh = (struct ethhdr*) etherhead;
	etherph = (struct etherp_hdr *) data;

	while (!etherp_quit) {
		if ((len = recvfrom(s, buf, ETH_FRAME_LEN, 0, NULL, NULL)) == -1) {
			if (etherp_quit)
				break;
			perror("Can't receive Ethernet frame");
			return errno;
		}

		if (len < ETH_HLEN) {
			/* Invalid Ethernet frame */
			continue;
		}

		if (ntohs(eh->h_proto) == ETHERTYPE_ETHERP) {
			if (len < ETH_ALEN) {
				printf("Warning: frame whose len = %d received\n", len);
				nb_errors += (id - last_id);
				continue;
			}

			etherp_nb_bytes += len;
			nb_bytes += len;
			nb_frames += 1;
			id = ntohl(etherph->id);
			ETHERP_VPRINT("Received frame from %02X:%02X:%02X:%02X:%02X:%02X ID=%d\n",
			              eh->h_source[0], eh->h_source[1], eh->h_source[2],
			              eh->h_source[3], eh->h_source[4], eh->h_source[5],
			              id);

			if ((id != (last_id + 1)) && (last_id != 0)) {
				printf("Warning: %d missing frame(s) (ID=%u but last received ID was %u)\n",
				       id - (last_id + 1), id, last_id);
				nb_errors += (id - last_id);
			}
			if (etherph->crc32 != crc32(crc32(0L, Z_NULL, 0), data + sizeof (struct etherp_hdr),
			                            len - sizeof (struct etherp_hdr) - ETH_HLEN)) {
				printf("Warning: Received frame with wrong CRC\n");
				++nb_errors;
			}
			last_id = id;

			if (etherph->stop)
				break;
		}
	}

	close(s);

	printf("\n\n");
	printf("--- etherp-recv statictics ---\n");
	printf("%llu frames received, %.02f MB received, %llu errors detected\n",
	       nb_frames, nb_bytes / 1000000., nb_errors);
	return 0;
}

int main(int argc, char *argv[])
{
	int c;
	const char *ifname = "eth0";
	struct sigaction sa_sigint;
	struct sigaction sa_sigalrm;
	struct itimerval itv;

	while (1) {
		c = getopt_long(argc, argv, "hI:v", etherp_recv_options, NULL);

		if (c == -1)
			break;

		switch (c) {
			case 'h':
				etherp_recv_usage();
				return 0;
				break;
			case 'I':
				ifname = optarg;
				break;
			case 'v':
				etherp_verbose = 1;
				break;
			default:
				etherp_recv_usage();
				return 1;
				break;
		}
	}

	if ((argc - optind) != 0) {
		etherp_recv_usage();
		return 1;
	}

	sa_sigint.sa_handler = etherp_signal_quit;
	sigemptyset(&sa_sigint.sa_mask);
	sigaction(SIGINT, &sa_sigint, NULL);
	if (!etherp_verbose) {
		/* Install a timer to display the bitrate every 1 second */
		sa_sigalrm.sa_handler = etherp_signal_display_bitrate;
		sigemptyset(&sa_sigalrm.sa_mask);
		sa_sigalrm.sa_flags = SA_RESTART;
		sigaction(SIGALRM, &sa_sigalrm, NULL);
		itv.it_value.tv_sec = 1;
		itv.it_value.tv_usec = 0;
		itv.it_interval.tv_sec = 1;
		itv.it_interval.tv_usec = 0;
		setitimer(ITIMER_REAL, &itv, NULL);
	}

	return etherp_recv_frames(ifname) ? 2 : 0;
}
