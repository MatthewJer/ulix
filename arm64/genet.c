/*-
 * Ulix ARM64 -- the Ethernet controller of the Raspberry Pi 4B ("GENET", Broadcom GENETv5)
 *
 * The register names, the register layout and the order of the steps that set the controller up are those of the FreeBSD driver
 * sys/arm64/broadcom/genet/if_genet.c and its header if_genetreg.h, which derive from NetBSD's bcmgenet.c. The code below is
 * written new for Ulix (one ring each for sending and receiving, no filters; polling or interrupts); the FreeBSD files are
 * under this licence:
 *
 * Copyright (c) 2020 Michael J Karels
 * Copyright (c) 2016, 2020 Jared McNeill <jmcneill@invisible.ca>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * NOT TESTED ON THE REAL BOARD: qemu has no model of this controller. What is written here follows the FreeBSD driver; the first
 * start on a Raspberry Pi will show what is missing.
 */

// How the controller works, in short. It has two parts: the MAC ("UMAC", it sends and receives frames) and the DMA engine that
// moves frames between memory and the MAC. Both directions have a ring of 256 descriptors. A descriptor says where a frame is in
// memory (or where one may be put) and how long it is; unusually, the descriptors do not live in RAM but in registers of the
// controller (12 bytes each). Two numbers per ring tell whom the next descriptor belongs to: for sending, the driver writes the
// "producer index" when it has filled descriptors and the controller advances the "consumer index" when it has sent them; for
// receiving, the controller advances the producer index when it has stored frames and the driver the consumer index when it has
// taken them. The indexes count up to 0xffff and wrap; the descriptor in use is the index modulo 256.
//
// The cable is not connected to the controller but to a separate chip, the PHY (a Broadcom BCM54213PE). The PHY is configured
// over a two-wire bus ("MDIO") whose registers are the same on all Ethernet PHYs (IEEE 802.3, clause 22): register 0 control,
// 1 status, 4 and 5 what we and the partner can do, 9 and 10 the same for gigabit speed. The firmware has already started the PHY;
// here it is told to negotiate with the other side and we read what speed came out.

#include "net.h"
#include "plat.h"
#include "delay.h"
#include "cache.h"

// ---- registers (offsets from the base address) ----
#define SYS_REV_CTRL        0x000          // bits 27..24: major version, 6 = GENET v5
#define SYS_PORT_CTRL       0x004
#define  PORT_MODE_EXT_GPHY 3              // an external PHY chip
#define SYS_RBUF_FLUSH_CTRL 0x008
#define  RBUF_FLUSH_RESET   (1 << 1)
#define EXT_RGMII_OOB_CTRL  0x08c
#define  RGMII_ID_MODE_DISABLE (1 << 16)
#define  RGMII_MODE_EN      (1 << 6)
#define  RGMII_OOB_DISABLE  (1 << 5)
#define  RGMII_LINK         (1 << 4)
#define INTRL2_CPU_STAT       0x200        // which interrupts are pending
#define INTRL2_CPU_CLEAR      0x208        // writing a bit here clears that interrupt
#define INTRL2_CPU_STAT_MASK  0x20c        // which interrupts are off
#define INTRL2_CPU_SET_MASK   0x210        // bits set here are interrupts that stay off
#define INTRL2_CPU_CLEAR_MASK 0x214        // bits set here turn those interrupts on
#define  IRQ_TXDMA_DONE       (1 << 16)    // frames have been sent
#define  IRQ_RXDMA_DONE       (1 << 13)    // frames have arrived
#define RBUF_CTRL           0x300
#define  RBUF_ALIGN_2B      (1 << 1)       // two bytes of padding before the frame, so that the IP header is aligned
#define  RBUF_64B_EN        (1 << 0)       // a 64-byte status block before the frame (we do not want it)
#define RBUF_TBUF_SIZE_CTRL 0x3b4
#define TBUF_CTRL           0x600
#define UMAC_CMD            0x808
#define  CMD_LCL_LOOP_EN    (1 << 15)
#define  CMD_SW_RESET       (1 << 13)
#define  CMD_PROMISC        (1 << 4)       // receive every frame, whatever its destination address
#define  CMD_SPEED          (3 << 2)
#define  CMD_SPEED_10       (0 << 2)
#define  CMD_SPEED_100      (1 << 2)
#define  CMD_SPEED_1000     (2 << 2)
#define  CMD_RXEN           (1 << 1)
#define  CMD_TXEN           (1 << 0)
#define UMAC_MAC0           0x80c          // our Ethernet address: first four bytes
#define UMAC_MAC1           0x810          // ... last two bytes
#define UMAC_MAX_FRAME_LEN  0x814
#define UMAC_MIB_CTRL       0xd80          // the statistics counters
#define MDIO_CMD            0xe14
#define  MDIO_START_BUSY    (1 << 29)
#define  MDIO_READ_FAILED   (1 << 28)
#define  MDIO_READ          (1 << 27)
#define  MDIO_WRITE         (1 << 26)

