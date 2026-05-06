#pragma once

#include <netinet/if_ether.h>    
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <stdio.h>

#define CHAN_PINGPONG 1

typedef struct shm_buffer_t {
    uint64_t size;
    // trailing bytes of size
} shm_buffer_t;

typedef struct ethhdr ethhdr;
typedef struct iphdr iphdr;
typedef struct udphdr udphdr;

static const size_t hdrs_len = sizeof(ethhdr) + sizeof(iphdr) + sizeof(udphdr); 
