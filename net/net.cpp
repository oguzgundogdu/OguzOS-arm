#include "net.h"
#include "netdev.h"
#include "string.h"
#include "types.h"
#include "uart.h"

namespace {

// ─── Byte order helpers (ARM64 is little-endian, network is big-endian) ───

u16 htons(u16 x) { return static_cast<u16>((x >> 8) | (x << 8)); }

u32 htonl(u32 x) {
  return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) |
         ((x >> 24) & 0xFF);
}
// ─── IP address utilities ───

constexpr u32 make_ip(u8 a, u8 b, u8 c, u8 d) {
  return (u32(a) << 24) | (u32(b) << 16) | (u32(c) << 8) | u32(d);
}

constexpr u32 LOOPBACK_IP = make_ip(127, 0, 0, 1);

u32 parse_ip(const char *s) {
  u32 ip = 0;
  u32 octet = 0;
  int parts = 0;
  while (*s) {
    if (*s >= '0' && *s <= '9') {
      octet = octet * 10 + static_cast<u32>(*s - '0');
    } else if (*s == '.') {
      ip = (ip << 8) | (octet & 0xFF);
      octet = 0;
      parts++;
    }
    s++;
  }
  ip = (ip << 8) | (octet & 0xFF);
  parts++;
  return (parts == 4) ? ip : 0;
}

void put_ip(u32 ip) {
  for (int i = 3; i >= 0; i--) {
    uart::put_int(static_cast<i64>((ip >> (i * 8)) & 0xFF));
    if (i > 0)
      uart::putc('.');
  }
}

void put_mac(const u8 *mac) {
  const char *hex = "0123456789abcdef";
  for (int i = 0; i < 6; i++) {
    if (i > 0)
      uart::putc(':');
    uart::putc(hex[mac[i] >> 4]);
    uart::putc(hex[mac[i] & 0xF]);
  }
}

// Write IP to byte array (network order)
void ip_to_bytes(u32 ip, u8 *dst) {
  volatile u8 *d = dst;
  d[0] = (ip >> 24) & 0xFF;
  d[1] = (ip >> 16) & 0xFF;
  d[2] = (ip >> 8) & 0xFF;
  d[3] = ip & 0xFF;
}