#define DESCRIPTORS  256                   // descriptors of a ring (the hardware's size)
#define DESC_SIZE    12                    // bytes of a descriptor: status, address low, address high
#define QUEUE        16                    // the hardware has 17 rings; the "default" one, number 16, is all we use
#define RING_SIZE    0x40                  // bytes of registers of a ring
#define BUFFER       2048                  // room for one frame (at most 1514 bytes plus the padding)

#define RX_BASE 0x2000                     // descriptors (12 bytes each) from here; the rings' registers from +0xc00; the DMA's from +0x1040
#define TX_BASE 0x4000
#define RING(base, q)       ((base) + 0xc00 + RING_SIZE * (q))
// registers of a ring, the same offsets in the receive and the transmit part where they mean the same
#define RX_PROD_INDEX(q)    (RING (RX_BASE, q) + 0x08)
#define RX_CONS_INDEX(q)    (RING (RX_BASE, q) + 0x0c)
#define TX_CONS_INDEX(q)    (RING (TX_BASE, q) + 0x08)
#define TX_PROD_INDEX(q)    (RING (TX_BASE, q) + 0x0c)
#define DMA_RING_CFG(base)  ((base) + 0x1040 + 0x00)       // which rings are on (bit n = ring n)
#define DMA_CTRL(base)      ((base) + 0x1040 + 0x04)
#define  DMA_CTRL_EN        (1 << 0)
#define  DMA_CTRL_RING(q)   (1 << ((q) + 1))
#define SCB_BURST_SIZE(base) ((base) + 0x1040 + 0x0c)

#define DESC_STATUS(base, i)  ((base) + DESC_SIZE * (i) + 0x00)
#define DESC_ADDR_LO(base, i) ((base) + DESC_SIZE * (i) + 0x04)
#define DESC_ADDR_HI(base, i) ((base) + DESC_SIZE * (i) + 0x08)
#define DESC_BUFLEN_SHIFT   16             // the length is in bits 27..16 of the status
#define DESC_EOP            (1 << 14)      // last descriptor of a frame
#define DESC_SOP            (1 << 13)      // first descriptor of a frame
#define TX_DESC_CRC         (1 << 6)       // the controller adds the checksum at the end of the frame
#define TX_DESC_QTAG_MASK   0x1f80
#define RX_DESC_ERROR       (1 << 2)

#define PHY_ADDRESS 1                      // the PHY of the Raspberry Pi 4 answers on MDIO address 1 (ethernet-phy@1 in its device tree)

static u64 base = 0;                       // virtual address of the controller
static u8 mac[6];
static u64 rx_pa, tx_pa;                   // the 256 receive buffers (one area), the one buffer for sending
static u32 rx_cons, tx_prod;               // our side of the indexes

static u32  rd (int off)        { return *(volatile u32 *)(base + off); }
static void wr (int off, u32 v) { *(volatile u32 *)(base + off) = v; }

// ---- the PHY, over MDIO ----
static int mdio_wait (void) {
  for (int i = 0; i < 1000; i++) { if (!(rd (MDIO_CMD) & MDIO_START_BUSY)) return 0; delay_us (10); }
  return -1;
}

static int phy_read (int reg) {                                    // the 16 bit register 'reg' of the PHY, or -1
  wr (MDIO_CMD, MDIO_READ | (PHY_ADDRESS << 21) | (reg << 16));
  wr (MDIO_CMD, rd (MDIO_CMD) | MDIO_START_BUSY);                  // (the command is written first, then started)
  if (mdio_wait () != 0 || (rd (MDIO_CMD) & MDIO_READ_FAILED)) return -1;
  return rd (MDIO_CMD) & 0xffff;
}

