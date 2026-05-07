#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <netinet/in.h>
#include <microkit.h>
#include "common.h"

static volatile shm_buffer_t *pp_comm_buffer;

#define SWAP(x,y) do { \
        unsigned char tmp[sizeof(x) == sizeof(y) ? (signed)sizeof(x) : -1]; \
        memcpy(tmp, &y, sizeof(x)); \
        memcpy(&y, &x, sizeof(x));  \
        memcpy(&x, tmp, sizeof(x)); \
    } while(0)

static uint16_t ip_chksum(struct iphdr *ip) {
    uint32_t sum = 0;
    uint16_t *buf = (uint16_t *)ip;
    ip->check = 0;
    int numBytes = ip->ihl /* num 4 byte words */ * 4;
    for (int i = 0; i < numBytes / 2 /* num 2 byte words */; i++) {
        sum += buf[i];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

void init(void)
{
    microkit_dbg_puts("ping_pong: Starting network verification test...\n");
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case CHAN_PINGPONG: {
        uint32_t len = pp_comm_buffer->size;
        char *data = (char *)(pp_comm_buffer + 1);

        struct ethhdr *ethHdr = (ethhdr *)data;
        struct iphdr *ipHdr = (iphdr *)(data + sizeof(ethhdr));
        struct udphdr *udpHdr = (udphdr *)(data + sizeof(ethhdr) + sizeof(iphdr));

        SWAP(ethHdr->h_dest, ethHdr->h_source);
        SWAP(ipHdr->saddr, ipHdr->daddr);
        SWAP(udpHdr->uh_sport, udpHdr->uh_dport);
        
        ipHdr->check = ip_chksum(ipHdr);
        udpHdr->uh_sum = 0;

        microkit_dbg_puts("ping_pong: ping_pong -> vmm: '");
        microkit_dbg_puts(data + hdrs_len);
        microkit_dbg_puts("'\n");

        microkit_notify(CHAN_PINGPONG);
        break;
    }
    default:
        microkit_dbg_puts("ping_pong: Unknown channel\n");
        break;
    }
}