u32 ip_from_bytes(const u8 *src) {
  const volatile u8 *b = src;
  return (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | u32(b[3]);
}

// ─── Timer helpers ───

u64 get_ticks() {
  u64 cnt;
  asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
  return cnt;
}

u64 get_freq() {
  u64 freq;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  return freq ? freq : 1;
}

bool elapsed_ms(u64 start, u32 ms) {
  u64 now = get_ticks();
  u64 freq = get_freq();
  return (now - start) > (freq * ms / 1000);
}

void delay_ms(u32 ms) {
  u64 start = get_ticks();
  while (!elapsed_ms(start, ms)) {
    asm volatile("yield");
  }
}

u32 ticks_to_ms(u64 ticks) {
  return static_cast<u32>(ticks * 1000 / get_freq());
}

// ─── Protocol structures ───

struct EthHdr {
  u8 dst[6];
  u8 src[6];
  u16 ethertype;
} __attribute__((packed));

constexpr u16 ETH_ARP = 0x0806;
constexpr u16 ETH_IPV4 = 0x0800;

struct ArpPacket {
  u16 htype;
  u16 ptype;
  u8 hlen;
  u8 plen;
  u16 oper;
  u8 sha[6];
  u8 spa[4];
  u8 tha[6];
  u8 tpa[4];
} __attribute__((packed));

constexpr u16 ARP_REQUEST = 1;
constexpr u16 ARP_REPLY = 2;

struct Ipv4Hdr {
  u8 ver_ihl;
  u8 tos;
  u16 total_len;
  u16 id;
  u16 flags_frag;
  u8 ttl;
  u8 protocol;
  u16 checksum;
  u32 src_ip;
  u32 dst_ip;
} __attribute__((packed));

constexpr u8 PROTO_ICMP = 1;
constexpr u8 PROTO_TCP = 6;
constexpr u8 PROTO_UDP = 17;

struct IcmpHdr {
  u8 type;
  u8 code;
  u16 checksum;
  u16 id;
  u16 seq;
} __attribute__((packed));

constexpr u8 ICMP_ECHO_REPLY = 0;
constexpr u8 ICMP_DEST_UNREACH = 3;
constexpr u8 ICMP_ECHO_REQUEST = 8;

struct UdpHdr {
  u16 src_port;
  u16 dst_port;
  u16 length;
  u16 checksum;
} __attribute__((packed));

// ─── DNS structures ───

struct DnsHdr {
  u16 id;
  u16 flags;
  u16 qdcount;
  u16 ancount;
  u16 nscount;
  u16 arcount;
} __attribute__((packed));

constexpr u16 DNS_PORT = 53;
constexpr u16 DNS_CLIENT_PORT = 53053;
constexpr u16 DNS_FLAG_RESPONSE = 0x8000;
constexpr u16 DNS_RCODE_MASK = 0x000F;
constexpr u16 DNS_TYPE_A = 1;
constexpr u16 DNS_CLASS_IN = 1;

// ─── DHCP structures ───

struct DhcpMessage {
  u8 op;
  u8 htype;
  u8 hlen;
  u8 hops;
  u32 xid;
  u16 secs;
  u16 flags;
  u32 ciaddr;
  u32 yiaddr;
  u32 siaddr;
  u32 giaddr;
  u8 chaddr[16];
  u8 sname[64];
  u8 file[128];
  u32 magic_cookie;
  u8 options[312];
} __attribute__((packed));

constexpr u32 DHCP_MAGIC = 0x63825363;
constexpr u8 DHCP_DISCOVER = 1;
constexpr u8 DHCP_OFFER = 2;
constexpr u8 DHCP_REQUEST = 3;
constexpr u8 DHCP_ACK = 5;

// ─── TCP constants ───

constexpr u8 TCP_FIN = 0x01;
constexpr u8 TCP_SYN = 0x02;
constexpr u8 TCP_RST = 0x04;
constexpr u8 TCP_PSH = 0x08;
constexpr u8 TCP_ACK = 0x10;

enum TcpState : u8 {
  TCP_STATE_CLOSED,
  TCP_STATE_SYN_SENT,
  TCP_STATE_ESTABLISHED,
  TCP_STATE_FIN_WAIT_1,
  TCP_STATE_FIN_WAIT_2,
  TCP_STATE_CLOSING,
};

struct TcpConn {
  TcpState state;
  u32 remote_ip;
  u16 local_port;
  u16 remote_port;
  u32 snd_nxt;
  u32 rcv_nxt;
  u8 dst_mac[6];
  bool got_rst;
  bool fin_received;
};

TcpConn tcp_conn;
u16 tcp_ephemeral_port = 49152;

constexpr u32 TCP_RECV_BUF_SIZE = 16384;
u8 tcp_recv_buf[TCP_RECV_BUF_SIZE];
u32 tcp_recv_len = 0;

// ─── Network state ───

u32 local_ip = 0;
u32 gateway_ip = 0;
u32 subnet_mask = 0;
u32 dns_ip = 0;
u8 local_mac[6];
bool configured = false;

const u8 BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ARP table
struct ArpEntry {
  u32 ip;
  u8 mac[6];
  bool valid;
};
constexpr u32 ARP_TABLE_SIZE = 16;
ArpEntry arp_table[ARP_TABLE_SIZE];

// Ping state
volatile bool ping_got_reply;

// DNS state
u16 dns_query_id = 0x2000;
u16 dns_expected_query_id = 0;
volatile bool dns_response_ready;
volatile bool dns_query_unreachable;
bool dns_last_lookup_unreachable;
u32 dns_resolved_ip;

// DHCP state
u32 dhcp_xid = 0x12345678;
volatile u8 dhcp_got_msg_type;
u32 dhcp_offered_ip;
u32 dhcp_server_ip;
u32 dhcp_offered_gateway;
u32 dhcp_offered_subnet;
u32 dhcp_offered_dns;

// Packet buffers
u8 send_buf[2048];
u8 recv_buf[2048];

// IP identification counter
u16 ip_id_counter = 1;

// ─── Checksum ───

u16 inet_checksum(const void *data, u32 len) {
  const u8 *p = static_cast<const u8 *>(data);
  u32 sum = 0;
  for (u32 i = 0; i + 1 < len; i += 2) {
    sum += (u32(p[i]) << 8) | u32(p[i + 1]);
  }
  if (len & 1) {
    sum += u32(p[len - 1]) << 8;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return static_cast<u16>(~sum);
}

u16 read_be16(const void *p) {
  const volatile u8 *b = static_cast<const volatile u8 *>(p);
  return (u16(b[0]) << 8) | u16(b[1]);
}

u32 read_be32(const void *p) {
  const volatile u8 *b = static_cast<const volatile u8 *>(p);
  return (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | u32(b[3]);
}

bool dns_skip_name(const u8 *pkt, u32 len, u32 &off) {
  while (off < len) {
    u8 label = pkt[off];
    if (label == 0) {
      off++;
      return true;
    }
    if ((label & 0xC0) == 0xC0) {
      if (off + 1 >= len)
        return false;
      off += 2;
      return true;
    }
    if ((label & 0xC0) != 0)
      return false;
    off++;
    if (off + label > len)
      return false;
    off += label;
  }
  return false;
}

bool dns_encode_name(const char *name, u8 *out, u32 out_len, u32 &written) {
  written = 0;
  const char *p = name;
  while (*p) {
    if (*p == '.')
      return false;

    const char *label_start = p;
    u32 label_len = 0;
    while (*p && *p != '.') {
      label_len++;
      p++;
    }

    if (label_len == 0 || label_len > 63)
      return false;
    if (written + 1 + label_len + 1 > out_len)
      return false;

    out[written++] = static_cast<u8>(label_len);
    for (u32 i = 0; i < label_len; i++) {
      out[written++] = static_cast<u8>(label_start[i]);
    }

    if (*p == '.')
      p++;
  }

  if (written + 1 > out_len)
    return false;
  out[written++] = 0;
  return true;
}

// ─── ARP ───

void arp_table_set(u32 ip, const u8 *mac) {
  // Update existing
  for (u32 i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_table[i].valid && arp_table[i].ip == ip) {
      str::memcpy(arp_table[i].mac, mac, 6);
      return;
    }
  }
  // Find empty slot
  for (u32 i = 0; i < ARP_TABLE_SIZE; i++) {
    if (!arp_table[i].valid) {
      arp_table[i].ip = ip;
      str::memcpy(arp_table[i].mac, mac, 6);
      arp_table[i].valid = true;
      return;
    }
  }
  // Table full: overwrite first entry
  arp_table[0].ip = ip;
  str::memcpy(arp_table[0].mac, mac, 6);
}

bool arp_table_lookup(u32 ip, u8 *mac_out) {
  for (u32 i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_table[i].valid && arp_table[i].ip == ip) {
      str::memcpy(mac_out, arp_table[i].mac, 6);
      return true;
    }
  }
  return false;
}

// ─── Ethernet send ───

void eth_send(const u8 *dst_mac, u16 ethertype, const void *payload,
              u32 payload_len) {
  EthHdr *eth = reinterpret_cast<EthHdr *>(send_buf);
  str::memcpy(eth->dst, dst_mac, 6);
  str::memcpy(eth->src, local_mac, 6);
  eth->ethertype = htons(ethertype);
  str::memcpy(send_buf + sizeof(EthHdr), payload, payload_len);
  netdev::send(send_buf, sizeof(EthHdr) + payload_len);
}

// ─── ARP send ───

void arp_send_request(u32 target_ip) {
  ArpPacket arp;
  str::memset(&arp, 0, sizeof(arp));
  arp.htype = htons(1);
  arp.ptype = htons(0x0800);
  arp.hlen = 6;
  arp.plen = 4;
  arp.oper = htons(ARP_REQUEST);
  str::memcpy(arp.sha, local_mac, 6);
  ip_to_bytes(local_ip, arp.spa);
  str::memset(arp.tha, 0, 6);
  ip_to_bytes(target_ip, arp.tpa);
  eth_send(BROADCAST_MAC, ETH_ARP, &arp, sizeof(arp));
}

void arp_send_reply(const u8 *dst_mac, u32 dst_ip) {
  ArpPacket arp;
  str::memset(&arp, 0, sizeof(arp));
  arp.htype = htons(1);
  arp.ptype = htons(0x0800);
  arp.hlen = 6;
  arp.plen = 4;
  arp.oper = htons(ARP_REPLY);
  str::memcpy(arp.sha, local_mac, 6);
  ip_to_bytes(local_ip, arp.spa);
  str::memcpy(arp.tha, dst_mac, 6);
  ip_to_bytes(dst_ip, arp.tpa);
  eth_send(dst_mac, ETH_ARP, &arp, sizeof(arp));
}

// ─── IPv4 send ───

void ipv4_send(u32 dst_ip, u8 protocol, const void *payload,
               u32 payload_len, const u8 *dst_mac) {
  u8 ip_pkt[1500];
  Ipv4Hdr *ip = reinterpret_cast<Ipv4Hdr *>(ip_pkt);
  str::memset(ip, 0, sizeof(Ipv4Hdr));
  ip->ver_ihl = 0x45;
  ip->ttl = 64;
  ip->protocol = protocol;
  ip->total_len = htons(static_cast<u16>(sizeof(Ipv4Hdr) + payload_len));
  ip->id = htons(ip_id_counter++);
  ip->src_ip = htonl(local_ip);
  ip->dst_ip = htonl(dst_ip);
  ip->checksum = 0;
  ip->checksum = htons(inet_checksum(ip, sizeof(Ipv4Hdr)));

  str::memcpy(ip_pkt + sizeof(Ipv4Hdr), payload, payload_len);
  eth_send(dst_mac, ETH_IPV4, ip_pkt, sizeof(Ipv4Hdr) + payload_len);
}


// ─── TCP helpers ───

u16 tcp_checksum(u32 src_ip_host, u32 dst_ip_host, const u8 *tcp_pkt,
                 u32 tcp_len) {
  u32 sum = 0;
  // Pseudo-header: src IP, dst IP, zero+proto, TCP length (all big-endian)
  sum += (src_ip_host >> 16) & 0xFFFF;
  sum += src_ip_host & 0xFFFF;
  sum += (dst_ip_host >> 16) & 0xFFFF;
  sum += dst_ip_host & 0xFFFF;
  sum += PROTO_TCP;
  sum += tcp_len;
  // TCP segment
  for (u32 i = 0; i + 1 < tcp_len; i += 2) {
    sum += (u32(tcp_pkt[i]) << 8) | u32(tcp_pkt[i + 1]);
  }
  if (tcp_len & 1) {
    sum += u32(tcp_pkt[tcp_len - 1]) << 8;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return static_cast<u16>(~sum);
}

void tcp_send_segment(u8 flags, const void *data, u32 data_len) {
  u8 tcp_pkt[1500];
  u32 hdr_len = 20;

  // Add MSS option for SYN
  if (flags & TCP_SYN) {
    hdr_len = 24;
  }

  str::memset(tcp_pkt, 0, hdr_len);

  // Source port
  tcp_pkt[0] = static_cast<u8>((tcp_conn.local_port >> 8) & 0xFF);
  tcp_pkt[1] = static_cast<u8>(tcp_conn.local_port & 0xFF);
  // Dest port
  tcp_pkt[2] = static_cast<u8>((tcp_conn.remote_port >> 8) & 0xFF);
  tcp_pkt[3] = static_cast<u8>(tcp_conn.remote_port & 0xFF);
  // Sequence number (big-endian)
  tcp_pkt[4] = static_cast<u8>((tcp_conn.snd_nxt >> 24) & 0xFF);
  tcp_pkt[5] = static_cast<u8>((tcp_conn.snd_nxt >> 16) & 0xFF);
  tcp_pkt[6] = static_cast<u8>((tcp_conn.snd_nxt >> 8) & 0xFF);
  tcp_pkt[7] = static_cast<u8>(tcp_conn.snd_nxt & 0xFF);
  // Ack number (big-endian)
  tcp_pkt[8] = static_cast<u8>((tcp_conn.rcv_nxt >> 24) & 0xFF);
  tcp_pkt[9] = static_cast<u8>((tcp_conn.rcv_nxt >> 16) & 0xFF);
  tcp_pkt[10] = static_cast<u8>((tcp_conn.rcv_nxt >> 8) & 0xFF);
  tcp_pkt[11] = static_cast<u8>(tcp_conn.rcv_nxt & 0xFF);
  // Data offset
  tcp_pkt[12] = static_cast<u8>((hdr_len / 4) << 4);
  // Flags
  tcp_pkt[13] = flags;
  // Window size (4096)
  tcp_pkt[14] = 0x10;
  tcp_pkt[15] = 0x00;

  // MSS option for SYN: kind=2, len=4, MSS=1460
  if (flags & TCP_SYN) {
    tcp_pkt[20] = 2;
    tcp_pkt[21] = 4;
    tcp_pkt[22] = static_cast<u8>((1460 >> 8) & 0xFF);
    tcp_pkt[23] = static_cast<u8>(1460 & 0xFF);
  }

  if (data && data_len > 0) {
    str::memcpy(tcp_pkt + hdr_len, data, data_len);
  }

  u32 total_len = hdr_len + data_len;

  // Compute checksum (field at offset 16-17 is already zero)
  u16 csum = tcp_checksum(local_ip, tcp_conn.remote_ip, tcp_pkt, total_len);
  tcp_pkt[16] = static_cast<u8>((csum >> 8) & 0xFF);
  tcp_pkt[17] = static_cast<u8>(csum & 0xFF);

  ipv4_send(tcp_conn.remote_ip, PROTO_TCP, tcp_pkt, total_len,
            tcp_conn.dst_mac);

  // Update snd_nxt: SYN and FIN consume one sequence number each
  if (flags & TCP_SYN)
    tcp_conn.snd_nxt++;
  if (flags & TCP_FIN)
    tcp_conn.snd_nxt++;
  tcp_conn.snd_nxt += data_len;
}

// ─── Packet handlers ───

void handle_arp(const u8 *pkt, u32 len) {
  if (len < sizeof(ArpPacket))
    return;

  u16 oper = read_be16(pkt + 6);
  const u8 *sender_mac = pkt + 8;
  u32 sender_ip = ip_from_bytes(pkt + 14);
  u32 target_ip = ip_from_bytes(pkt + 24);

  // Cache sender's MAC
  arp_table_set(sender_ip, sender_mac);

  if (oper == ARP_REQUEST && target_ip == local_ip) {
    arp_send_reply(sender_mac, sender_ip);
  }
}

void handle_icmp(u32 src_ip, const u8 *pkt, u32 len) {
  if (len < sizeof(IcmpHdr))
    return;

  u8 type = pkt[0];

  if (type == ICMP_ECHO_REPLY) {
    ping_got_reply = true;
  } else if (type == ICMP_DEST_UNREACH) {
    // ICMP error payload includes original IP header + first 8 bytes.
    if (len < 8 + sizeof(Ipv4Hdr) + sizeof(UdpHdr))
      return;
    const u8 *inner_ip = pkt + 8;
    u8 ihl = (inner_ip[0] & 0x0F) * 4;
    if (ihl < 20 || len < 8 + ihl + sizeof(UdpHdr))
      return;
    if (inner_ip[9] != PROTO_UDP)
      return;
    const u8 *inner_udp = inner_ip + ihl;
    u16 inner_src_port = read_be16(inner_udp + 0);
    u16 inner_dst_port = read_be16(inner_udp + 2);
    if (inner_src_port == DNS_CLIENT_PORT && inner_dst_port == DNS_PORT) {
      (void)src_ip;
      dns_query_unreachable = true;
    }
  } else if (type == ICMP_ECHO_REQUEST && local_ip != 0) {
    // Respond to ping
    u8 reply_buf[1500];
    u32 reply_len = len;
    if (reply_len > sizeof(reply_buf))
      reply_len = sizeof(reply_buf);
    str::memcpy(reply_buf, pkt, reply_len);
    reply_buf[0] = ICMP_ECHO_REPLY;
    reply_buf[2] = 0;
    reply_buf[3] = 0;
    u16 csum = inet_checksum(reply_buf, reply_len);
    reply_buf[2] = static_cast<u8>((csum >> 8) & 0xFF);
    reply_buf[3] = static_cast<u8>(csum & 0xFF);

    u8 dst_mac[6];
    if (arp_table_lookup(src_ip, dst_mac)) {
      ipv4_send(src_ip, PROTO_ICMP, reply_buf, reply_len, dst_mac);
    }
  }
}

void handle_dhcp(const u8 *data, u32 len) {
  if (len < 240)
    return; // Minimum DHCP message size

  // Parse only with byte reads to avoid unaligned access on ARM.
  if (read_be32(data + 236) != DHCP_MAGIC)
    return;
  if (read_be32(data + 4) != dhcp_xid)
    return;

  // Parse options
  u8 msg_type = 0;
  u32 server_id = 0;
  u32 subnet = 0;
  u32 router = 0;
  u32 dns = 0;

  const u8 *opt = data + 240;
  u32 opt_len = len - 240;
  u32 i = 0;

  while (i < opt_len) {
    u8 code = opt[i++];
    if (code == 255)
      break; // End
    if (code == 0)
      continue; // Padding

    if (i >= opt_len)
      break;
    u8 olen = opt[i++];
    if (i + olen > opt_len)
      break;

    if (code == 53 && olen >= 1)
      msg_type = opt[i];
    else if (code == 54 && olen >= 4)
      server_id = ip_from_bytes(&opt[i]);
    else if (code == 1 && olen >= 4)
      subnet = ip_from_bytes(&opt[i]);
    else if (code == 3 && olen >= 4)
      router = ip_from_bytes(&opt[i]);
    else if (code == 6 && olen >= 4)
      dns = ip_from_bytes(&opt[i]);

    i += olen;
  }

  u32 yiaddr = read_be32(data + 16);

  if (msg_type == DHCP_OFFER) {
    dhcp_offered_ip = yiaddr;
    dhcp_server_ip = server_id;
    dhcp_offered_subnet = subnet;
    dhcp_offered_gateway = router;
    dhcp_offered_dns = dns;
    dhcp_got_msg_type = DHCP_OFFER;
  } else if (msg_type == DHCP_ACK) {
    dhcp_offered_ip = yiaddr;
    if (subnet)
      dhcp_offered_subnet = subnet;
    if (router)
      dhcp_offered_gateway = router;
    if (dns)
      dhcp_offered_dns = dns;
    dhcp_got_msg_type = DHCP_ACK;
  }
}

void handle_dns(const u8 *data, u32 len) {
  if (len < sizeof(DnsHdr))
    return;

  u16 id = read_be16(&data[0]);
  if (id != dns_expected_query_id)
    return;

  u16 flags = read_be16(&data[2]);
  if ((flags & DNS_FLAG_RESPONSE) == 0)
    return;

  dns_response_ready = true;
  dns_resolved_ip = 0;

  if ((flags & DNS_RCODE_MASK) != 0)
    return;

  u16 qdcount = read_be16(&data[4]);
  u16 ancount = read_be16(&data[6]);
  u32 off = sizeof(DnsHdr);

  for (u16 i = 0; i < qdcount; i++) {
    if (!dns_skip_name(data, len, off))
      return;
    if (off + 4 > len)
      return;
    off += 4; // qtype + qclass
  }

  for (u16 i = 0; i < ancount; i++) {
    if (!dns_skip_name(data, len, off))
      return;
    if (off + 10 > len)
      return;

    u16 type = read_be16(&data[off]);
    off += 2;
    u16 cls = read_be16(&data[off]);
    off += 2;
    off += 4; // ttl
    u16 rdlen = read_be16(&data[off]);
    off += 2;

    if (off + rdlen > len)
      return;

    if (type == DNS_TYPE_A && cls == DNS_CLASS_IN && rdlen == 4) {
      dns_resolved_ip = ip_from_bytes(&data[off]);
      return;
    }

    off += rdlen;
  }
}

void handle_tcp(u32 src_ip, const u8 *pkt, u32 len) {
  if (len < 20)
    return;
  if (tcp_conn.state == TCP_STATE_CLOSED)
    return;
  if (src_ip != tcp_conn.remote_ip)
    return;

  u16 src_port = read_be16(pkt);
  u16 dst_port = read_be16(pkt + 2);
  if (src_port != tcp_conn.remote_port || dst_port != tcp_conn.local_port)
    return;

  u32 seq = read_be32(pkt + 4);
  u32 ack = read_be32(pkt + 8);
  u8 data_off = static_cast<u8>(((pkt[12] >> 4) & 0x0F) * 4);
  u8 flags = pkt[13];

  if (flags & TCP_RST) {
    tcp_conn.got_rst = true;
    tcp_conn.state = TCP_STATE_CLOSED;
    return;
  }

  switch (tcp_conn.state) {
  case TCP_STATE_SYN_SENT:
    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
      tcp_conn.rcv_nxt = seq + 1;
      tcp_conn.snd_nxt = ack; // Server acknowledged our SYN
      tcp_conn.state = TCP_STATE_ESTABLISHED;
      tcp_send_segment(TCP_ACK, nullptr, 0);
    }
    break;

  case TCP_STATE_ESTABLISHED: {
    if (flags & TCP_ACK) {
      tcp_conn.snd_nxt = (ack > tcp_conn.snd_nxt) ? ack : tcp_conn.snd_nxt;
    }

    u32 payload_len = (data_off < len) ? (len - data_off) : 0;
    if (payload_len > 0 && seq == tcp_conn.rcv_nxt) {
      u32 space = TCP_RECV_BUF_SIZE - tcp_recv_len;
      u32 copy_len = (payload_len < space) ? payload_len : space;
      if (copy_len > 0) {
        str::memcpy(tcp_recv_buf + tcp_recv_len, pkt + data_off, copy_len);
        tcp_recv_len += copy_len;
      }
      tcp_conn.rcv_nxt += payload_len;
      tcp_send_segment(TCP_ACK, nullptr, 0);
    }

    if (flags & TCP_FIN) {
      tcp_conn.rcv_nxt++;
      tcp_conn.fin_received = true;
      tcp_send_segment(TCP_ACK, nullptr, 0);
      tcp_conn.state = TCP_STATE_CLOSING;
    }
    break;
  }

  case TCP_STATE_FIN_WAIT_1:
    if (flags & TCP_ACK) {
      if (flags & TCP_FIN) {
        tcp_conn.rcv_nxt = seq + 1;
        tcp_send_segment(TCP_ACK, nullptr, 0);
        tcp_conn.state = TCP_STATE_CLOSED;
      } else {
        tcp_conn.state = TCP_STATE_FIN_WAIT_2;
      }
    }
    break;

  case TCP_STATE_FIN_WAIT_2:
    if (flags & TCP_FIN) {
      tcp_conn.rcv_nxt = seq + 1;
      tcp_send_segment(TCP_ACK, nullptr, 0);
      tcp_conn.state = TCP_STATE_CLOSED;
    }
    break;

  default:
    break;
  }
}

void handle_udp(u32 src_ip, const u8 *pkt, u32 len) {
  if (len < sizeof(UdpHdr))
    return;

  u16 dst_port = read_be16(pkt + 2);
  u32 data_len = read_be16(pkt + 4);
  if (data_len < sizeof(UdpHdr) || data_len > len)
    return;

  const u8 *data = pkt + sizeof(UdpHdr);
  u32 payload_len = data_len - sizeof(UdpHdr);

  if (dst_port == 68) {
    handle_dhcp(data, payload_len);
  } else if (dst_port == DNS_CLIENT_PORT) {
    // Some virtual NAT/DNS implementations reply from an address different
    // from the configured DNS server. Query-ID validation in handle_dns()
    // protects us from accepting unrelated packets.
    (void)src_ip;
    handle_dns(data, payload_len);
  }
}

void handle_ipv4(const u8 *pkt, u32 len) {
  if (len < sizeof(Ipv4Hdr))
    return;

  u8 ihl = (pkt[0] & 0x0F) * 4;
  u32 total = read_be16(pkt + 2);
  if (ihl < 20 || total < ihl || total > len)
    return;

  u32 src_ip = ip_from_bytes(pkt + 12);
  u8 protocol = pkt[9];
  const u8 *payload = pkt + ihl;
  u32 payload_len = total - ihl;

  if (protocol == PROTO_ICMP) {
    handle_icmp(src_ip, payload, payload_len);
  } else if (protocol == PROTO_TCP) {
    handle_tcp(src_ip, payload, payload_len);
  } else if (protocol == PROTO_UDP) {
    handle_udp(src_ip, payload, payload_len);
  }
}

void process_packet(const u8 *pkt, u32 len) {
  if (len < sizeof(EthHdr))
    return;

  u16 ethertype = read_be16(pkt + 12);
  const u8 *payload = pkt + sizeof(EthHdr);
  u32 payload_len = len - sizeof(EthHdr);

  if (ethertype == ETH_ARP) {
    handle_arp(payload, payload_len);
  } else if (ethertype == ETH_IPV4) {
    handle_ipv4(payload, payload_len);
  }
}

// Poll and process available packets (limited to prevent infinite loops)
void poll_packets() {
  i32 len;
  u32 count = 0;
  while (count < 32) {
    len = netdev::recv(recv_buf, sizeof(recv_buf));
    if (len <= 0)
      break;
    process_packet(recv_buf, static_cast<u32>(len));
    count++;
  }
}

// Poll at most one packet. Useful in tight wait loops to keep latency bounded.
void poll_one_packet() {
  i32 len = netdev::recv(recv_buf, sizeof(recv_buf));
  if (len > 0)
    process_packet(recv_buf, static_cast<u32>(len));
}

// ─── ARP resolution ───

bool arp_resolve(u32 ip, u8 *mac_out, u32 timeout) {
  // Check cache first
  if (arp_table_lookup(ip, mac_out))
    return true;

  // Send ARP request and wait
  for (u32 attempt = 0; attempt < 3; attempt++) {
    arp_send_request(ip);
    u64 wait_start = get_ticks();
    while (!elapsed_ms(wait_start, timeout)) {
      poll_one_packet();
      if (arp_table_lookup(ip, mac_out))
        return true;
      asm volatile("yield");
    }
  }
  return false;
}

// Get MAC for destination IP (handles routing through gateway)
bool resolve_dst_mac(u32 dst_ip, u8 *mac_out) {
  // Determine if destination is on local subnet
  u32 target;
  if (subnet_mask != 0 && (dst_ip & subnet_mask) == (local_ip & subnet_mask)) {
    target = dst_ip;
  } else {
    target = gateway_ip;
  }
  return arp_resolve(target, mac_out, 1000);
}

bool dns_query_server(u32 server_ip, u16 query_id, const u8 *dns_query,
                      u32 query_len, u32 *resolved_ip) {
  if (server_ip == 0)
    return false;

  u8 dst_mac[6];
  if (!resolve_dst_mac(server_ip, dst_mac))
    return false;

  u8 udp_pkt[sizeof(UdpHdr) + 512];
  UdpHdr *udp = reinterpret_cast<UdpHdr *>(udp_pkt);
  udp->src_port = htons(DNS_CLIENT_PORT);
  udp->dst_port = htons(DNS_PORT);
  udp->length = htons(static_cast<u16>(sizeof(UdpHdr) + query_len));
  udp->checksum = 0;
  str::memcpy(udp_pkt + sizeof(UdpHdr), dns_query, query_len);

  dns_expected_query_id = query_id;
  dns_response_ready = false;
  dns_query_unreachable = false;
  dns_resolved_ip = 0;

  ipv4_send(server_ip, PROTO_UDP, udp_pkt, sizeof(UdpHdr) + query_len, dst_mac);

  u64 wait_start = get_ticks();
  while (!elapsed_ms(wait_start, 1200)) {
    poll_one_packet();
    if (dns_response_ready || dns_query_unreachable)
      break;
    asm volatile("yield");
  }

  if (dns_query_unreachable)
    return false;
  if (!dns_response_ready || dns_resolved_ip == 0)
    return false;

  *resolved_ip = dns_resolved_ip;
  return true;
}

bool dns_resolve_a(const char *hostname, u32 *resolved_ip) {
  if (!configured)
    return false;
  dns_last_lookup_unreachable = false;

  u8 dns_query[512];
  str::memset(dns_query, 0, sizeof(dns_query));

  u16 query_id = ++dns_query_id;
  if (query_id == 0)
    query_id = ++dns_query_id;

  // Build DNS header with volatile byte writes to prevent widened stores.
  volatile u8 *dns_hdr = dns_query;
  dns_hdr[0] = static_cast<u8>((query_id >> 8) & 0xFF);
  dns_hdr[1] = static_cast<u8>(query_id & 0xFF);
  dns_hdr[2] = 0x01; // standard query + recursion desired
  dns_hdr[3] = 0x00;
  dns_hdr[4] = 0x00;
  dns_hdr[5] = 0x01; // QDCOUNT = 1

  u32 query_len = sizeof(DnsHdr);
  u32 encoded_len = 0;
  if (!dns_encode_name(hostname, dns_query + query_len,
                       sizeof(dns_query) - query_len, encoded_len)) {
    return false;
  }
  query_len += encoded_len;
  if (query_len + 4 > sizeof(dns_query))
    return false;

  dns_query[query_len++] = 0;
  dns_query[query_len++] = 1; // QTYPE A
  dns_query[query_len++] = 0;
  dns_query[query_len++] = 1; // QCLASS IN

  u32 candidates[4];
  u32 cand_count = 0;

  if (dns_ip != 0)
    candidates[cand_count++] = dns_ip;
  if (gateway_ip != 0 && gateway_ip != dns_ip)
    candidates[cand_count++] = gateway_ip;
  candidates[cand_count++] = make_ip(1, 1, 1, 1);
  candidates[cand_count++] = make_ip(8, 8, 8, 8);

  for (u32 i = 0; i < cand_count; i++) {
    u32 server = candidates[i];

    bool seen_before = false;
    for (u32 j = 0; j < i; j++) {
      if (candidates[j] == server) {
        seen_before = true;
        break;
      }
    }
    if (seen_before)
      continue;

    if (dns_query_server(server, query_id, dns_query, query_len, resolved_ip))
      return true;
    if (dns_query_unreachable)
      dns_last_lookup_unreachable = true;
  }

  return false;
}

// ─── DHCP ───

void dhcp_send_discover() {
  alignas(4) DhcpMessage msg;
  str::memset(&msg, 0, sizeof(msg));
  msg.op = 1;
  msg.htype = 1;
  msg.hlen = 6;
  msg.xid = htonl(dhcp_xid);
  msg.flags = htons(0x8000); // Broadcast flag
  str::memcpy(msg.chaddr, local_mac, 6);
  msg.magic_cookie = htonl(DHCP_MAGIC);

  u8 *opt = msg.options;
  int oi = 0;
  // Option 53: DHCP Message Type = Discover
  opt[oi++] = 53;
  opt[oi++] = 1;
  opt[oi++] = DHCP_DISCOVER;
  // Option 55: Parameter Request List
  opt[oi++] = 55;
  opt[oi++] = 4;
  opt[oi++] = 1;  // Subnet Mask
  opt[oi++] = 3;  // Router
  opt[oi++] = 6;  // DNS
  opt[oi++] = 28; // Broadcast Address
  // End
  opt[oi++] = 255;

  u32 msg_len = 240 + static_cast<u32>(oi);
  // Pad to BOOTP minimum (300 bytes)
  if (msg_len < 300)
    msg_len = 300;

  // DHCP uses special IP: 0.0.0.0 → 255.255.255.255
  // Build packet manually since we have no IP yet
  u8 udp_pkt[sizeof(UdpHdr) + sizeof(DhcpMessage)];
  UdpHdr *udp = reinterpret_cast<UdpHdr *>(udp_pkt);
  udp->src_port = htons(68);
  udp->dst_port = htons(67);
  udp->length = htons(static_cast<u16>(sizeof(UdpHdr) + msg_len));
  udp->checksum = 0;
  str::memcpy(udp_pkt + sizeof(UdpHdr), &msg, msg_len);

  u8 ip_pkt[sizeof(Ipv4Hdr) + sizeof(udp_pkt)];
  Ipv4Hdr *ip = reinterpret_cast<Ipv4Hdr *>(ip_pkt);
  str::memset(ip, 0, sizeof(Ipv4Hdr));
  ip->ver_ihl = 0x45;
  ip->ttl = 64;
  ip->protocol = PROTO_UDP;
  u32 ip_total = sizeof(Ipv4Hdr) + sizeof(UdpHdr) + msg_len;
  ip->total_len = htons(static_cast<u16>(ip_total));
  ip->id = htons(ip_id_counter++);
  ip->src_ip = 0;          // 0.0.0.0
  ip->dst_ip = 0xFFFFFFFF; // 255.255.255.255 in network byte order
  ip->checksum = 0;
  ip->checksum = htons(inet_checksum(ip, sizeof(Ipv4Hdr)));
  str::memcpy(ip_pkt + sizeof(Ipv4Hdr), udp_pkt,
              sizeof(UdpHdr) + msg_len);

  eth_send(BROADCAST_MAC, ETH_IPV4, ip_pkt, ip_total);
}

void dhcp_send_request() {
  alignas(4) DhcpMessage msg;
  str::memset(&msg, 0, sizeof(msg));
  msg.op = 1;
  msg.htype = 1;
  msg.hlen = 6;
  msg.xid = htonl(dhcp_xid);
  msg.flags = htons(0x8000);
  str::memcpy(msg.chaddr, local_mac, 6);
  msg.magic_cookie = htonl(DHCP_MAGIC);

  volatile u8 *opt = msg.options;
  u32 oi = 0;
  // Option 53: DHCP Request
  opt[oi++] = 53;
  opt[oi++] = 1;
  opt[oi++] = DHCP_REQUEST;
  // Option 50: Requested IP Address
  opt[oi++] = 50;
  opt[oi++] = 4;
  opt[oi++] = (dhcp_offered_ip >> 24) & 0xFF;
  opt[oi++] = (dhcp_offered_ip >> 16) & 0xFF;
  opt[oi++] = (dhcp_offered_ip >> 8) & 0xFF;
  opt[oi++] = dhcp_offered_ip & 0xFF;
  // Option 54: Server Identifier
  opt[oi++] = 54;
  opt[oi++] = 4;
  opt[oi++] = (dhcp_server_ip >> 24) & 0xFF;
  opt[oi++] = (dhcp_server_ip >> 16) & 0xFF;
  opt[oi++] = (dhcp_server_ip >> 8) & 0xFF;
  opt[oi++] = dhcp_server_ip & 0xFF;
  // End
  opt[oi++] = 255;

  u32 msg_len = 240 + static_cast<u32>(oi);
  // Pad to BOOTP minimum (300 bytes)
  if (msg_len < 300)
    msg_len = 300;

  u8 udp_pkt[sizeof(UdpHdr) + sizeof(DhcpMessage)];
  UdpHdr *udp = reinterpret_cast<UdpHdr *>(udp_pkt);
  udp->src_port = htons(68);
  udp->dst_port = htons(67);
  udp->length = htons(static_cast<u16>(sizeof(UdpHdr) + msg_len));
  udp->checksum = 0;
  str::memcpy(udp_pkt + sizeof(UdpHdr), &msg, msg_len);

  u8 ip_pkt[sizeof(Ipv4Hdr) + sizeof(udp_pkt)];
  Ipv4Hdr *ip = reinterpret_cast<Ipv4Hdr *>(ip_pkt);
  str::memset(ip, 0, sizeof(Ipv4Hdr));
  ip->ver_ihl = 0x45;
  ip->ttl = 64;
  ip->protocol = PROTO_UDP;
  u32 ip_total = sizeof(Ipv4Hdr) + sizeof(UdpHdr) + msg_len;
  ip->total_len = htons(static_cast<u16>(ip_total));
  ip->id = htons(ip_id_counter++);
  ip->src_ip = 0;
  ip->dst_ip = 0xFFFFFFFF;
  ip->checksum = 0;
  ip->checksum = htons(inet_checksum(ip, sizeof(Ipv4Hdr)));
  str::memcpy(ip_pkt + sizeof(Ipv4Hdr), udp_pkt,
              sizeof(UdpHdr) + msg_len);

  eth_send(BROADCAST_MAC, ETH_IPV4, ip_pkt, ip_total);
}

bool do_dhcp() {
  dhcp_got_msg_type = 0;

  // Increment xid for each attempt
  dhcp_xid++;

  // Send DHCP Discover
  dhcp_send_discover();

  // Wait for DHCP Offer with bounded single-packet polling.
  for (u32 iter = 0; iter < 50000; iter++) {
    i32 len = netdev::recv(recv_buf, sizeof(recv_buf));
    if (len > 0)
      process_packet(recv_buf, static_cast<u32>(len));
    if (dhcp_got_msg_type == DHCP_OFFER)
      break;
  }
  if (dhcp_got_msg_type != DHCP_OFFER)
    return false;

  // Send DHCP Request
  dhcp_got_msg_type = 0;
  dhcp_send_request();

  // Wait for DHCP ACK with bounded single-packet polling.
  for (u32 iter = 0; iter < 50000; iter++) {
    i32 len = netdev::recv(recv_buf, sizeof(recv_buf));
    if (len > 0)
      process_packet(recv_buf, static_cast<u32>(len));
    if (dhcp_got_msg_type == DHCP_ACK)
      break;
  }
  if (dhcp_got_msg_type != DHCP_ACK)
    return false;

  // Apply configuration
  local_ip = dhcp_offered_ip;
  gateway_ip = dhcp_offered_gateway;
  subnet_mask = dhcp_offered_subnet;
  dns_ip = dhcp_offered_dns;
  configured = true;

  return true;
}

// ─── TCP connect / close ───

bool tcp_connect(u32 ip, u16 port, u32 timeout_ms) {
  // Resolve MAC for destination
  u8 dst_mac[6];
  if (!resolve_dst_mac(ip, dst_mac))
    return false;

  // Initialize connection state
  str::memset(&tcp_conn, 0, sizeof(tcp_conn));
  tcp_conn.remote_ip = ip;
  tcp_conn.remote_port = port;
  tcp_conn.local_port = ++tcp_ephemeral_port;
  if (tcp_conn.local_port < 49152)
    tcp_conn.local_port = 49152;
  tcp_conn.snd_nxt = static_cast<u32>(get_ticks() & 0xFFFFFFFF);
  tcp_conn.rcv_nxt = 0;
  str::memcpy(tcp_conn.dst_mac, dst_mac, 6);
  tcp_conn.got_rst = false;
  tcp_conn.fin_received = false;
  tcp_recv_len = 0;

  // Send SYN
  tcp_conn.state = TCP_STATE_SYN_SENT;
  tcp_send_segment(TCP_SYN, nullptr, 0);

  // Wait for ESTABLISHED
  u64 start = get_ticks();
  while (!elapsed_ms(start, timeout_ms)) {
    poll_one_packet();
    if (tcp_conn.state == TCP_STATE_ESTABLISHED)
      return true;
    if (tcp_conn.got_rst || tcp_conn.state == TCP_STATE_CLOSED)
      return false;
    asm volatile("yield");
  }

  // Timeout — send RST to clean up
  tcp_send_segment(TCP_RST | TCP_ACK, nullptr, 0);
  tcp_conn.state = TCP_STATE_CLOSED;
  return false;
}

void tcp_send_data(const void *data, u32 len) {
  const u8 *p = static_cast<const u8 *>(data);
  while (len > 0) {
    u32 chunk = (len > 1400) ? 1400 : len;
    tcp_send_segment(TCP_ACK | TCP_PSH, p, chunk);
    p += chunk;
    len -= chunk;
  }
}

void tcp_close() {
  if (tcp_conn.state == TCP_STATE_ESTABLISHED) {
    tcp_conn.state = TCP_STATE_FIN_WAIT_1;
    tcp_send_segment(TCP_FIN | TCP_ACK, nullptr, 0);
    u64 start = get_ticks();
    while (!elapsed_ms(start, 2000)) {
      poll_one_packet();
      if (tcp_conn.state == TCP_STATE_CLOSED)
        return;
      asm volatile("yield");
    }
  }
  tcp_conn.state = TCP_STATE_CLOSED;
}

// ─── URL parsing ───

struct ParsedUrl {
  char host[128];
  char path[256];
  u16 port;
  bool valid;
};

ParsedUrl parse_url(const char *url) {
  ParsedUrl u;
  str::memset(&u, 0, sizeof(u));
  u.port = 80;
  u.valid = false;

  const char *p = url;

  // Skip http://
  if (str::starts_with(p, "http://")) {
    p += 7;
  } else if (str::starts_with(p, "https://")) {
    // HTTPS not supported
    return u;
  }

  // Extract host (and optional port)
  const char *host_start = p;
  u32 host_len = 0;

  while (*p && *p != '/' && *p != ':') {
    host_len++;
    p++;
  }
  if (host_len == 0 || host_len >= sizeof(u.host))
    return u;
  str::memcpy(u.host, host_start, host_len);
  u.host[host_len] = '\0';

  // Optional port
  if (*p == ':') {
    p++;
    u.port = 0;
    while (*p >= '0' && *p <= '9') {
      u.port = u.port * 10 + static_cast<u16>(*p - '0');
      p++;
    }
    if (u.port == 0)
      return u;
  }

  // Path (default to "/")
  if (*p == '/') {
    u32 path_len = 0;
    while (*p && path_len < sizeof(u.path) - 1) {
      u.path[path_len++] = *p++;
    }
    u.path[path_len] = '\0';
  } else {
    u.path[0] = '/';
    u.path[1] = '\0';
  }

  u.valid = true;
  return u;
}

// ─── curl implementation ───

void do_curl(const char *url) {
  if (!configured) {
    uart::puts("curl: network not configured. Run \033[1mdhcp\033[0m first.\n");
    return;
  }

  ParsedUrl pu = parse_url(url);
  if (!pu.valid) {
    uart::puts("curl: invalid URL (only http:// supported)\n");
    return;
  }

  // Resolve hostname to IP
  u32 dst_ip = parse_ip(pu.host);
  if (dst_ip == 0) {
    if (!dns_resolve_a(pu.host, &dst_ip)) {
      uart::puts("curl: could not resolve host: ");
      uart::puts(pu.host);
      uart::putc('\n');
      return;
    }
  }

  // TCP connect
  if (!tcp_connect(dst_ip, pu.port, 5000)) {
    if (tcp_conn.got_rst) {
      uart::puts("curl: connection refused\n");
    } else {
      uart::puts("curl: connection timed out\n");
    }
    return;
  }

  // Build HTTP/1.0 GET request
  char req[512];
  req[0] = '\0';
  str::cat(req, "GET ");
  str::cat(req, pu.path);
  str::cat(req, " HTTP/1.0\r\nHost: ");
  str::cat(req, pu.host);
  str::cat(req, "\r\nUser-Agent: OguzOS/1.0\r\nConnection: close\r\n\r\n");

  tcp_send_data(req, static_cast<u32>(str::len(req)));

  // Receive response
  // Phase 1: Read headers, find \r\n\r\n boundary
  char hdr_buf[2048];
  u32 hdr_len = 0;
  bool headers_done = false;
  u32 body_start_offset = 0;

  u64 timeout_start = get_ticks();
  constexpr u32 RECV_TIMEOUT_MS = 10000;

  while (!headers_done) {
    poll_one_packet();

    if (tcp_recv_len > 0) {
      timeout_start = get_ticks(); // Reset timeout on data
      u32 copy = tcp_recv_len;
      if (hdr_len + copy > sizeof(hdr_buf) - 1)
        copy = sizeof(hdr_buf) - 1 - hdr_len;
      if (copy > 0) {
        str::memcpy(hdr_buf + hdr_len, tcp_recv_buf, copy);
        hdr_len += copy;
        hdr_buf[hdr_len] = '\0';
      }
      tcp_recv_len = 0;

      // Search for header/body boundary
      for (u32 i = 0; i + 3 < hdr_len; i++) {
        if (hdr_buf[i] == '\r' && hdr_buf[i + 1] == '\n' &&
            hdr_buf[i + 2] == '\r' && hdr_buf[i + 3] == '\n') {
          headers_done = true;
          body_start_offset = i + 4;
          break;
        }
      }
    }

    if (tcp_conn.fin_received || tcp_conn.got_rst ||
        tcp_conn.state == TCP_STATE_CLOSED ||
        tcp_conn.state == TCP_STATE_CLOSING)
      break;
    if (elapsed_ms(timeout_start, RECV_TIMEOUT_MS))
      break;
    asm volatile("yield");
  }

  if (hdr_len == 0) {
    uart::puts("curl: no response received\n");
    tcp_close();
    return;
  }

  // Print any body data already in the header buffer
  if (body_start_offset < hdr_len) {
    for (u32 i = body_start_offset; i < hdr_len; i++) {
      uart::putc(hdr_buf[i]);
    }
  }

  // Phase 2: Print remaining body data as it arrives
  if (!tcp_conn.fin_received && tcp_conn.state != TCP_STATE_CLOSED &&
      tcp_conn.state != TCP_STATE_CLOSING) {
    timeout_start = get_ticks();
    while (true) {
      poll_one_packet();

      if (tcp_recv_len > 0) {
        timeout_start = get_ticks();
        for (u32 i = 0; i < tcp_recv_len; i++) {
          uart::putc(static_cast<char>(tcp_recv_buf[i]));
        }
        tcp_recv_len = 0;
      }

      if (tcp_conn.fin_received || tcp_conn.got_rst ||
          tcp_conn.state == TCP_STATE_CLOSED ||
          tcp_conn.state == TCP_STATE_CLOSING)
        break;
      if (elapsed_ms(timeout_start, RECV_TIMEOUT_MS))
        break;
      asm volatile("yield");
    }
  }

  // Ensure output ends with a newline
  uart::putc('\n');

  tcp_close();
}

} // anonymous namespace

