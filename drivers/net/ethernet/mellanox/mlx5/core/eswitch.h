/*
 * Copyright (c) 2015, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __MLX5_ESWITCH_H__
#define __MLX5_ESWITCH_H__

#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <net/devlink.h>
#include <net/ip_tunnels.h>
#include <linux/mlx5/device.h>
#include <linux/rhashtable.h>


#define MLX5_MAX_UC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_uc_list))

#define MLX5_MAX_MC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_mc_list))

#define MLX5_L2_ADDR_HASH_SIZE (BIT(BITS_PER_BYTE))
#define MLX5_L2_ADDR_HASH(addr) (addr[5])

#define FDB_UPLINK_VPORT 0xffff

/* L2 -mac address based- hash helpers */
struct l2addr_node {
	struct hlist_node hlist;
	u8                addr[ETH_ALEN];
};

#define for_each_l2hash_node(hn, tmp, hash, i) \
	for (i = 0; i < MLX5_L2_ADDR_HASH_SIZE; i++) \
		hlist_for_each_entry_safe(hn, tmp, &hash[i], hlist)

#define l2addr_hash_find(hash, mac, type) ({                \
	int ix = MLX5_L2_ADDR_HASH(mac);                    \
	bool found = false;                                 \
	type *ptr = NULL;                                   \
							    \
	hlist_for_each_entry(ptr, &hash[ix], node.hlist)    \
		if (ether_addr_equal(ptr->node.addr, mac)) {\
			found = true;                       \
			break;                              \
		}                                           \
	if (!found)                                         \
		ptr = NULL;                                 \
	ptr;                                                \
})

#define l2addr_hash_add(hash, mac, type, gfp) ({            \
	int ix = MLX5_L2_ADDR_HASH(mac);                    \
	type *ptr = NULL;                                   \
							    \
	ptr = kzalloc(sizeof(type), gfp);                   \
	if (ptr) {                                          \
		ether_addr_copy(ptr->node.addr, mac);       \
		hlist_add_head(&ptr->node.hlist, &hash[ix]);\
	}                                                   \
	ptr;                                                \
})

#define l2addr_hash_del(ptr) ({                             \
	hlist_del(&ptr->node.hlist);                        \
	kfree(ptr);                                         \
})

struct vport_ingress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *allow_untagged_spoofchk_grp;
	struct mlx5_flow_group *allow_spoofchk_only_grp;
	struct mlx5_flow_group *allow_untagged_only_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_flow_handle  *allow_rule;
	struct mlx5_flow_handle  *drop_rule;
};

struct vport_egress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *allowed_vlans_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_flow_handle  *allowed_vlan;
	struct mlx5_flow_handle  *drop_rule;
};

struct mlx5_vport_info {
	u8                      mac[ETH_ALEN];
	u16                     vlan;
	u8                      qos;
	u64                     node_guid;
	int                     link_state;
	u32                     max_rate;
	bool                    spoofchk;
	bool                    trusted;
};

struct mlx5_vport {
	struct mlx5_core_dev    *dev;
	int                     vport;
	struct hlist_head       uc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct hlist_head       mc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct mlx5_flow_handle *promisc_rule;
	struct mlx5_flow_handle *allmulti_rule;
	struct work_struct      vport_change_handler;

	struct vport_ingress    ingress;
	struct vport_egress     egress;

	struct mlx5_vport_info  info;

	struct {
		bool            enabled;
		u32             esw_tsar_ix;
	} qos;

	bool                    enabled;
	u16                     enabled_events;
};

struct mlx5_l2_table {
	struct hlist_head l2_hash[MLX5_L2_ADDR_HASH_SIZE];
	u32                  size;
	unsigned long        *bitmap;
};

struct mlx5_eswitch_fdb {
	void *fdb;
	union {
		struct legacy_fdb {
			struct mlx5_flow_group *addr_grp;
			struct mlx5_flow_group *allmulti_grp;
			struct mlx5_flow_group *promisc_grp;
		} legacy;

		struct offloads_fdb {
			struct mlx5_flow_table *fdb;
			struct mlx5_flow_group *send_to_vport_grp;
			struct mlx5_flow_group *miss_grp;
			struct mlx5_flow_handle *miss_rule;
			int vlan_push_pop_refcount;
		} offloads;
	};
};

