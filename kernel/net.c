#include "net.h"
#include "rtl8139.h"
#include "string.h"
#include "screen.h"
#include "timer.h"

uint8_t net_our_mac[6] = {0};
uint8_t net_our_ip[4] = {10, 0, 2, 15};
uint8_t net_gateway_ip[4] = {10, 0, 2, 2};
uint8_t net_dns_ip[4] = {10, 0, 2, 3};

#define ARP_CACHE_SIZE 8
static struct {
    uint8_t ip[4];
    uint8_t mac[6];
    int valid;
} arp_cache[ARP_CACHE_SIZE];

static void arp_cache_add(const uint8_t* ip, const uint8_t* mac);

static uint16_t net_checksum(const void* data, int len)
{
    uint32_t sum = 0;
    const uint16_t* p = (const uint16_t*)data;
    for (int i = 0; i < len / 2; i++)
        sum += p[i];
    if (len & 1)
        sum += ((const uint8_t*)data)[len - 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~(uint16_t)sum;
}

static void mac_to_str(const uint8_t* mac, char* out)
{
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++)
    {
        out[i * 3] = hex[mac[i] >> 4];
        out[i * 3 + 1] = hex[mac[i] & 0xF];
        out[i * 3 + 2] = ':';
    }
    out[17] = 0;
}

static void ip_to_str(const uint8_t* ip, char* out)
{
    for (int i = 0; i < 4; i++)
    {
        int v = ip[i];
        if (v >= 100) { *out++ = '0' + v / 100; v %= 100; }
        if (v >= 10) *out++ = '0' + v / 10;
        *out++ = '0' + v % 10;
        if (i < 3) *out++ = '.';
    }
    *out = 0;
}

static void net_handle_arp(const uint8_t* data, int len)
{
    if (len < (int)sizeof(arp_packet_t)) return;
    const arp_packet_t* arp = (const arp_packet_t*)data;

    if (arp->htype != 0x0100 || arp->ptype != 0x0008) return;
    if (arp->hlen != 6 || arp->plen != 4) return;

    if (memcmp(arp->tpa, net_our_ip, 4) != 0) return;

    arp_cache_add(arp->spa, arp->sha);

    if (arp->oper == 0x0100)
    {
        uint8_t reply[sizeof(eth_header_t) + sizeof(arp_packet_t)];
        eth_header_t* e = (eth_header_t*)reply;
        arp_packet_t* a = (arp_packet_t*)(reply + sizeof(eth_header_t));

        memcpy(e->dst_mac, arp->sha, 6);
        memcpy(e->src_mac, net_our_mac, 6);
        e->type = 0x0608;

        a->htype = 0x0100;
        a->ptype = 0x0008;
        a->hlen = 6;
        a->plen = 4;
        a->oper = 0x0200;
        memcpy(a->sha, net_our_mac, 6);
        memcpy(a->spa, net_our_ip, 4);
        memcpy(a->tha, arp->sha, 6);
        memcpy(a->tpa, arp->spa, 4);

        rtl8139_send(reply, sizeof(reply));
    }
}

static void net_handle_icmp(const uint8_t* src_ip, const uint8_t* data, int len)
{
    if (len < (int)sizeof(icmp_header_t)) return;
    const icmp_header_t* icmp = (const icmp_header_t*)data;

    if (icmp->type == 8)
    {
        screen_write("ICMP: ping from ");
        char buf[16];
        ip_to_str(src_ip, buf);
        screen_write(buf);
        screen_write("\n");

        uint8_t reply[512];
        icmp_header_t* ricmp = (icmp_header_t*)(reply + sizeof(eth_header_t) + sizeof(ip_header_t));
        ricmp->type = 0;
        ricmp->code = 0;
        ricmp->id = icmp->id;
        ricmp->sequence = icmp->sequence;
        int data_len = len - sizeof(icmp_header_t);
        if (data_len > 0)
            memcpy(ricmp + 1, icmp + 1, data_len < 256 ? data_len : 256);
        int total = sizeof(icmp_header_t) + data_len;
        ricmp->checksum = 0;
        ricmp->checksum = net_checksum(ricmp, total);

        net_send_ip(src_ip, IP_PROTO_ICMP, ricmp, total);
    }
}

static void net_handle_udp(const uint8_t* src_ip, const uint8_t* data, int len)
{
    if (len < (int)sizeof(udp_header_t)) return;
    const udp_header_t* udp = (const udp_header_t*)data;
    int udp_len = (udp->length >> 8) | ((udp->length & 0xFF) << 8);
    if ((unsigned int)udp_len < sizeof(udp_header_t)) return;
    int payload_len = udp_len - sizeof(udp_header_t);
    uint16_t dst_port = (udp->dst_port >> 8) | ((udp->dst_port & 0xFF) << 8);
    uint16_t src_port = (udp->src_port >> 8) | ((udp->src_port & 0xFF) << 8);

    if (dst_port == 53)
    {
        const dns_header_t* dns = (const dns_header_t*)(data + sizeof(udp_header_t));
        uint16_t ancount = (dns->ancount >> 8) | ((dns->ancount & 0xFF) << 8);
        if (ancount > 0 && payload_len >= (int)sizeof(dns_header_t))
        {
            screen_write("DNS: response received\n");
        }
    }
    else
    {
        char buf[16];
        ip_to_str(src_ip, buf);
        screen_write("UDP: ");
        screen_write(buf);
        screen_write(" port ");
        screen_write_dec(src_port);
        screen_write(" -> ");
        screen_write_dec(dst_port);
        screen_write(" len=");
        screen_write_dec(payload_len);
        screen_write("\n");
    }
}

