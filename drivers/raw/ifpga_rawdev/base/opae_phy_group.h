#ifndef _OPAE_PHY_MAC_H
#define _OPAE_PHY_MAC_H

#include "opae_osdep.h"

#define MAX_PHY_GROUP_DEVICES  8
#define PHY_GROUP_ENTRY_SIZE 0x1000

#define PHY_GROUP_INFO 0x8
#define PHY_GROUP_CTRL 0x10
#define CTRL_COMMAND_SHIFT 62
#define CMD_RD 0x1UL
#define CMD_WR 0x2UL
#define CTRL_PHY_NUM_SHIFT 43
#define CTRL_PHY_NUM_MASK GENMASK_ULL(45, 43)
#define CTRL_RESET BIT_ULL(42)
#define CTRL_PHY_ADDR_SHIFT 32
#define CTRL_PHY_ADDR_MASK GENMASK_ULL(41, 32)
#define CTRL_WRITE_DATA_MASK GENMASK_ULL(31, 0)
#define PHY_GROUP_STAT 0x18
#define STAT_DATA_VALID BIT_ULL(32)
#define STAT_READ_DATA_MASK GENMASK_ULL(31, 0)

struct phy_group_info {
	union {
		u64 info;
		struct {
			u8 group_number:8;
			u8 num_phys:8;
			u8 speed:8;
			u8 direction:1;
			u64 resvd:39;
		};
	};
};

struct phy_group_device {
	u8 *base;
	struct phy_group_info info;
	u32 group_index;
	u32 entries;
	u32 speed;
	u32 entry_size;
	u32 flags;
};

struct phy_group_device *phy_group_probe(void *base);
int phy_group_write_reg(struct phy_group_device *dev,
		u8 entry, u16 addr, u32 value);
int phy_group_read_reg(struct phy_group_device *dev,
		u8 entry, u16 addr, u32 *value);

#endif
