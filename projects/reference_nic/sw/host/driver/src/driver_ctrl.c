/* FIXME: Add proper comment headers. */
/* NOTE: This program utilizes the libnl-3.0 library for generic netlink functions.
 * You must install this library before compiling (http://www.infradead.org/~tgr/libnl/). */

/* Header files provided by libnl (probably in /usr/local/include). */
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>

#include "nf10_eth_driver.h"

static void set_program_name(const char *);
static void parse_options(int, char*[]);
static void usage(void);
static int driver_connect(void);
static void driver_disconnect(void);

struct command {
	const char *name;
	int min_args;
	int max_args;
	void (*handler)(int argc, char *argv[]);
};

const char *program_name;

struct nl_sock *nf10_genl_sock;
int nf10_genl_family;

static struct command all_commands[];

int main(int argc, char *argv[])
{
	struct command *p;

	set_program_name(argv[0]);

	/* Move past program name. */
	argc -= 1;
	argv += 1;

	if(argc < 1) {
		printf("ERROR: main(): too few arguments\n\n");
		usage();
		return 0;
	}	

	for(p = all_commands; p->name != NULL; p++) {
		if(!strcmp(p->name, argv[0])) {
			int n_arg = argc - 1;
			if(n_arg < p->min_args) {
				printf("ERROR: main(): '%s' command requires at least %d arguments\n",
					p->name, p->min_args);
				return 0;
			}
			else if(n_arg > p->max_args) {
				printf("ERROR: main(): '%s' command takes at most %d arguments\n",
					p->name, p->max_args);
				return 0;
			}
			else {
				p->handler(argc, argv);
				return 0;
			}
		}
	}	

	printf("ERROR: main(): unknown command '%s'\n\n", argv[0]);
	usage();

	return 0;
}

static void set_program_name(const char *argv0)
{
	const char *slash = strrchr(argv0, '/');
	program_name = slash ? slash + 1 : argv0;
}

/* FIXME: delete this code... 
static void parse_options(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{0, 0, 0, 0},
	};
	char *short_options = long_options_to_short_options(long_options);

	for(;;) {
		int c;

		c = getopt_long(argc, argv, short_options, long_options, NULL);
		if(c == EOF) {
			break;
		}
	
		switch(c) {
		case 'h':
			usage();
			exit(EXIT_SUCCESS);

		case 'V':
			printf("%s compiled "__DATE__" "__TIME__"\n",
				program_name);
			exit(EXIT_SUCCESS);

		case '?':
			printf("ERROR: parse_options(): unknown option\n");
			exit(EXIT_FAILURE);

		default:
			abort();
		}
	}
	free(short_options);
}
*/

static void usage(void)
{
	printf("%s: NetFPGA-10G Ethernet driver management, control, and testing utility\n"
		"usage: %s [OPTIONS] COMMAND [ARG...]\n\n"
		"  echo STRING			Send STRING to driver, driver echoes it back.\n"
		"  dma_tx STRING		Transmit STRING to the hardware via DMA.\n",
		program_name, program_name);
}

static int driver_connect()
{
	int err;

	nf10_genl_sock = nl_socket_alloc();
	if(nf10_genl_sock == NULL) {
		printf("ERROR: driver_connect(): could not allocate netlink socket\n");
		return -1;
	}

	err = genl_connect(nf10_genl_sock);
	if(err != 0) {
		printf("ERROR: driver_connect(): genl_connect failed\n");
		nl_socket_free(nf10_genl_sock);
		return err;
	}

	nf10_genl_family = genl_ctrl_resolve(nf10_genl_sock, NF10_GENL_FAMILY_NAME);
	if(nf10_genl_family < 0) {
		printf("ERROR: driver_connect(): could not resolve family channel number\n");
		nl_socket_free(nf10_genl_sock);
		return -1;
	}

	return 0;
}

static void driver_disconnect()
{
	nl_socket_free(nf10_genl_sock);
}

static int do_echo_recv_msg_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh;
	struct nlattr *attrs[NF10_GENL_A_MAX + 1];
	char *reply;	

	nlh = nlmsg_hdr(msg);

	genlmsg_parse(nlh, 0, attrs, NF10_GENL_A_MAX, 0);

	reply = nla_data(attrs[NF10_GENL_A_MSG]);
	if(reply == NULL) {
		printf("ERROR: do_echo_recv_msg_cb(): reply is NULL\n");
		return 0;
	}

	printf("Received reply:\n%s\n", reply);

	return 0;
}