static void net_process_packet(const uint8_t* data, int len)
{
    if (len < (int)sizeof(eth_header_t)) return;
    const eth_header_t* eth = (const eth_header_t*)data;
    uint16_t type = (eth->type >> 8) | ((eth->type & 0xFF) << 8);

    if (memcmp(eth->dst_mac, net_our_mac, 6) != 0 &&
        !(eth->dst_mac[0] == 0xFF && eth->dst_mac[1] == 0xFF &&
          eth->dst_mac[2] == 0xFF && eth->dst_mac[3] == 0xFF &&
          eth->dst_mac[4] == 0xFF && eth->dst_mac[5] == 0xFF))
        return;

    const uint8_t* payload = data + sizeof(eth_header_t);
    int payload_len = len - sizeof(eth_header_t);

    if (type == ETH_TYPE_ARP)
    {
        net_handle_arp(payload, payload_len);
    }
    else if (type == ETH_TYPE_IPV4 && payload_len >= (int)sizeof(ip_header_t))
    {
        const ip_header_t* ip = (const ip_header_t*)payload;
        uint8_t src_ip[4];
        memcpy(src_ip, ip->src_ip, 4);

        int ihl = (ip->ver_ihl & 0x0F) * 4;
        if (ihl < 20 || ihl > payload_len) return;

        uint8_t proto = ip->protocol;
        const uint8_t* proto_data = payload + ihl;
        int proto_len = payload_len - ihl;

        if (proto == IP_PROTO_ICMP)
            net_handle_icmp(src_ip, proto_data, proto_len);
        else if (proto == IP_PROTO_UDP)
            net_handle_udp(src_ip, proto_data, proto_len);
    }
}

static int arp_cache_lookup(const uint8_t* ip, uint8_t* mac)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0)
        {
            memcpy(mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

static void arp_cache_add(const uint8_t* ip, const uint8_t* mac)
{
    int oldest = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!arp_cache[i].valid) { oldest = i; break; }
        if (i > oldest) oldest = i;
    }
    memcpy(arp_cache[oldest].ip, ip, 4);
    memcpy(arp_cache[oldest].mac, mac, 6);
    arp_cache[oldest].valid = 1;
}

int net_arp_resolve(const uint8_t* ip, uint8_t* mac)
{
    if (arp_cache_lookup(ip, mac)) return 1;

    if (!rtl8139_dev.iobase) return 0;

    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    uint8_t packet[sizeof(eth_header_t) + sizeof(arp_packet_t)];
    eth_header_t* e = (eth_header_t*)packet;
    arp_packet_t* a = (arp_packet_t*)(packet + sizeof(eth_header_t));

    memcpy(e->dst_mac, broadcast, 6);
    memcpy(e->src_mac, net_our_mac, 6);
    e->type = 0x0608;

    a->htype = 0x0100;
    a->ptype = 0x0008;
    a->hlen = 6;
    a->plen = 4;
    a->oper = 0x0100;
    memcpy(a->sha, net_our_mac, 6);
    memcpy(a->spa, net_our_ip, 4);
    memset(a->tha, 0, 6);
    memcpy(a->tpa, ip, 4);

    rtl8139_send(packet, sizeof(packet));

    uint64_t timeout = timer_get_ticks() + 50;
    while (timer_get_ticks() < timeout)
    {
        uint8_t buf[1522];
        int rlen = rtl8139_recv(buf, sizeof(buf));
        if (rlen > 0)
        {
            net_process_packet(buf, rlen);
            if (arp_cache_lookup(ip, mac)) return 1;
        }
    }
    return 0;
}

int net_send_ip(const uint8_t* dst_ip, uint8_t protocol, const void* data, int len)
{
    uint8_t dst_mac[6];
    if (!net_arp_resolve(dst_ip, dst_mac)) return -1;

    int total = sizeof(eth_header_t) + sizeof(ip_header_t) + len;
    if (total > 1518) return -1;

    uint8_t packet[1518];
    eth_header_t* e = (eth_header_t*)packet;
    ip_header_t* ip = (ip_header_t*)(packet + sizeof(eth_header_t));

    memcpy(e->dst_mac, dst_mac, 6);
    memcpy(e->src_mac, net_our_mac, 6);
    e->type = 0x0008;

    int ip_total = sizeof(ip_header_t) + len;
    ip->ver_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = ((ip_total >> 8) & 0xFF) | ((ip_total & 0xFF) << 8);
    ip->id = 0x0000;
    ip->flags_frag = 0x0000;
    ip->ttl = 64;
    ip->protocol = protocol;
    memset(ip->src_ip, 0, 4);
    memcpy(ip->src_ip, net_our_ip, 4);
    memcpy(ip->dst_ip, dst_ip, 4);
    ip->checksum = 0;
    ip->checksum = net_checksum(ip, sizeof(ip_header_t));

    memcpy(packet + sizeof(eth_header_t) + sizeof(ip_header_t), data, len);

    return rtl8139_send(packet, total);
}

