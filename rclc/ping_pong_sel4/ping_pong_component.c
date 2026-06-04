#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <microkit.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/custom_transport.h>
#include <std_msgs/msg/header.h>

#include "common.h"
#include "util/spsc_queue.h"
#include "util/networking.h"

#define STRING_BUFFER_LEN 100
#define DEVICE_ID 0

char *pp2vmm, *vmm2pp;
const uint64_t pp2vmm_size = 0x100000, vmm2pp_size = 0x100000;
spsc_queue_t *spsc_pp2vmm, *spsc_vmm2pp;

ethhdr txEthHdr;
iphdr txIpHdr;
udphdr txUdpHdr;

static rclc_support_t support;
static rcl_allocator_t allocator;
static rcl_node_t node;
static rcl_publisher_t pingPublisher, pongPublisher;
static rcl_subscription_t pingSubscriber, pongSubscriber;
static rclc_executor_t executor;
static std_msgs__msg__Header incomingPing, incomingPong, outgoingPing, outgoingPong;
static int seqNum = 0;

bool custom_transport_seL4_open(uxrCustomTransport *) { return true; }
bool custom_transport_seL4_close(uxrCustomTransport *) { return true; }
size_t custom_transport_seL4_write(uxrCustomTransport *, const uint8_t *buf, size_t len, uint8_t *errcode);
size_t custom_transport_seL4_read(uxrCustomTransport *, uint8_t *buf, size_t len, int timeout, uint8_t *errcode);

void send_impl(std_msgs__msg__Header *hdr, rcl_publisher_t *publisher);
void send_ping() { send_impl(&outgoingPing, &pingPublisher); microkit_dbg_puts("sent_ping\n"); }
void send_pong() { send_impl(&outgoingPong, &pongPublisher); microkit_dbg_puts("sent_pong\n"); }

void heap_init(void);
void init(void) { heap_init(); }

void transport_init();
void notified(microkit_channel ch)
{
    switch (ch) {
    case CHAN_READY: {
        transport_init();
        microkit_dbg_puts("ping_pong: Guest is ready, initialising and sending first ping\n");
        send_ping();
        microkit_dbg_puts("sent first ping\n");
        break;
    }
    case CHAN_PINGPONG: {
        if(spsc_empty(spsc_vmm2pp))
        {
            microkit_dbg_puts("notified but queue is empty\n");
            break;    
        }
        rclc_executor_spin_some(&executor, 0);
        break;
    }
    default:
        microkit_dbg_puts("ping_pong: Unknown channel\n");
        break;
    }
}

#define RCCHECK(fn) do { \
    if (fn != RCL_RET_OK) exit(1); \
} while(0)

void transport_init()
{
    spsc_pp2vmm = (spsc_queue_t *)pp2vmm;
    spsc_init(spsc_pp2vmm, pp2vmm + sizeof(spsc_queue_t), pp2vmm + pp2vmm_size, 11);
    spsc_vmm2pp = (spsc_queue_t *)vmm2pp;
    spsc_init(spsc_vmm2pp, vmm2pp + sizeof(spsc_queue_t), vmm2pp + vmm2pp_size, 11);

    txEthHdr = make_ethhdr("02:00:00:00:00:02", "02:00:00:00:00:01");
    txIpHdr = make_iphdr("10.0.2.100", "10.0.2.15");
    txUdpHdr = make_udphdr(54321, 8888);

    std_msgs__msg__Header__init(&incomingPing);
    std_msgs__msg__Header__init(&incomingPong);
    std_msgs__msg__Header__init(&outgoingPing);
    std_msgs__msg__Header__init(&outgoingPong);

    rmw_ret_t rmw_ret = rmw_uros_set_custom_transport(
        false,
        NULL,
        custom_transport_seL4_open,
        custom_transport_seL4_close,
        custom_transport_seL4_write,
        custom_transport_seL4_read);

    if (rmw_ret != RMW_RET_OK) {
        microkit_dbg_puts("ERROR: rmw_uros_set_custom_transport failed\n");
        return;
    }

    allocator = rcl_get_default_allocator();

    RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
    RCCHECK(rclc_node_init_default(&node, "pingpong_node", "", &support));

    RCCHECK(rclc_publisher_init_best_effort(&pingPublisher, &node,
              ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Header), "/microROS/ping"));
    RCCHECK(rclc_publisher_init_best_effort(&pongPublisher, &node,
              ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Header), "/microROS/pong"));

    RCCHECK(rclc_subscription_init_best_effort(&pingSubscriber, &node,
              ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Header), "/microROS/ping"));
    RCCHECK(rclc_subscription_init_best_effort(&pongSubscriber, &node,
              ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Header), "/microROS/pong"));

    executor = rclc_executor_get_zero_initialized_executor();
    RCCHECK(rclc_executor_init(&executor, &support.context, 3, &allocator));
    RCCHECK(rclc_executor_add_subscription(&executor, &pingSubscriber, &incomingPing,
                                           send_pong, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_subscription(&executor, &pongSubscriber, &incomingPong,
                                           send_ping, ON_NEW_DATA));

    microkit_dbg_puts("ping_pong: micro-ROS client initialized.\n");
}

size_t custom_transport_seL4_write(uxrCustomTransport *, const uint8_t *buf, size_t len, uint8_t *)
{
    if(len > 1500) 
    {
        microkit_dbg_puts("len > 1500\n");
        return 0;
    }
    char *txBuf = spsc_new_block(spsc_pp2vmm);
    make_pkt(txBuf, 2048, (const char *)buf, len, &txEthHdr, &txIpHdr, &txUdpHdr);
    spsc_push(spsc_pp2vmm);
    microkit_notify(CHAN_PINGPONG);
    return len;
}

static bool is_addressed_to_me(const char *pkt)
{
    const ethhdr *ethHdr = (const ethhdr *)pkt;
    const iphdr *ipHdr = (const iphdr *)(ethHdr + 1);
    const udphdr *udpHdr = (const udphdr *)(ipHdr + 1);

    if (memcmp(ethHdr->h_dest, txEthHdr.h_source, 6) != 0)
        return false;

    if(ethHdr->h_proto != htons(ETH_P_IP))
        return false;

    if(ipHdr->daddr != txIpHdr.saddr)
        return false;

    if(ipHdr->protocol != IPPROTO_UDP)
        return false;

    if(udpHdr->uh_dport != txUdpHdr.uh_sport)
        return false;

    return true;
}

static char dbgBuf[2048] = {0};
size_t custom_transport_seL4_read(uxrCustomTransport *, uint8_t *buf, size_t len, int, uint8_t *)
{
    char *rxPkt;
    while(rxPkt = spsc_front_block(spsc_vmm2pp))
    {
        if(is_addressed_to_me(rxPkt))
            break;
        spsc_pop(spsc_vmm2pp);
    }
    char *payload = get_payload(rxPkt);
    size_t payloadLen = get_payload_len(rxPkt);

    if(len < payloadLen)
    {
        spsc_pop(spsc_vmm2pp);
        return 0;
    }
    memcpy(buf, payload, payloadLen);
    spsc_pop(spsc_vmm2pp);
    return payloadLen;
}
void send_impl(std_msgs__msg__Header *hdr, rcl_publisher_t *publisher)
{
    hdr->frame_id.size = snprintf(hdr->frame_id.data, hdr->frame_id.capacity, "%d_%d", seqNum, DEVICE_ID);
    hdr->stamp.sec = 0;
    hdr->stamp.nanosec = 0;
    RCCHECK(rcl_publish(publisher, (const void *)hdr, NULL));
    seqNum++;
}