static void do_echo(int argc, char *argv[])
{
	struct nl_msg	*msg;
	int err;	

	err = driver_connect();
	if(err != 0) {
		printf("ERROR: do_echo(): failed to connect to the driver\n");
		return;
	}

	msg = nlmsg_alloc();
	if(msg == NULL) {
		printf("ERROR: do_echo(): could not allocate new netlink message\n");
		driver_disconnect();
		return;
	}

	/* genlmsg_put will fill in the fields of the nlmsghdr and the genlmsghdr. */
	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nf10_genl_family, 0, 0,
			NF10_GENL_C_ECHO, NF10_GENL_FAMILY_VERSION);

	nla_put_string(msg, NF10_GENL_A_MSG, argv[1]);

	/* nl_send_auto will automatically fill in the PID and the sequence number,
	 * and also add an NLM_F_REQUEST flag. It will also add an NLM_F_ACK
	 * flag unless the netlink socket has the NL_NO_AUTO_ACK flag set. */
	nl_send_auto(nf10_genl_sock, msg);

	nlmsg_free(msg);

	nl_socket_modify_cb(nf10_genl_sock, NL_CB_VALID, NL_CB_CUSTOM, do_echo_recv_msg_cb, NULL);

	nl_recvmsgs_default(nf10_genl_sock);

	driver_disconnect();
}

static int do_dma_tx_recv_ack_cb(struct nl_msg *msg, void *arg)
{
	/* FIXME: msg doesn't really make sense to user... */
	//printf("Received ACK\n");

	return 0;
}


static void do_dma_tx(int argc, char *argv[])
{
	struct nl_msg	*msg;
	int err;	

	err = driver_connect();
	if(err != 0) {
		printf("ERROR: do_dma_tx(): failed to connect to the driver\n");
		return;
	}

	msg = nlmsg_alloc();
	if(msg == NULL) {
		printf("ERROR: do_dma_tx(): could not allocate new netlink message\n");
		driver_disconnect();
		return;
	}

	/* genlmsg_put will fill in the fields of the nlmsghdr and the genlmsghdr. */
	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nf10_genl_family, 0, 0,
			NF10_GENL_C_DMA_TX, NF10_GENL_FAMILY_VERSION);

	nla_put_string(msg, NF10_GENL_A_MSG, argv[1]);

	/* nl_send_auto will automatically fill in the PID and the sequence number,
	 * and also add an NLM_F_REQUEST flag. It will also add an NLM_F_ACK
	 * flag unless the netlink socket has the NL_NO_AUTO_ACK flag set. */
	nl_send_auto(nf10_genl_sock, msg);

	nlmsg_free(msg);

	nl_socket_modify_cb(nf10_genl_sock, NL_CB_ACK, NL_CB_CUSTOM, do_dma_tx_recv_ack_cb, NULL);

	/* FIXME: this function will return even if there's no ACK in the buffer. I.E. it doesn't
	 * seem to wait for the ACK to be received... Ideally we'd have the behavior that getting an 
	 * ACK tells us everything is OK, otherwise we time out on waiting for an ACK and tell this
	 * to the user. */
	nl_recvmsgs_default(nf10_genl_sock);

	driver_disconnect();
}

static int do_dma_rx_recv_msg_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh;
	struct nlattr	*na;
	struct nlattr *attrs[NF10_GENL_A_MAX + 1];

	nlh = nlmsg_hdr(msg);

	genlmsg_parse(nlh, 0, attrs, NF10_GENL_A_MAX, 0);

	na = attrs[NF10_GENL_A_DMA_BUF];
	if(na) {
		if(nla_data(na) == NULL) {
			printf("ERROR: do_dma_rx_recv_msg_cb(): NF10_GENL_A_DMA_BUF attribute has NULL data\n");
			return 0;
		}

		/* Cheating a bit... */
		//nl_msg_dump(msg, stdout);
		printf("%s\n", nla_data(na));
	}
	else {
		printf("Didn't find any data to receive\n");
	}

	return 0;
	
}

static void do_dma_rx(int argc, char* argv[])
{
	struct nl_msg	*msg;
	int err;

	err = driver_connect();
	if(err != 0) {
		printf("ERROR: do_dma_rx(): failed to connect to the driver\n");
		return;
	}

	msg = nlmsg_alloc();
	if(msg == NULL) {
		printf("ERROR: do_dma_rx(): could not allocate new netlink message\n");
		driver_disconnect();
		return;
	}

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nf10_genl_family, 0, 0,
			NF10_GENL_C_DMA_RX, NF10_GENL_FAMILY_VERSION);
	
	nl_send_auto(nf10_genl_sock, msg);

	nlmsg_free(msg);

	nl_socket_modify_cb(nf10_genl_sock, NL_CB_VALID, NL_CB_CUSTOM, do_dma_rx_recv_msg_cb, NULL);
	
	nl_recvmsgs_default(nf10_genl_sock);

	driver_disconnect();	
}

static struct command all_commands[] = {
	{ "echo", 1, 1, do_echo },
	{ "dma_tx", 1, 1, do_dma_tx},
	{ "dma_rx", 0, 0, do_dma_rx},
	{ NULL, 0, 0, NULL },
};