static void phy_write (int reg, int value) {
  wr (MDIO_CMD, MDIO_WRITE | (PHY_ADDRESS << 21) | (reg << 16) | (value & 0xffff));
  wr (MDIO_CMD, rd (MDIO_CMD) | MDIO_START_BUSY);
  mdio_wait ();
}

// Let the PHY negotiate with the other end of the cable and wait (at most five seconds) until there is a link.
// Returns the speed in Mbit/s (10, 100, 1000), or 0 if there is no link (no cable, or nobody at the other end).
static int phy_link (void) {
  phy_write (0, 0x1200);                                           // control: autonegotiation on (bit 12), start again (bit 9)
  for (int ms = 0; ms < 5000; ms += 10) {
    int status = phy_read (1);                                     // status: bit 2 link is up, bit 5 negotiation is complete
    if (status > 0 && (status & 0x0004) && (status & 0x0020)) {
      int partner_gigabit = phy_read (10), partner = phy_read (5);
      if (partner_gigabit > 0 && (partner_gigabit & (1 << 11))) return 1000;       // the partner can do gigabit, full duplex
      if (partner > 0 && (partner & ((1 << 8) | (1 << 7)))) return 100;           // 100 Mbit/s, full or half duplex
      return 10;
    }
    delay_us (10000);
  }
  return 0;
}

// ---- the controller ----
// Back to the state after power-on: the buffers of the MAC, then the MAC itself, then the statistics.
static void reset (void) {
  u32 v = rd (SYS_RBUF_FLUSH_CTRL);
  wr (SYS_RBUF_FLUSH_CTRL, v | RBUF_FLUSH_RESET);  delay_us (10);
  wr (SYS_RBUF_FLUSH_CTRL, v & ~RBUF_FLUSH_RESET);  delay_us (10);
  wr (SYS_RBUF_FLUSH_CTRL, 0);                      delay_us (10);
  wr (UMAC_CMD, 0);
  wr (UMAC_CMD, CMD_LCL_LOOP_EN | CMD_SW_RESET);    delay_us (10);
  wr (UMAC_CMD, 0);
  wr (UMAC_MIB_CTRL, 7);  wr (UMAC_MIB_CTRL, 0);    // reset and then count again: receive, transmit, runt frames
}

static void dma_off (void) {
  wr (TX_BASE + 0x1040 + 0x04, rd (TX_BASE + 0x1040 + 0x04) & ~(DMA_CTRL_EN | DMA_CTRL_RING (QUEUE)));
  wr (RX_BASE + 0x1040 + 0x04, rd (RX_BASE + 0x1040 + 0x04) & ~(DMA_CTRL_EN | DMA_CTRL_RING (QUEUE)));
}

static void tx_ring_init (void) {
  int q = QUEUE;
  tx_prod = 0;
  wr (SCB_BURST_SIZE (TX_BASE), 0x08);
  wr (RING (TX_BASE, q) + 0x00, 0);  wr (RING (TX_BASE, q) + 0x04, 0);          // read pointer
  wr (TX_CONS_INDEX (q), 0);  wr (TX_PROD_INDEX (q), 0);
  wr (RING (TX_BASE, q) + 0x10, (DESCRIPTORS << 16) | BUFFER);                  // number of descriptors, size of a buffer
  wr (RING (TX_BASE, q) + 0x14, 0);  wr (RING (TX_BASE, q) + 0x18, 0);          // start address
  wr (RING (TX_BASE, q) + 0x1c, DESCRIPTORS * DESC_SIZE / 4 - 1);  wr (RING (TX_BASE, q) + 0x20, 0);     // end address (in words)
  wr (RING (TX_BASE, q) + 0x24, 1);                                             // interrupt after every frame (not used)
  wr (RING (TX_BASE, q) + 0x28, 0);
  wr (RING (TX_BASE, q) + 0x2c, 0);  wr (RING (TX_BASE, q) + 0x30, 0);          // write pointer
  wr (DMA_RING_CFG (TX_BASE), 1 << q);                                          // this ring is on
  wr (DMA_CTRL (TX_BASE), rd (DMA_CTRL (TX_BASE)) | DMA_CTRL_EN | DMA_CTRL_RING (q));
}

