/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2001 Cluster File Systems, Inc. <braam@clusterfs.com>
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Basic library routines. 
 *
 */

#ifndef _QSWNAL_H
#define _QSWNAL_H
#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif

#ifdef PROPRIETARY_ELAN
# include <qsw/kernel.h>
#else
# include <qsnet/kernel.h>
#endif

#undef printf                                   /* nasty QSW #define */

#include <linux/config.h>
#include <linux/module.h>

#include <elan3/elanregs.h>
#include <elan3/elandev.h>
#include <elan3/elanvp.h>
#include <elan3/elan3mmu.h>
#include <elan3/elanctxt.h>
#include <elan3/elandebug.h>
#include <elan3/urom_addrs.h>
#include <elan3/busops.h>
#include <elan3/kcomm.h>

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/unistd.h>
#include <net/sock.h>
#include <linux/uio.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <asm/segment.h>

#define DEBUG_SUBSYSTEM S_QSWNAL

#include <linux/kp30.h>
#include <portals/p30.h>
#include <portals/lib-p30.h>

#define KQSW_OPTIMIZE_GETS 1

#define KQSW_CHECKSUM   0
#if KQSW_CHECKSUM
typedef unsigned long kqsw_csum_t;
#define KQSW_CSUM_SIZE  (2 * sizeof (kqsw_csum_t))
#else
#define KQSW_CSUM_SIZE  0
#endif
#define KQSW_HDR_SIZE   (sizeof (ptl_hdr_t) + KQSW_CSUM_SIZE)

/*
 *  Elan NAL
 */
#define EP_SVC_LARGE_PORTALS_SMALL      (0x10)  /* Portals over elan port number (large payloads) */
#define EP_SVC_LARGE_PORTALS_LARGE      (0x11)  /* Portals over elan port number (small payloads) */
/* NB small/large message sizes are GLOBAL constants */

/*
 * Performance Tuning defines
 * NB no mention of PAGE_SIZE for interoperability
 */
#define KQSW_MAXPAYLOAD                 PTL_MTU
#define KQSW_SMALLPAYLOAD               ((4<<10) - KQSW_HDR_SIZE) /* small/large ep receiver breakpoint */

#define KQSW_TX_MAXCONTIG               (1<<10) /* largest payload that gets made contiguous on transmit */

#define KQSW_NTXMSGS                    8       /* # normal transmit messages */
#define KQSW_NNBLK_TXMSGS               256     /* # reserved transmit messages if can't block */

#define KQSW_NRXMSGS_LARGE              64      /* # large receive buffers */
#define KQSW_EP_ENVELOPES_LARGE         128     /* # large ep envelopes */

#define KQSW_NRXMSGS_SMALL              256     /* # small receive buffers */
#define KQSW_EP_ENVELOPES_SMALL         2048    /* # small ep envelopes */

#define KQSW_RESCHED                    100     /* # busy loops that forces scheduler to yield */

/*
 * derived constants
 */

#define KQSW_TX_BUFFER_SIZE     (KQSW_HDR_SIZE + KQSW_TX_MAXCONTIG)
/* The pre-allocated tx buffer (hdr + small payload) */

#define KQSW_NTXMSGPAGES        (btopr(KQSW_TX_BUFFER_SIZE) + 1 + btopr(KQSW_MAXPAYLOAD) + 1)
/* Reserve elan address space for pre-allocated and pre-mapped transmit
 * buffer and a full payload too.  Extra pages allow for page alignment */

#define KQSW_NRXMSGPAGES_SMALL  (btopr(KQSW_HDR_SIZE + KQSW_SMALLPAYLOAD))
/* receive hdr/payload always contiguous and page aligned */
#define KQSW_NRXMSGBYTES_SMALL  (KQSW_NRXMSGPAGES_SMALL * PAGE_SIZE)

#define KQSW_NRXMSGPAGES_LARGE  (btopr(KQSW_HDR_SIZE + KQSW_MAXPAYLOAD))
/* receive hdr/payload always contiguous and page aligned */
#define KQSW_NRXMSGBYTES_LARGE  (KQSW_NRXMSGPAGES_LARGE * PAGE_SIZE)
/* biggest complete packet we can receive (or transmit) */

/* Remote memory descriptor */
typedef struct
{
        __u32            kqrmd_neiov;           /* # frags */
        EP_IOVEC         kqrmd_eiov[0];         /* actual frags */
} kqswnal_remotemd_t;

typedef struct 
{
        struct list_head krx_list;              /* enqueue -> thread */
        EP_RCVR         *krx_eprx;              /* port to post receives to */
        EP_RXD          *krx_rxd;               /* receive descriptor (for repost) */
        E3_Addr          krx_elanaddr;          /* Elan address of buffer (contiguous in elan vm) */
        int              krx_npages;            /* # pages in receive buffer */
        int              krx_nob;               /* Number Of Bytes received into buffer */
        atomic_t         krx_refcount;          /* who's using me? */
        int              krx_rpc_completed;     /* I completed peer's RPC */
        kpr_fwd_desc_t   krx_fwd;               /* embedded forwarding descriptor */
        struct page     *krx_pages[KQSW_NRXMSGPAGES_LARGE]; /* pages allocated */
        struct iovec     krx_iov[KQSW_NRXMSGPAGES_LARGE]; /* iovec for forwarding */
}  kqswnal_rx_t;

