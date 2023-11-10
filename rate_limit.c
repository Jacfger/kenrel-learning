#include <linux/init.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/slab.h>
#include <linux/tcp.h>


static int threshold = 1 << 20;
module_param(threshold, int, 0644);

static int rate = 2000;
module_param(rate, int, 0644);

struct myflow {
	unsigned int saddr;
	unsigned int daddr;
	unsigned short int sport;
	unsigned short int dport;

	// State
	unsigned int pkt_count, drop_count, byte_count, byte_drop_count;
	unsigned long granular;
	unsigned long start_granular;
	unsigned int next_limit;
};

#define FLOW_TABLE_SIZE 4096
// static struct myflow flow_table[FLOW_TABLE_SIZE];

static struct kmem_cache *flow_cache;

static struct myflow *flow_table[FLOW_TABLE_SIZE] = {[0 ... FLOW_TABLE_SIZE - 1] = NULL};


/* Hash function */
static inline unsigned int hash(unsigned int saddr, unsigned daddr, unsigned short int sport, unsigned short int dport) {
	return ((saddr % FLOW_TABLE_SIZE + 1) * (daddr % FLOW_TABLE_SIZE + 1) * (sport % FLOW_TABLE_SIZE + 1) * (dport % FLOW_TABLE_SIZE + 1)) % FLOW_TABLE_SIZE;
}

static inline void reset_flow(void *flow) {
	memset(flow, 0, sizeof(struct myflow));
}

static struct myflow *get_flow(unsigned int saddr, unsigned daddr, unsigned short int sport, unsigned short int dport) {
	struct myflow *flow;
	int flow_hash = hash(saddr, daddr, sport, dport);
	flow = flow_table[flow_hash];

	/* New flow. Initialize source, destination port & address */
	// if (flow->saddr == 0 && flow->daddr == 0 && flow->sport == 0 && flow->dport == 0) {
	if (flow == NULL) {
		printk(KERN_INFO "flow cache not found, making a new one\n");
		flow_table[flow_hash] = flow = kmem_cache_alloc(flow_cache, GFP_KERNEL);
		// Jump if error happen in kmem_cache_alloc
		if (flow == NULL)
			return NULL;
		flow->saddr = saddr;
		flow->daddr = daddr;
		flow->sport = sport;
		flow->dport = dport;
		flow->granular = 0;
		flow->start_granular = jiffies;
		// flow->next_limit = rate * 1000;
		flow->next_limit = threshold;
	}

	return flow;
}

/* Hook function */
static unsigned int drop_flow(void *priv, struct sk_buff *skb, const struct nf_hook_state *state) {
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

	// if (sport != 443 && dport != 443) {
	// 	return NF_ACCEPT;
	// }

	flow = get_flow(saddr, daddr, sport, dport);
	if (flow == NULL) {
		printk(KERN_INFO "Failed to allocate new cache\n");
        ret = NF_DROP;
        goto out;
	}

	/* Payload length is the total packet length minus IP and TCP header length
     * iph->ihl and tcph->doff are multiples of 4 bytes */
	payload_len = ntohs(iph->tot_len) - (iph->ihl << 2) - (tcph->doff << 2);

	/* Does not contain data, accept immediately */
	if (payload_len == 0) {
		ret = NF_ACCEPT;
		goto out;
	}
	if (flow->byte_count > threshold && time_after(jiffies, flow->granular)) {
		// flow->next_limit = flow->byte_count + rate * 1000;
		if (flow->granular == 0) {
			flow->granular = jiffies;
		}
		flow->next_limit += (jiffies - flow->granular) * rate * HZ_TO_MSEC_NUM;
		flow->granular = jiffies;
		// printk(KERN_INFO "Jiffies: %lu, Granular: %lu\n", jiffies, flow->granular);
	}

	if (flow->byte_count < threshold || flow->byte_count < flow->next_limit) {
		flow->pkt_count++;
		flow->byte_count += payload_len;
		// printk(KERN_INFO "[FLOW-ACCEPT] payload:%d pkts(accept/drop): %d/%d bytes(accept/drop): %d/%d. flow limit: %d\n",
		// 	   payload_len, flow->pkt_count, flow->drop_count, flow->byte_count, flow->byte_drop_count, flow->next_limit);
		ret = NF_ACCEPT;
	} else {
		flow->drop_count++;
		flow->byte_drop_count += payload_len;
		// printk(KERN_INFO "[FLOW-DROP] payload:%d pkts(accept/drop): %d/%d bytes(accept/drop): %d/%d. flow limit: %d\n",
		// 	   payload_len, flow->pkt_count, flow->drop_count, flow->byte_count, flow->byte_drop_count, flow->next_limit);
		ret = NF_DROP;
	}

out:
	/* Reset flow if TCP FIN bit is set */
	if (unlikely(tcph->fin)) {
		printk(KERN_INFO "[Finish rate = %d] t = %lu ms accept/drop (bytes): %d/%d\n", rate, (jiffies - flow->start_granular) * HZ_TO_MSEC_NUM, flow->byte_count, flow->byte_drop_count);
		int flow_hash = hash(saddr, daddr, sport, dport);
		flow_table[flow_hash] = 0;
		kmem_cache_free(flow_cache, flow);
		// reset_flow(flow);
	}
	return ret;
}

static const struct nf_hook_ops drop_flow_nf_ops = {
	.hook = drop_flow,
	.pf = NFPROTO_INET,
	.hooknum = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST, // Lowest priority value, highest priority
};

static int __init drop_flow_init_module(void) {
	int err;
	int i;

	flow_cache = kmem_cache_create("flow table", sizeof(struct myflow), 0, 0, reset_flow);
	if (flow_cache == NULL) {
		printk(KERN_INFO "Failed to initialize kmem_cache\n");
	}

	// for (i = 0; i < FLOW_TABLE_SIZE; ++i)
	// 	reset_flow(flow_table[i]);

	err = nf_register_net_hook(&init_net, &drop_flow_nf_ops);
	if (err < 0)
		return err;

	return 0;
}

static void __exit drop_flow_exit_module(void) {
	nf_unregister_net_hook(&init_net, &drop_flow_nf_ops);

	int i;
	for (i = 0; i < FLOW_TABLE_SIZE; i++) {
		if (flow_table[i] != 0) {
            kmem_cache_free(flow_cache, flow_table[i]);
		}
	}

	if (flow_cache != NULL)
		kmem_cache_destroy(flow_cache);
}

module_init(drop_flow_init_module);
module_exit(drop_flow_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author's name");
MODULE_DESCRIPTION("Netfilter module to drop packets with flow");