// ─── Public interface ───

namespace net {

bool init() {
  if (!netdev::is_available())
    return false;

  netdev::get_mac(local_mac);
  str::memset(arp_table, 0, sizeof(arp_table));
  configured = false;

  // Small delay to let virtio-net device stabilize
  delay_ms(100);

  // Drain any packets that arrived during init
  poll_packets();

  // Run DHCP with retries
  uart::puts("  [net]  DHCP: requesting IP... ");
  for (u32 attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      uart::putc('.');
      delay_ms(500);
    }
    if (do_dhcp()) {
      uart::puts("\033[1;32mok\033[0m\n");
      uart::puts("  [net]  IPv4: ");
      put_ip(local_ip);
      uart::puts(" gw ");
      put_ip(gateway_ip);
      uart::putc('\n');
      return true;
    }
  }

  uart::puts("\033[1;31mfailed\033[0m (run \033[1mdhcp\033[0m manually)\n");
  return false;
}

bool is_available() { return configured; }

void ifconfig() {
  if (!netdev::is_available()) {
    uart::puts("No network device found.\n");
    return;
  }

  uart::puts("\033[1meth0\033[0m:\n");
  uart::puts("  MAC     : ");
  put_mac(local_mac);
  uart::putc('\n');

  if (configured) {
    uart::puts("  IPv4    : ");
    put_ip(local_ip);
    uart::putc('\n');
    uart::puts("  Subnet  : ");
    put_ip(subnet_mask);
    uart::putc('\n');
    uart::puts("  Gateway : ");
    put_ip(gateway_ip);
    uart::putc('\n');
    if (dns_ip) {
      uart::puts("  DNS     : ");
      put_ip(dns_ip);
      uart::putc('\n');
    }
  } else {
    uart::puts("  Status  : not configured (run \033[1mdhcp\033[0m)\n");
  }
}