typedef struct
{
        struct list_head  ktx_list;             /* enqueue idle/active */
        struct list_head  ktx_delayed_list;     /* enqueue delayedtxds */
        int               ktx_isnblk:1;         /* reserved descriptor? */
        int               ktx_state:7;          /* What I'm doing */
        uint32_t          ktx_basepage;         /* page offset in reserved elan tx vaddrs for mapping pages */
        int               ktx_npages;           /* pages reserved for mapping messages */
        int               ktx_nmappedpages;     /* # pages mapped for current message */
        int               ktx_port;             /* destination ep port */
        ptl_nid_t         ktx_nid;              /* destination node */
        void             *ktx_args[2];          /* completion passthru */
        E3_Addr           ktx_ebuffer;          /* elan address of ktx_buffer */
        char             *ktx_buffer;           /* pre-allocated contiguous buffer for hdr + small payloads */
        unsigned long     ktx_launchtime;       /* when (in jiffies) the transmit was launched */

        /* debug/info fields */
        pid_t             ktx_launcher;         /* pid of launching process */
        ptl_hdr_t        *ktx_wire_hdr;         /* portals header (wire endian) */

        int               ktx_nfrag;            /* # message frags */
        union {
                EP_IOVEC   iov[EP_MAXFRAG];     /* msg frags (elan vaddrs) */
                EP_DATAVEC datav[EP_MAXFRAG];   /* DMA frags (eolan vaddrs) */
        }                 ktx_frags;
} kqswnal_tx_t;

#define KTX_IDLE        0                       /* on kqn_(nblk_)idletxds */
#define KTX_SENDING     1                       /* local send */
#define KTX_FORWARDING  2                       /* routing a packet */
#define KTX_GETTING     3                       /* local optimised get */

typedef struct
{
        char               kqn_init;            /* what's been initialised */
        char               kqn_shuttingdown;    /* I'm trying to shut down */
        atomic_t           kqn_nthreads;        /* # threads still running */

        kqswnal_rx_t      *kqn_rxds;            /* all the receive descriptors */
        kqswnal_tx_t      *kqn_txds;            /* all the transmit descriptors */

        struct list_head   kqn_idletxds;        /* transmit descriptors free to use */
        struct list_head   kqn_nblk_idletxds;   /* reserved free transmit descriptors */
        struct list_head   kqn_activetxds;      /* transmit descriptors being used */
        spinlock_t         kqn_idletxd_lock;    /* serialise idle txd access */
        wait_queue_head_t  kqn_idletxd_waitq;   /* sender blocks here waiting for idle txd */
        struct list_head   kqn_idletxd_fwdq;    /* forwarded packets block here waiting for idle txd */
        
        spinlock_t         kqn_sched_lock;      /* serialise packet schedulers */
        wait_queue_head_t  kqn_sched_waitq;     /* scheduler blocks here */

        struct list_head   kqn_readyrxds;       /* rxds full of data */
        struct list_head   kqn_delayedfwds;     /* delayed forwards */
        struct list_head   kqn_delayedtxds;     /* delayed transmits */

        spinlock_t         kqn_statelock;       /* cb_cli/cb_sti */
        nal_cb_t          *kqn_cb;              /* -> kqswnal_lib */
        EP_DEV            *kqn_epdev;           /* elan device */
        EP_XMTR           *kqn_eptx;            /* elan transmitter */
        EP_RCVR           *kqn_eprx_small;      /* elan receiver (small messages) */
        EP_RCVR           *kqn_eprx_large;      /* elan receiver (large messages) */
        ELAN3_DMA_HANDLE  *kqn_eptxdmahandle;   /* elan reserved tx vaddrs */
        ELAN3_DMA_HANDLE  *kqn_eprxdmahandle;   /* elan reserved rx vaddrs */
        kpr_router_t       kqn_router;          /* connection to Kernel Portals Router module */

        ptl_nid_t          kqn_nid_offset;      /* this cluster's NID offset */
        int                kqn_nnodes;          /* this cluster's size */
        int                kqn_elanid;          /* this nodes's elan ID */
}  kqswnal_data_t;

/* kqn_init state */
#define KQN_INIT_NOTHING        0               /* MUST BE ZERO so zeroed state is initialised OK */
#define KQN_INIT_DATA           1
#define KQN_INIT_PTL            2
#define KQN_INIT_ALL            3

extern nal_cb_t        kqswnal_lib;
extern nal_t           kqswnal_api;
extern kqswnal_data_t  kqswnal_data;

extern int kqswnal_thread_start (int (*fn)(void *arg), void *arg);
extern void kqswnal_rxhandler(EP_RXD *rxd);
extern int kqswnal_scheduler (void *);
extern void kqswnal_fwd_packet (void *arg, kpr_fwd_desc_t *fwd);
extern void kqswnal_reply_complete (EP_RXD *rxd);
extern void kqswnal_requeue_rx (kqswnal_rx_t *krx);

static inline ptl_nid_t
kqswnal_elanid2nid (int elanid) 
{
        return (kqswnal_data.kqn_nid_offset + elanid);
}

static inline int
kqswnal_nid2elanid (ptl_nid_t nid) 
{
        /* not in this cluster? */
        if (nid < kqswnal_data.kqn_nid_offset ||
            nid >= kqswnal_data.kqn_nid_offset + kqswnal_data.kqn_nnodes)
                return (-1);
        
        return (nid - kqswnal_data.kqn_nid_offset);
}

static inline int
kqswnal_pages_spanned (void *base, int nob)
{
        unsigned long first_page = ((unsigned long)base) >> PAGE_SHIFT;
        unsigned long last_page  = (((unsigned long)base) + (nob - 1)) >> PAGE_SHIFT;

        LASSERT (last_page >= first_page);      /* can't wrap address space */
        return (last_page - first_page + 1);
}

#if KQSW_CHECKSUM
static inline kqsw_csum_t kqsw_csum (kqsw_csum_t sum, void *base, int nob)
{
        unsigned char *ptr = (unsigned char *)base;
        
        while (nob-- > 0)
                sum += *ptr++;
        
        return (sum);
}
#endif

#endif /* _QSWNAL_H */