enum {
	SRIOV_NONE,
	SRIOV_LEGACY,
	SRIOV_OFFLOADS
};

struct mlx5_esw_sq {
	struct mlx5_flow_handle	*send_to_vport_rule;
	struct list_head	 list;
};

struct mlx5_neigh_update {
	struct rhashtable       neigh_ht;
	/* Save the neigh hash entries in a list in addition to the hash table
	 * (neigh_ht). In order to iterate easily over the neigh entries.
	 * Used for stats query.
	 */
	struct list_head	neigh_list;
	/* protect lookup/remove operations */
	spinlock_t              encap_lock;
	struct notifier_block   netevent_nb;
	struct delayed_work     neigh_stats_work;
	unsigned long           min_interval; /* jiffies */
};

struct mlx5_eswitch_rep {
	int		       (*load)(struct mlx5_eswitch *esw,
				       struct mlx5_eswitch_rep *rep);
	void		       (*unload)(struct mlx5_eswitch *esw,
					 struct mlx5_eswitch_rep *rep);
	u16		       vport;
	u8		       hw_id[ETH_ALEN];
	struct net_device      *netdev;

	struct mlx5_flow_handle *vport_rx_rule;
	struct list_head       vport_sqs_list;
	u16		       vlan;
	u32		       vlan_refcount;
	struct mlx5_neigh_update neigh_update;
	bool		       valid;
};

struct mlx5_esw_offload {
	struct mlx5_flow_table *ft_offloads;
	struct mlx5_flow_group *vport_rx_group;
	struct mlx5_eswitch_rep *vport_reps;
	DECLARE_HASHTABLE(encap_tbl, 8);
	u8 inline_mode;
	u64 num_flows;
	u8 encap;
};

struct mlx5_eswitch {
	struct mlx5_core_dev    *dev;
	struct mlx5_l2_table    l2_table;
	struct mlx5_eswitch_fdb fdb_table;
	struct hlist_head       mc_table[MLX5_L2_ADDR_HASH_SIZE];
	struct workqueue_struct *work_queue;
	struct mlx5_vport       *vports;
	int                     total_vports;
	int                     enabled_vports;
	/* Synchronize between vport change events
	 * and async SRIOV admin state changes
	 */
	struct mutex            state_lock;
	struct esw_mc_addr      *mc_promisc;

	struct {
		bool            enabled;
		u32             root_tsar_id;
	} qos;

	struct mlx5_esw_offload offloads;
	int                     mode;
};

void esw_offloads_cleanup(struct mlx5_eswitch *esw, int nvports);
int esw_offloads_init(struct mlx5_eswitch *esw, int nvports);

/* E-Switch API */
int mlx5_eswitch_init(struct mlx5_core_dev *dev);
void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw);
void mlx5_eswitch_attach(struct mlx5_eswitch *esw);
void mlx5_eswitch_detach(struct mlx5_eswitch *esw);
void mlx5_eswitch_vport_event(struct mlx5_eswitch *esw, struct mlx5_eqe *eqe);
int mlx5_eswitch_enable_sriov(struct mlx5_eswitch *esw, int nvfs, int mode);
void mlx5_eswitch_disable_sriov(struct mlx5_eswitch *esw);
int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       int vport, u8 mac[ETH_ALEN]);
int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 int vport, int link_state);
int mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				int vport, u16 vlan, u8 qos);
int mlx5_eswitch_set_vport_spoofchk(struct mlx5_eswitch *esw,
				    int vport, bool spoofchk);
int mlx5_eswitch_set_vport_trust(struct mlx5_eswitch *esw,
				 int vport_num, bool setting);
int mlx5_eswitch_set_vport_rate(struct mlx5_eswitch *esw,
				int vport, u32 max_rate);
int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  int vport, struct ifla_vf_info *ivi);
int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 int vport,
				 struct ifla_vf_stats *vf_stats);

struct mlx5_flow_spec;
struct mlx5_esw_flow_attr;

struct mlx5_flow_handle *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				struct mlx5_esw_flow_attr *attr);
void
mlx5_eswitch_del_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_handle *rule,
				struct mlx5_esw_flow_attr *attr);

struct mlx5_flow_handle *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, int vport, u32 tirn);

