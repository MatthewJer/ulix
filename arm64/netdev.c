// Ulix ARM64 -- which network card is there: tries the drivers in turn and passes the work on to the one that finds its card

#include "net.h"

static const struct net_driver *drivers[] = { &genet_driver, &virtio_net_driver };       // the Pi's Ethernet, the card of qemu
static const struct net_driver *card = 0;                                                // the driver in use

int net_drop_tx = 0;                                           // tests: while > 0, a frame to send is thrown away (as if the network lost it)

int net_dev_init (void) {
  for (unsigned i = 0; i < sizeof drivers / sizeof drivers[0]; i++)
    if (drivers[i]->init () == 0) { card = drivers[i]; net_if_name = card->name; return 0; }
  return -1;
}

const u8 *net_dev_mac (void) { return card->mac (); }
int net_dev_recv (void *out, int max) { return card ? card->recv (out, max) : 0; }
int net_dev_start_irq (void) { return card && card->start_irq ? card->start_irq () : 0; }
void net_dev_ack (void) { if (card && card->ack) card->ack (); }

int net_dev_send (const void *frame, int len) {
  if (!card) return -1;
  if (net_drop_tx > 0) { net_drop_tx--; return 0; }
  return card->send (frame, len);
}
