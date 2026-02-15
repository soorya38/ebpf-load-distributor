// xdp_lb.c
// -----------------------------------------------------------------------------
// eBPF XDP Load Balancer (Demo Version with Logging)
//
// Description:
//   This program implements a minimal Layer-3 load balancer using eBPF/XDP.
//   It intercepts incoming IPv4 packets at the NIC level, selects a backend
//   server using hash-based load distribution, rewrites destination IP/MAC,
//   and retransmits the packet.
//
// Logging:
//   Kernel trace logging is enabled via bpf_printk() for demo visibility.
//   Logs can be viewed using:
//
//       cat /sys/kernel/debug/tracing/trace_pipe
//       OR
//       bpftool prog tracelog
//
// Notes:
//   - Intended for educational/demo purposes.
//   - Production implementations require checksum updates,
//     connection tracking, health checks, etc.
// -----------------------------------------------------------------------------

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>

#define MAX_BACKENDS 8


// -----------------------------------------------------------------------------
// Backend Server Structure
// -----------------------------------------------------------------------------
struct backend {
    __u32 ip;        // Backend IPv4 address (network byte order)
    __u8  mac[6];    // Backend MAC address
};


// -----------------------------------------------------------------------------
// Backend Map
//
// Stores backend server metadata indexed by integer key.
// Typically populated from user space.
// -----------------------------------------------------------------------------
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, MAX_BACKENDS);
    __type(key, __u32);
    __type(value, struct backend);
} backends SEC(".maps");


// -----------------------------------------------------------------------------
// Backend Count Map
//
// Stores number of active backend servers.
// Allows dynamic updates without recompiling BPF program.
// -----------------------------------------------------------------------------
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} backend_count SEC(".maps");


// -----------------------------------------------------------------------------
// Simple Hash Function
//
// Used to distribute traffic based on source IP.
// Lightweight, verifier-safe, non-cryptographic.
// -----------------------------------------------------------------------------
static __always_inline __u32 hash(__u32 a)
{
    a ^= a >> 16;
    a *= 0x7feb352d;
    a ^= a >> 15;
    a *= 0x846ca68b;
    a ^= a >> 16;
    return a;
}


// -----------------------------------------------------------------------------
// XDP Entry Function
//
// Packet Processing Steps:
//
//   1. Validate packet bounds
//   2. Parse Ethernet header
//   3. Ensure IPv4 packet
//   4. Parse IP header safely
//   5. Select backend via hash
//   6. Rewrite destination IP + MAC
//   7. Transmit packet back
// -----------------------------------------------------------------------------
SEC("xdp")
int xdp_load_balancer(struct xdp_md *ctx)
{
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;


    // -------------------------------------------------------------------------
    // Ethernet Header Validation
    // -------------------------------------------------------------------------
    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return XDP_PASS;


    // -------------------------------------------------------------------------
    // IPv4 Header Parsing
    // -------------------------------------------------------------------------
    struct iphdr *iph = (void *)(eth + 1);

    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    // Demo log: packet detected
    bpf_printk("XDP: IPv4 packet src=%x dst=%x\n",
               iph->saddr, iph->daddr);


    // -------------------------------------------------------------------------
    // Retrieve Active Backend Count
    // -------------------------------------------------------------------------
    __u32 key0 = 0;
    __u32 *count = bpf_map_lookup_elem(&backend_count, &key0);

    if (!count || *count == 0) {
        bpf_printk("XDP: No backend servers configured\n");
        return XDP_PASS;
    }


    // -------------------------------------------------------------------------
    // Select Backend (Hash-Based Distribution)
    // -------------------------------------------------------------------------
    __u32 idx = hash(iph->saddr) % *count;

    struct backend *b = bpf_map_lookup_elem(&backends, &idx);
    if (!b) {
        bpf_printk("XDP: Backend lookup failed idx=%d\n", idx);
        return XDP_PASS;
    }

    bpf_printk("XDP: Forwarding to backend index=%d\n", idx);


    // -------------------------------------------------------------------------
    // Rewrite Destination IP and MAC
    // NOTE: Production code must update checksums.
    // -------------------------------------------------------------------------
    iph->daddr = b->ip;
    __builtin_memcpy(eth->h_dest, b->mac, 6);


    // -------------------------------------------------------------------------
    // Transmit Packet Back via Interface
    // -------------------------------------------------------------------------
    return XDP_TX;
}


// Required license declaration for kernel loading
char LICENSE[] SEC("license") = "GPL";

