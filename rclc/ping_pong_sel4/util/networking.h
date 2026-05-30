#include "../common.h"
#include <arpa/inet.h>
#include <assert.h>

typedef struct view_t
{
    char *buf;
    size_t len;
} view_t;

static void set_ip_chksum(struct iphdr *ip) {
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
    ip->check = ~sum;
}

void make_pkt(char *outBuf, size_t outBufLen, const char *payload, size_t payloadLen,
            const ethhdr* ethHdr, const iphdr *ipHdr, const udphdr *udpHdr)
{
    ethhdr *pktEthHdr = (ethhdr *)(outBuf);
    iphdr *pktIpHdr = (iphdr *)(outBuf + sizeof(ethhdr));
    udphdr *pktUdpHdr = (udphdr *)(outBuf + sizeof(ethhdr) + sizeof(iphdr));

    *pktEthHdr = *ethHdr;
    *pktIpHdr = *ipHdr;
    *pktUdpHdr = *udpHdr;

    memcpy(pktUdpHdr + 1, payload, payloadLen);

    pktIpHdr->tot_len = htons(sizeof(iphdr) + sizeof(udphdr) + payloadLen);
    set_ip_chksum(pktIpHdr);

    pktUdpHdr->uh_ulen = htons(sizeof(udphdr) + payloadLen);
    pktUdpHdr->uh_sum = 0; // disable udpchksum
}

iphdr make_iphdr(const char *srcIp, const char *dstIp)
{
    iphdr ipHdr;
    ipHdr.version = 4;
    ipHdr.ihl = 5;
    ipHdr.tos = 0;
    
    ipHdr.tot_len = 0; // set in make_pkt
    ipHdr.id = htons(54321);
    ipHdr.frag_off = 0;
    
    ipHdr.ttl = 64;
    ipHdr.protocol = IPPROTO_UDP;
    
    ipHdr.saddr = inet_addr(srcIp);
    ipHdr.daddr = inet_addr(dstIp);
    ipHdr.check = 0; // set in make_pkt

    return ipHdr;
}

udphdr make_udphdr(uint16_t srcPort, uint16_t dstPort)
{
    udphdr udpHdr;
    udpHdr.uh_sport = htons(srcPort);
    udpHdr.uh_dport = htons(dstPort);
    return udpHdr;
}

void parse_mac(const char *mac, /* out-param */ uint8_t *macBytes) 
{
    int ret = sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                    &macBytes[0], &macBytes[1], &macBytes[2],
                    &macBytes[3], &macBytes[4], &macBytes[5]);
    assert(ret == 6);
}

ethhdr make_ethhdr(const char *srcMac, const char *dstMac)
{
    ethhdr ethHdr;
    parse_mac(dstMac, ethHdr.h_dest);
    parse_mac(srcMac, ethHdr.h_source);
    ethHdr.h_proto = htons(ETH_P_IP);
    return ethHdr;
}

bool is_udp(const char *pkt)
{
    const ethhdr *ethHdr = (const ethhdr *)(pkt);
    const iphdr *ipHdr = (const iphdr *)(pkt + sizeof(ethhdr));
    return ipHdr->protocol == IPPROTO_UDP;
}

size_t get_payload_len(const char *pkt)
{
    assert(is_udp(pkt));
    const udphdr *udpHdr = (const udphdr *)(pkt + sizeof(ethhdr) + sizeof(iphdr));
    return ntohs(udpHdr->uh_ulen) - sizeof(udpHdr);
}

size_t pkt_len(const char *pkt)
{
    assert(is_udp(pkt));
    return sizeof(ethhdr) + sizeof(iphdr) + sizeof(udphdr) + get_payload_len(pkt);
}

char *get_payload(char *pkt)
{
    assert(is_udp(pkt));
    return pkt + sizeof(ethhdr) + sizeof(iphdr) + sizeof(udphdr);
}

size_t put_pkt_hex(char *outBuf, const char *pkt, size_t pktLen)
{
    size_t bytesPrinted = 0;
    for(size_t i = 0; i < pktLen; i++) {
        bytesPrinted += sprintf(outBuf + bytesPrinted, "%02x ", (uint8_t)pkt[i]);
    }

    return bytesPrinted;   
}