static void rx_ring_init (void) {
  int q = QUEUE;
  rx_cons = 0;
  wr (SCB_BURST_SIZE (RX_BASE), 0x08);
  wr (RING (RX_BASE, q) + 0x00, 0);  wr (RING (RX_BASE, q) + 0x04, 0);          // write pointer
  wr (RX_PROD_INDEX (q), 0);  wr (RX_CONS_INDEX (q), 0);
  wr (RING (RX_BASE, q) + 0x10, (DESCRIPTORS << 16) | BUFFER);
  wr (RING (RX_BASE, q) + 0x14, 0);  wr (RING (RX_BASE, q) + 0x18, 0);
  wr (RING (RX_BASE, q) + 0x1c, DESCRIPTORS * DESC_SIZE / 4 - 1);  wr (RING (RX_BASE, q) + 0x20, 0);
  wr (RING (RX_BASE, q) + 0x28, (5 << 16) | (DESCRIPTORS >> 4));                // flow control thresholds
  wr (RING (RX_BASE, q) + 0x2c, 0);  wr (RING (RX_BASE, q) + 0x30, 0);          // read pointer
  wr (DMA_RING_CFG (RX_BASE), 1 << q);
  for (int i = 0; i < DESCRIPTORS; i++) {                                       // every descriptor gets its own buffer, for good
    u64 buffer = rx_pa + (u64)i * BUFFER;
    wr (DESC_ADDR_LO (RX_BASE, i), (u32)buffer);
    wr (DESC_ADDR_HI (RX_BASE, i), buffer >> 32);
  }
  wr (DMA_CTRL (RX_BASE), rd (DMA_CTRL (RX_BASE)) | DMA_CTRL_EN | DMA_CTRL_RING (q));
}

static int init (void) {
  if (!plat.genet) return -1;
  base = KERNEL_VA + plat.genet;
  u32 major = (rd (SYS_REV_CTRL) >> 24) & 0xf;
  if (major != 6) { printf ("genet: unknown controller (revision register 0x%x)\n", rd (SYS_REV_CTRL)); return -1; }
  printf ("genet: Ethernet controller GENET v5.%d\n", (rd (SYS_REV_CTRL) >> 16) & 0xf);

  // our Ethernet address: from the device tree (the firmware puts the one of the board there), else what the controller still holds
  if (plat.genet_has_mac) for (int i = 0; i < 6; i++) mac[i] = plat.genet_mac[i];
  else {
    u32 lo = rd (UMAC_MAC0), hi = rd (UMAC_MAC1);
    mac[0] = lo >> 24;  mac[1] = lo >> 16;  mac[2] = lo >> 8;  mac[3] = lo;  mac[4] = hi >> 8;  mac[5] = hi;
  }

  reset ();
  dma_off ();
  wr (INTRL2_CPU_SET_MASK, 0xffffffff);                                         // all interrupts off (start_irq () turns two on, later)
  wr (SYS_PORT_CTRL, PORT_MODE_EXT_GPHY);

  wr (UMAC_MAC0, ((u32)mac[0] << 24) | (mac[1] << 16) | (mac[2] << 8) | mac[3]);
  wr (UMAC_MAC1, (mac[4] << 8) | mac[5]);
  wr (UMAC_CMD, rd (UMAC_CMD) | CMD_PROMISC);                                   // no address filter: everything comes in, net.c picks

  rx_pa = alloc_frames_contig (DESCRIPTORS * BUFFER / PAGE_SIZE);
  tx_pa = alloc_frame ();
  tx_ring_init ();
  rx_ring_init ();

  wr (UMAC_MAX_FRAME_LEN, 1536);
  wr (RBUF_CTRL, (rd (RBUF_CTRL) | RBUF_ALIGN_2B) & ~RBUF_64B_EN);              // two bytes of padding, no status block
  wr (TBUF_CTRL, rd (TBUF_CTRL) & ~RBUF_64B_EN);
  wr (RBUF_TBUF_SIZE_CTRL, 1);
  wr (UMAC_CMD, rd (UMAC_CMD) | CMD_TXEN | CMD_RXEN);

  // the link: speed from the PHY, then tell the MAC (the "RGMII" connection between MAC and PHY) what the PHY has decided.
  // The PHY of the Pi 4 adds the clock delay for the received data itself ("rgmii-rxid"), so the MAC does not.
  int speed = phy_link ();
  if (speed == 0) printf ("genet: no link (is the cable connected?)\n");
  else {
    printf ("genet: link up, %d Mbit/s\n", speed);
    u32 v = rd (EXT_RGMII_OOB_CTRL);
    v &= ~(RGMII_OOB_DISABLE | RGMII_ID_MODE_DISABLE);
    wr (EXT_RGMII_OOB_CTRL, v | RGMII_LINK | RGMII_MODE_EN);
    wr (UMAC_CMD, (rd (UMAC_CMD) & ~CMD_SPEED) | (speed == 1000 ? CMD_SPEED_1000 : speed == 100 ? CMD_SPEED_100 : CMD_SPEED_10));
  }
  return 0;
}

