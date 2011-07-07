/* NetFPGA-10G http://www.netfpga.org
 * 
 * NetFPGA-10G Register Access Library
 *
 * Description: 
 *  A library of functions for reading and writing register in the
 *  hardware. These functions read and write registers by invoking
 *  register access functions in the linux driver over a generic netlink
 *  interface. Therefore, the driver must be loaded in the kernel for
 *  these functions to work. Please refer to the wiki for detailed
 *  documentation.
 *
 * Notes:
 *  This library makes use of the libnl-3.0 library for generic netlink
 *  functions. You must install this library into your system before
 *  compiling (http://www.infradead.org/~tgr/libnl/).
 *
 * Revision history: 
 *  2011/07/06 Jonathan Ellithorpe: "Let there be a register access
 *                                  library..."
 */

/* Header files provided by libnl (probably in /usr/local/include). */
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

/* Standard header files. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <errno.h>

/* NF10 specific generic netlink definitions  */
#include "nf10_genl.h"

static int  driver_connect(void);
static void driver_disconnect(void);

struct nl_sock *nf10_genl_sock;
int nf10_genl_family;

static int driver_connect()
{
    int err;

    nf10_genl_sock = nl_socket_alloc();
    if(nf10_genl_sock == NULL)
        return -NLE_NOMEM;

    err = genl_connect(nf10_genl_sock);
    if(err) {
        nl_socket_free(nf10_genl_sock);
        return err;
    }

    nf10_genl_family = genl_ctrl_resolve(nf10_genl_sock, NF10_GENL_FAMILY_NAME);
    if(nf10_genl_family < 0) {
        nl_socket_free(nf10_genl_sock);
        return nf10_genl_family;
    }

    return 0;
}

static void driver_disconnect()
{
    nl_socket_free(nf10_genl_sock);
}

static int nf10_reg_rd_recv_msg_cb(struct nl_msg *msg, void *arg)
{
    struct nlmsghdr *nlh;
    struct nlattr   *na;
    struct nlattr   *attrs[NF10_GENL_A_MAX + 1];
    uint32_t        *val_ptr;

    val_ptr = (uint32_t*)arg;

    nlh = nlmsg_hdr(msg);

    genlmsg_parse(nlh, 0, attrs, NF10_GENL_A_MAX, 0);

    na = attrs[NF10_GENL_A_REGVAL32];
    if(na) {
        if(nla_data(na) == NULL)
            return -NLE_NOATTR;
        else {
            *val_ptr = *(uint32_t*)nla_data(na);
            return 0;
        }
    } else 
        return -NLE_MISSING_ATTR;
}

int nf10_reg_rd(uint32_t addr, uint32_t *val_ptr)
{
    struct nl_msg   *msg;
    int             err;   

    err = driver_connect();
    if(err) 
        return err;

    msg = nlmsg_alloc();
    if(msg == NULL) {
        driver_disconnect();
        return -NLE_NOMEM;
    }

    /* genlmsg_put will fill in the fields of the nlmsghdr and the genlmsghdr. */
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nf10_genl_family, 0, 0,
            NF10_GENL_C_REG_RD, NF10_GENL_FAMILY_VERSION);

    nla_put_u32(msg, NF10_GENL_A_ADDR32, addr);

    /* nl_send_auto will automatically fill in the PID and the sequence number,
     * and also add an NLM_F_REQUEST flag. It will also add an NLM_F_ACK
     * flag unless the netlink socket has the NL_NO_AUTO_ACK flag set. */
    err = nl_send_auto(nf10_genl_sock, msg);
    if(err < 0)
        return err;

    nlmsg_free(msg);

    nl_socket_modify_cb(nf10_genl_sock, NL_CB_VALID, NL_CB_CUSTOM, nf10_reg_rd_recv_msg_cb, (void*)val_ptr);
    
    err = nl_recvmsgs_default(nf10_genl_sock);
    if(err)
        return err;

    driver_disconnect();    

    return 0;
}

static int nf10_reg_wr_recv_ack_cb(struct nl_msg *msg, void *arg)
{
    return 0;
}

int nf10_reg_wr(uint32_t addr, uint32_t val)
{
    struct nl_msg   *msg;
    int             err;    

    err = driver_connect();
    if(err)
        return err;

    msg = nlmsg_alloc();
    if(msg == NULL) {
        driver_disconnect();
        return -NLE_NOMEM;
    }

    /* genlmsg_put will fill in the fields of the nlmsghdr and the genlmsghdr. */
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nf10_genl_family, 0, 0,
            NF10_GENL_C_REG_WR, NF10_GENL_FAMILY_VERSION);

    nla_put_u32(msg, NF10_GENL_A_ADDR32, addr);
    nla_put_u32(msg, NF10_GENL_A_REGVAL32, val);

    /* nl_send_auto will automatically fill in the PID and the sequence number,
     * and also add an NLM_F_REQUEST flag. It will also add an NLM_F_ACK
     * flag unless the netlink socket has the NL_NO_AUTO_ACK flag set. */
    err = nl_send_auto(nf10_genl_sock, msg);
    if(err < 0)
        return err;

    nlmsg_free(msg);

    nl_socket_modify_cb(nf10_genl_sock, NL_CB_ACK, NL_CB_CUSTOM, nf10_reg_wr_recv_ack_cb, NULL);

    /* FIXME: this function will return even if there's no ACK in the buffer. I.E. it doesn't
     * seem to wait for the ACK to be received... Ideally we'd have the behavior that getting an 
     * ACK tells us everything is OK, otherwise we time out on waiting for an ACK and tell this
     * to the user. */
    nl_recvmsgs_default(nf10_genl_sock);

    driver_disconnect();    
}
