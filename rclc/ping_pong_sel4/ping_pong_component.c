#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <netinet/in.h>
#include <microkit.h>
#include "common.h"
#include "util/networking.h"
#include "util/spsc_queue.h"


char *pp2vmm;
uint64_t pp2vmm_size = 0x100000;
spsc_queue_t *spsc_pp2vmm;

char *vmm2pp;
uint64_t vmm2pp_size = 0x100000;
spsc_queue_t *spsc_vmm2pp;

static ethhdr txEthHdr;
static iphdr txIpHdr;
static udphdr txUdpHdr;
static bool guest_ready = false;

void init(void)
{
    microkit_dbg_puts("ping_pong: Starting network verification test...\n");

    txEthHdr = make_ethhdr("02:00:00:00:00:02", "02:00:00:00:00:01");
    txIpHdr = make_iphdr("10.0.2.100", "10.0.2.15");
    txUdpHdr = make_udphdr(54321, 12345);

    spsc_pp2vmm = (spsc_queue_t *)pp2vmm;
    assert(spsc_init(spsc_pp2vmm, pp2vmm + sizeof(spsc_queue_t), pp2vmm + pp2vmm_size, 11));
    spsc_vmm2pp = (spsc_queue_t *)vmm2pp;
    assert(spsc_init(spsc_vmm2pp, vmm2pp + sizeof(spsc_queue_t), vmm2pp + vmm2pp_size, 11));
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case CHAN_READY: {
        if (guest_ready) {
            break;
        }
        guest_ready = true;
        microkit_dbg_puts("ping_pong: Guest is ready, sending first packet\n");

        char *txPktBuf = spsc_new_block(spsc_pp2vmm);
        const char payload[] = "HELLO";
        make_pkt(txPktBuf, 2048, payload, sizeof(payload), &txEthHdr, &txIpHdr, &txUdpHdr);
        microkit_dbg_puts("Sending payload = '");
        microkit_dbg_puts(payload);
        microkit_dbg_puts("'\n");
        spsc_push(spsc_pp2vmm);

        microkit_notify(CHAN_PINGPONG);
        break;
    }
    case CHAN_PINGPONG: {
        char *txPktBuf = spsc_new_block(spsc_pp2vmm);
        char *rxPkt = spsc_front_block(spsc_vmm2pp);

        char *payload = get_payload(rxPkt);
        size_t payloadLen = get_payload_len(rxPkt);
        make_pkt(txPktBuf, 2048, payload, payloadLen, &txEthHdr, &txIpHdr, &txUdpHdr);

        microkit_dbg_puts("Received payload = '");
        microkit_dbg_puts(payload);
        microkit_dbg_puts("'\n");

        spsc_pop(spsc_vmm2pp);
        spsc_push(spsc_pp2vmm);

        microkit_notify(CHAN_PINGPONG);
        break;
    }
    default:
        microkit_dbg_puts("ping_pong: Unknown channel\n");
        break;
    }
}