static const u8 *mac_address (void) { return mac; }

// Send one frame: copy it into the buffer, fill the next descriptor, tell the controller (producer index), wait until it has gone.
static int send_frame (const void *frame, int len) {
  if (len < 14 || len > 1514) return -1;
  u8 *buffer = KVA (tx_pa);
  for (int i = 0; i < len; i++) buffer[i] = ((const u8 *)frame)[i];
  while (len < 60) buffer[len++] = 0;                                           // a frame has at least 60 bytes (without the checksum)
  cache_clean (buffer, len);
  int i = tx_prod & (DESCRIPTORS - 1);
  wr (DESC_ADDR_LO (TX_BASE, i), (u32)tx_pa);
  wr (DESC_ADDR_HI (TX_BASE, i), tx_pa >> 32);
  wr (DESC_STATUS (TX_BASE, i), TX_DESC_QTAG_MASK | DESC_SOP | DESC_EOP | TX_DESC_CRC | ((u32)len << DESC_BUFLEN_SHIFT));
  tx_prod = (tx_prod + 1) & 0xffff;
  wr (TX_PROD_INDEX (QUEUE), tx_prod);
  for (int us = 0; us < 100000; us += 10) {                                     // the controller is done when its consumer index has caught up
    if ((rd (TX_CONS_INDEX (QUEUE)) & 0xffff) == tx_prod) return 0;
    delay_us (10);
  }
  printf ("genet: the frame is not sent\n");
  return -1;
}

// Has a frame arrived? The controller advances its producer index for every frame it has stored; we are behind by rx_cons.
static int recv_frame (void *out, int max) {
  u32 produced = rd (RX_PROD_INDEX (QUEUE)) & 0xffff;
  if (produced == rx_cons) return 0;
  int i = rx_cons & (DESCRIPTORS - 1);
  u32 status = rd (DESC_STATUS (RX_BASE, i));
  int len = (status >> DESC_BUFLEN_SHIFT) & 0xfff;
  u8 *buffer = KVA (rx_pa + (u64)i * BUFFER);
  int result = 0;
  if ((status & (DESC_SOP | DESC_EOP | RX_DESC_ERROR)) == (DESC_SOP | DESC_EOP) && len > 2) {     // one whole frame, no error
    cache_flush_inval (buffer, BUFFER);
    len -= 2;                                                                   // the two bytes of padding before the frame
    if (len > max) len = max;
    for (int k = 0; k < len; k++) ((u8 *)out)[k] = buffer[2 + k];
    result = len;
  }
  rx_cons = (rx_cons + 1) & 0xffff;
  wr (RX_CONS_INDEX (QUEUE), rx_cons);                                          // the descriptor (and its buffer) goes back to the controller
  return result;
}

// Interrupts: the controller has many; we want two, "frames have arrived" and "frames have been sent". The handler clears whatever is
// pending; the work itself is done by the network thread (netd.c), which finds the frames by looking at the indexes.
static int start_irq (void) {
  if (!plat.genet_irq) return 0;
  wr (INTRL2_CPU_CLEAR_MASK, IRQ_RXDMA_DONE | IRQ_TXDMA_DONE);
  return plat.genet_irq;
}

static void ack (void) { wr (INTRL2_CPU_CLEAR, rd (INTRL2_CPU_STAT) & ~rd (INTRL2_CPU_STAT_MASK)); }

const struct net_driver genet_driver = { "genet0", init, mac_address, send_frame, recv_frame, start_irq, ack };
