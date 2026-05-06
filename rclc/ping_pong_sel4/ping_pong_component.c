#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <netinet/in.h>
#include <microkit.h>
#include "common.h"

static volatile shm_buffer_t *pp_comm_buffer;

#define SWAP(x,y) do \ 
   { unsigned char swap_temp[sizeof(x) == sizeof(y) ? (signed)sizeof(x) : -1]; \
     memcpy(swap_temp,&y,sizeof(x)); \
     memcpy(&y,&x,       sizeof(x)); \
     memcpy(&x,swap_temp,sizeof(x)); \
    } while(0)

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
