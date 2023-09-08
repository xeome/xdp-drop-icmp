// XDP program that limits total data used by a single source address to 100MiB

#include <stdint.h>
#include <arpa/inet.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/icmp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>

#define OVER(x, d) (x + 1 > (typeof(x))d)

// Hashmap for storing the total data used by each source address
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32);
    __type(value, __u64);
    __uint(max_entries, 10240);
} data_used SEC(".maps");

SEC("prog")
int datalimit(struct xdp_md* ctx) {
    void* data_end = (void*)(long)ctx->data_end;
    void* data = (void*)(long)ctx->data;

    struct ethhdr* eth = data;
    struct iphdr* iph = (struct iphdr*)(eth + 1);
    struct icmphdr* icmph = (struct icmphdr*)(iph + 1);

    // Sanity checks
    if (OVER(eth, data_end))
        return XDP_DROP;

    if (eth->h_proto != ntohs(ETH_P_IP))
        return XDP_PASS;

    if (OVER(iph, data_end))
        return XDP_DROP;

    if (OVER(icmph, data_end))
        return XDP_DROP;

    // Get the total data used by this source address
    __u32 key = iph->saddr;
    __u64* value = bpf_map_lookup_elem(&data_used, &key);
    if (!value) {
        // If the source address is not in the hashmap, initialize it to 0
        __u64 zero = 0;
        bpf_map_update_elem(&data_used, &key, &zero, BPF_ANY);
        value = bpf_map_lookup_elem(&data_used, &key);
    }

    if (!value) {
        // If the source address is still not in the hashmap, drop the packet
        return XDP_DROP;
    }

    // If the total data used by this source address is greater than 100MiB, drop the packet
    if (*value && *value > 100000000) {
        return XDP_DROP;
    }

    // Update the total data used by this source address
    __u64 packet_len = data_end - data;
    *value += packet_len;

    return XDP_PASS;
}