enum {
	SET_VLAN_STRIP	= BIT(0),
	SET_VLAN_INSERT	= BIT(1)
};

#define MLX5_FLOW_CONTEXT_ACTION_VLAN_POP  0x40
#define MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH 0x80

struct mlx5_neigh {
	struct net_device *neigh_dev;
	union {
		__be32	v4;
		struct in6_addr v6;
	} dst_ip;
	int family;
};

struct mlx5_neigh_hash_entry {
	struct rhash_head rhash_node;
	struct mlx5_neigh m_neigh;

	/* Save the neigh hash entry in a list on the representor in
	 * addition to the hash table. In order to iterate easily over the
	 * neighbour entries. Used for stats query.
	 */
	struct list_head neigh_list;

	/* encap list sharing the same neigh */
	struct list_head encap_list;

	/* valid only when the neigh reference is taken during
	 * neigh_update_work workqueue callback.
	 */
	struct neighbour *n;
	struct work_struct neigh_update_work;

	/* neigh hash entry can be deleted only when the refcount is zero.
	 * refcount is needed to avoid neigh hash entry removal by TC, while it's
	 * used by the neigh notifaction call.
	 */
	refcount_t refcnt;

	/* Save the last reported time offloaded trafic pass over one of the
	 * neigh hash entry flows. Use it to periodically update the neigh
	 * 'used' value and avoid neigh deleting by the kernel.
	 */
	unsigned long reported_lastuse;
};

enum {
	/* set when the encap entry is successfully offloaded into HW */
	MLX5_ENCAP_ENTRY_VALID     = BIT(0),
};

struct mlx5_encap_entry {
	/* neigh hash entry list of encaps sharing the same neigh */
	struct list_head encap_list;
	struct mlx5_neigh m_neigh;
	/* a node of the eswitch encap hash table which keeping all the encap entries */
	struct hlist_node encap_hlist;
	struct list_head flows;
	u32 encap_id;
	struct ip_tunnel_info tun_info;
	unsigned char h_dest[ETH_ALEN];	/* destination eth addr	*/

	struct net_device *out_dev;
	int tunnel_type;
	u8 flags;
	char *encap_header;
	int encap_size;
};

struct mlx5_esw_flow_attr {
	struct mlx5_eswitch_rep *in_rep;
	struct mlx5_eswitch_rep *out_rep;

	int	action;
	u16	vlan;
	bool	vlan_handled;
	struct mlx5_encap_entry *encap;
	struct mlx5_flow_spec *encap_spec;
};

int mlx5_eswitch_sqs2vport_start(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep,
				 u16 *sqns_array, int sqns_num);
void mlx5_eswitch_sqs2vport_stop(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep);

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode);
int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode);
int mlx5_devlink_eswitch_inline_mode_set(struct devlink *devlink, u8 mode);
int mlx5_devlink_eswitch_inline_mode_get(struct devlink *devlink, u8 *mode);
int mlx5_eswitch_inline_mode_get(struct mlx5_eswitch *esw, int nvfs, u8 *mode);
int mlx5_devlink_eswitch_encap_mode_set(struct devlink *devlink, u8 encap);
int mlx5_devlink_eswitch_encap_mode_get(struct devlink *devlink, u8 *encap);
void mlx5_eswitch_register_vport_rep(struct mlx5_eswitch *esw,
				     int vport_index,
				     struct mlx5_eswitch_rep *rep);
void mlx5_eswitch_unregister_vport_rep(struct mlx5_eswitch *esw,
				       int vport_index);
struct net_device *mlx5_eswitch_get_uplink_netdev(struct mlx5_eswitch *esw);

int mlx5_eswitch_add_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr);
int mlx5_eswitch_del_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr);
int __mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				  int vport, u16 vlan, u8 qos, u8 set_flags);

#define MLX5_DEBUG_ESWITCH_MASK BIT(3)

#define esw_info(dev, format, ...)				\
	pr_info("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_warn(dev, format, ...)				\
	pr_warn("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_debug(dev, format, ...)				\
	mlx5_core_dbg_mask(dev, MLX5_DEBUG_ESWITCH_MASK, format, ##__VA_ARGS__)
#endif /* __MLX5_ESWITCH_H__ */