void ping(const char *target, u32 count) {
  u32 dst_ip = parse_ip(target);
  bool resolved_from_dns = false;

  if (dst_ip == 0 && str::cmp(target, "localhost") == 0) {
    dst_ip = LOOPBACK_IP;
  } else if (dst_ip == 0) {
    if (!configured) {
      uart::puts("Network not configured. Run \033[1mdhcp\033[0m first.\n");
      return;
    }
    if (dns_ip == 0) {
      uart::puts("No DNS server configured. Use \033[1mping <ip>\033[0m.\n");
      return;
    }
    if (!dns_resolve_a(target, &dst_ip)) {
      if (dns_last_lookup_unreachable) {
        uart::puts("DNS lookup failed: network unreachable.\n");
      } else {
        uart::puts("Could not resolve host: ");
        uart::puts(target);
        uart::putc('\n');
      }
      return;
    }
    resolved_from_dns = true;
  }

  if (dst_ip == LOOPBACK_IP) {
    uart::puts("PING ");
    put_ip(dst_ip);
    uart::puts(":\n");

    for (u32 seq = 1; seq <= count; seq++) {
      uart::puts("  Reply from ");
      put_ip(dst_ip);
      uart::puts(": seq=");
      uart::put_int(static_cast<i64>(seq));
      uart::puts(" time=0ms\n");
    }

    uart::puts("--- ");
    put_ip(dst_ip);
    uart::puts(" ping statistics ---\n");
    uart::put_int(static_cast<i64>(count));
    uart::puts(" sent, ");
    uart::put_int(static_cast<i64>(count));
    uart::puts(" received, 0% loss\n");
    return;
  }

  if (!configured) {
    uart::puts("Network not configured. Run \033[1mdhcp\033[0m first.\n");
    return;
  }

  if (resolved_from_dns) {
    uart::puts("Resolved ");
    uart::puts(target);
    uart::puts(" -> ");
    put_ip(dst_ip);
    uart::putc('\n');
  }

  // Resolve MAC
  u8 dst_mac[6];
  uart::puts("PING ");
  put_ip(dst_ip);
  uart::puts(":\n");

  if (!resolve_dst_mac(dst_ip, dst_mac)) {
    uart::puts("  ARP resolution failed\n");
    return;
  }

  u32 sent = 0;
  u32 received = 0;

  for (u32 seq = 1; seq <= count; seq++) {
    // Build ICMP echo request
    u8 icmp_pkt[64];
    str::memset(icmp_pkt, 0, sizeof(icmp_pkt));
    IcmpHdr *icmp = reinterpret_cast<IcmpHdr *>(icmp_pkt);
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(0x1234);
    icmp->seq = htons(static_cast<u16>(seq));
    // Fill data
    for (u32 i = sizeof(IcmpHdr); i < 40; i++) {
      icmp_pkt[i] = static_cast<u8>(i);
    }
    icmp->checksum = 0;
    icmp->checksum = htons(inet_checksum(icmp_pkt, 40));

    ping_got_reply = false;
    u64 t_start = get_ticks();

    ipv4_send(dst_ip, PROTO_ICMP, icmp_pkt, 40, dst_mac);
    sent++;

    // Wait for reply (up to 2 seconds)
    u64 wait_start = get_ticks();
    while (!elapsed_ms(wait_start, 2000)) {
      poll_one_packet();
      if (ping_got_reply)
        break;
      asm volatile("yield");
    }

    if (ping_got_reply) {
      u64 t_end = get_ticks();
      u32 rtt = ticks_to_ms(t_end - t_start);
      received++;
      uart::puts("  Reply from ");
      put_ip(dst_ip);
      uart::puts(": seq=");
      uart::put_int(static_cast<i64>(seq));
      uart::puts(" time=");
      uart::put_int(static_cast<i64>(rtt));
      uart::puts("ms\n");
    } else {
      uart::puts("  Request timeout for seq=");
      uart::put_int(static_cast<i64>(seq));
      uart::putc('\n');
    }

    // Small delay between pings (except after last one)
    if (seq < count) {
      u64 delay_start = get_ticks();
      while (!elapsed_ms(delay_start, 1000)) {
        poll_one_packet();
        asm volatile("yield");
      }
    }
  }

  // Summary
  uart::puts("--- ");
  put_ip(dst_ip);
  uart::puts(" ping statistics ---\n");
  uart::put_int(static_cast<i64>(sent));
  uart::puts(" sent, ");
  uart::put_int(static_cast<i64>(received));
  uart::puts(" received, ");
  u32 loss = (sent > 0) ? ((sent - received) * 100 / sent) : 0;
  uart::put_int(static_cast<i64>(loss));
  uart::puts("% loss\n");
}

void dhcp() {
  if (!netdev::is_available()) {
    uart::puts("No network device found.\n");
    return;
  }

  uart::puts("DHCP: requesting IP... ");
  if (do_dhcp()) {
    uart::puts("\033[1;32mok\033[0m\n");
    uart::puts("  IP      : ");
    put_ip(local_ip);
    uart::putc('\n');
    uart::puts("  Subnet  : ");
    put_ip(subnet_mask);
    uart::putc('\n');
    uart::puts("  Gateway : ");
    put_ip(gateway_ip);
    uart::putc('\n');
    if (dns_ip) {
      uart::puts("  DNS     : ");
      put_ip(dns_ip);
      uart::putc('\n');
    }
  } else {
    uart::puts("\033[1;31mfailed\033[0m\n");
  }
}

void curl(const char *url) { do_curl(url); }

} // namespace net