int net_recv_ip(uint8_t* src_ip, uint8_t* protocol, void* buffer, int max_len)
{
    for (int tries = 0; tries < 100; tries++)
    {
        uint8_t buf[1522];
        int rlen = rtl8139_recv(buf, sizeof(buf));
        if (rlen <= 0) return 0;

        net_process_packet(buf, rlen);

        if (rlen >= (int)(sizeof(eth_header_t) + sizeof(ip_header_t)))
        {
            const eth_header_t* e = (const eth_header_t*)buf;
            uint16_t type = (e->type >> 8) | ((e->type & 0xFF) << 8);
            if (type == ETH_TYPE_IPV4)
            {
                const ip_header_t* ip = (const ip_header_t*)(buf + sizeof(eth_header_t));
                int ihl = (ip->ver_ihl & 0x0F) * 4;
                int payload_len = ((ip->total_len >> 8) | ((ip->total_len & 0xFF) << 8)) - ihl;
                if (payload_len > 0 && payload_len <= max_len)
                {
                    memcpy(src_ip, ip->src_ip, 4);
                    *protocol = ip->protocol;
                    memcpy(buffer, buf + sizeof(eth_header_t) + ihl, payload_len);
                    return payload_len;
                }
            }
        }
    }
    return 0;
}

int net_dns_query(const char* hostname, uint8_t* ip_out)
{
    screen_write("DNS: querying ");
    screen_write(hostname);
    screen_write("\n");

    uint8_t packet[512];
    dns_header_t* dns = (dns_header_t*)packet;
    memset(dns, 0, sizeof(dns_header_t));
    dns->id = 0x0100;
    dns->flags = 0x0001;
    dns->qdcount = 0x0100;
    dns->ancount = 0;
    dns->nscount = 0;
    dns->arcount = 0;

    uint8_t* qname = (uint8_t*)(packet + sizeof(dns_header_t));
    const char* p = hostname;
    while (*p)
    {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int label_len = dot - p;
        *qname++ = (uint8_t)label_len;
        memcpy(qname, p, label_len);
        qname += label_len;
        p = dot;
        if (*dot == '.') p++;
    }
    *qname++ = 0;
    *qname++ = 0;
    *qname++ = DNS_TYPE_A;
    *qname++ = 0;
    *qname++ = DNS_CLASS_IN;

    int dns_len = qname - packet;

    uint8_t udp_data[512];
    udp_header_t* udp = (udp_header_t*)udp_data;
    int udp_len = sizeof(udp_header_t) + dns_len;
    udp->src_port = 0x3500;
    udp->dst_port = 0x3500;
    udp->length = ((udp_len >> 8) & 0xFF) | ((udp_len & 0xFF) << 8);
    udp->checksum = 0;
    memcpy(udp_data + sizeof(udp_header_t), packet, dns_len);

    net_send_ip(net_dns_ip, IP_PROTO_UDP, udp_data, udp_len);

    uint64_t timeout = timer_get_ticks() + 200;
    while (timer_get_ticks() < timeout)
    {
        uint8_t buf[1024];
        int rlen = net_recv_ip(NULL, NULL, buf, sizeof(buf));
        if (rlen >= (int)sizeof(dns_header_t))
        {
            const dns_header_t* resp = (const dns_header_t*)buf;
            uint16_t ancount = (resp->ancount >> 8) | ((resp->ancount & 0xFF) << 8);
            if (ancount > 0)
            {
                const uint8_t* rdata = buf + sizeof(dns_header_t);
                while (*rdata) rdata++;
                rdata += 5;
                if ((uint32_t)(rdata - buf) + 4 <= (uint32_t)rlen)
                {
                    memcpy(ip_out, rdata, 4);
                    char ips[16];
                    ip_to_str(ip_out, ips);
                    screen_write("DNS: resolved to ");
                    screen_write(ips);
                    screen_write("\n");
                    return 1;
                }
            }
        }
    }
    screen_write("DNS: timeout\n");
    return 0;
}

void net_init(void)
{
    if (!rtl8139_dev.iobase) return;

    memcpy(net_our_mac, rtl8139_dev.mac, 6);

    char mac_str[18];
    mac_to_str(net_our_mac, mac_str);
    screen_write("Net: MAC ");
    screen_write(mac_str);
    screen_write(" IP ");
    char ip_str[16];
    ip_to_str(net_our_ip, ip_str);
    screen_write(ip_str);
    screen_write("\n");

    memset(arp_cache, 0, sizeof(arp_cache));
}