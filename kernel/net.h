#ifndef NET_H
#define NET_H

#include "types.h"

#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

#define ARP_HTYPE_ETH   1
#define ARP_PTYPE_IPV4  0x0800
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

#pragma pack(push, 1)
typedef struct {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t type;
} eth_header_t;

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint8_t  spa[4];
    uint8_t  tha[6];
    uint8_t  tpa[4];
} arp_packet_t;

typedef struct {
    uint8_t  ver_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
} ip_header_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} icmp_header_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;
#pragma pack(pop)

#define DNS_TYPE_A      1
#define DNS_CLASS_IN    1

extern uint8_t net_our_mac[6];
extern uint8_t net_our_ip[4];
extern uint8_t net_gateway_ip[4];
extern uint8_t net_dns_ip[4];

void net_init(void);
int net_arp_resolve(const uint8_t* ip, uint8_t* mac);
int net_send_ip(const uint8_t* dst_ip, uint8_t protocol, const void* data, int len);
int net_recv_ip(uint8_t* src_ip, uint8_t* protocol, void* buffer, int max_len);
int net_dns_query(const char* hostname, uint8_t* ip_out);

#endif