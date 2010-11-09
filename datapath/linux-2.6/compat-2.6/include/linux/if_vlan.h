#ifndef __LINUX_IF_VLAN_WRAPPER_H
#define __LINUX_IF_VLAN_WRAPPER_H 1

#include_next <linux/if_vlan.h>
#include <linux/skbuff.h>

/*
 * The behavior of __vlan_put_tag() has changed over time:
 *
 *      - In 2.6.26 and earlier, it adjusted both MAC and network header
 *        pointers.  (The latter didn't make any sense.)
 *
 *      - In 2.6.27 and 2.6.28, it did not adjust any header pointers at all.
 *
 *      - In 2.6.29 and later, it adjusts the MAC header pointer only.
 *
 * This is the version from 2.6.33.  We unconditionally substitute this version
 * to avoid the need to guess whether the version in the kernel tree is
 * acceptable.
 */
#define __vlan_put_tag rpl_vlan_put_tag
static inline struct sk_buff *__vlan_put_tag(struct sk_buff *skb, u16 vlan_tci)
{
	struct vlan_ethhdr *veth;

	if (skb_cow_head(skb, VLAN_HLEN) < 0) {
		kfree_skb(skb);
		return NULL;
	}
	veth = (struct vlan_ethhdr *)skb_push(skb, VLAN_HLEN);

	/* Move the mac addresses to the beginning of the new header. */
	memmove(skb->data, skb->data + VLAN_HLEN, 2 * VLAN_ETH_ALEN);
	skb->mac_header -= VLAN_HLEN;

	/* first, the ethernet type */
	veth->h_vlan_proto = htons(ETH_P_8021Q);

	/* now, the TCI */
	veth->h_vlan_TCI = htons(vlan_tci);

	skb->protocol = htons(ETH_P_8021Q);

	return skb;
}

#endif	/* linux/if_vlan.h wrapper */
