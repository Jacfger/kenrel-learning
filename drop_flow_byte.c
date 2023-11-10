#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/slab.h>

static int port = 80;
MODULE_PARM_DESC(port, "Port number. The default port is 80");
module_param(port, int, 0644);

static int bcount = 1000000;
MODULE_PARM_DESC(bcount, "Maximum byte allowed before dropping. The default value is 1000");
module_param(bcount, int, 0644);

struct myflow {
    unsigned int saddr;
    unsigned int daddr;
    unsigned short int sport;
    unsigned short int dport;

    // State
    unsigned int pkt_count, drop_count, byte_count, byte_drop_count;
};

#define FLOW_TABLE_SIZE 4096
static struct myflow flow_table[FLOW_TABLE_SIZE];

/* Hash function */
static inline unsigned int hash(unsigned int saddr, unsigned daddr, unsigned short int sport, unsigned short int dport)
{
    return ((saddr % FLOW_TABLE_SIZE + 1) * (daddr % FLOW_TABLE_SIZE + 1) * (sport % FLOW_TABLE_SIZE + 1) * (dport % FLOW_TABLE_SIZE + 1)) % FLOW_TABLE_SIZE;
}

static inline void reset_flow(struct myflow *flow)
{
    memset(flow, 0, sizeof(struct myflow));
}

static struct myflow *get_flow(unsigned int saddr, unsigned daddr, unsigned short int sport, unsigned short int dport)
{
    struct myflow *flow;

    flow = &flow_table[hash(saddr, daddr, sport, dport)];

    /* New flow. Initialize source, destination port & address */
    if (flow->saddr == 0 && flow->daddr == 0 && flow->sport == 0 && flow->dport == 0) {
        flow->saddr = saddr;
        flow->daddr = daddr;
        flow->sport = sport;
        flow->dport = dport;
    }

    return flow;
}

/* Hook function */
static unsigned int drop_flow(void *priv, struct sk_buff* skb, const struct nf_hook_state *state)
{
    struct iphdr *iph;
    struct tcphdr *tcph;
    unsigned int saddr, daddr;
    unsigned short int sport, dport;
    struct myflow *flow;
    unsigned int payload_len;
    int ret; // Return value

    iph = ip_hdr(skb);
    if (iph->protocol != IPPROTO_TCP)
        return NF_ACCEPT;

    tcph = tcp_hdr(skb);
    saddr = iph->saddr;
    daddr = iph->daddr;
    sport = ntohs(tcph->source);
    dport = ntohs(tcph->dest);

    if (sport != port && dport != port)
        return NF_ACCEPT;

    flow = get_flow(saddr, daddr, sport, dport);

    /* Payload length is the total packet length minus IP and TCP header length
     * iph->ihl and tcph->doff are multiples of 4 bytes */
    payload_len = ntohs(iph->tot_len) - (iph->ihl << 2) - (tcph->doff << 2);

    /* Does not contain data, accept immediately */
    if (payload_len == 0) {
        ret = NF_ACCEPT;
        goto out;
    }

    if (flow->byte_count < bcount) {
        flow->pkt_count++;
        flow->byte_count += payload_len;

        printk(KERN_INFO "[FLOW-ACCEPT] [%pI4:%d->%pI4:%d] payload:%d pkts(accept/drop): %d/%d bytes(accept/drop): %d/%d\n",
            &flow->saddr, flow->sport, &flow->daddr, flow->dport, payload_len,
            flow->pkt_count, flow->drop_count, flow->byte_count, flow->byte_drop_count);

        ret = NF_ACCEPT;
    }
    else {
        flow->drop_count++;
        flow->byte_drop_count += payload_len;

        printk(KERN_INFO "[FLOW-DROP] [%pI4:%d->%pI4:%d] payload:%d pkts(accept/drop): %d/%d bytes(accept/drop): %d/%d\n",
            &flow->saddr, flow->sport, &flow->daddr, flow->dport, payload_len,
            flow->pkt_count, flow->drop_count, flow->byte_count, flow->byte_drop_count);

        ret = NF_DROP;
    }

out:
    /* Reset flow if TCP FIN bit is set */
    if (unlikely(tcph->fin))
        reset_flow(flow);
    return ret;
}

static const struct nf_hook_ops drop_flow_nf_ops = {
    .hook = drop_flow,
    .pf = NFPROTO_INET,
    .hooknum = NF_INET_PRE_ROUTING,
    .priority = NF_IP_PRI_FIRST, // Lowest priority value, highest priority
};

static int __init drop_flow_init_module(void)
{
    int err;
    int i;

    for (i = 0; i < FLOW_TABLE_SIZE; ++i)
        reset_flow(&flow_table[i]);

    err = nf_register_net_hook(&init_net, &drop_flow_nf_ops);
    if (err < 0)
        return err;

    return 0;
}

static void __exit drop_flow_exit_module(void)
{
    nf_unregister_net_hook(&init_net, &drop_flow_nf_ops);
}

module_init(drop_flow_init_module);
module_exit(drop_flow_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author's name");
MODULE_DESCRIPTION("Netfilter module to drop packets with flow");
