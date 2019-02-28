/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

#include "opae_osdep.h"
#include "opae_phy_group.h"

static int phy_indirect_wait(struct phy_group_device *dev)
{
	int retry = 0;
	u64 val;

	while (!((val = opae_readq(dev->base + PHY_GROUP_STAT)) &
				STAT_DATA_VALID)) {
		if (retry++ > 1000)
			return -EBUSY;

		udelay(1);
	}

	return 0;
}

static void phy_indirect_write(struct phy_group_device *dev, u8 entry,
		u16 addr, u32 value)
{
	u64 ctrl;

	ctrl = CMD_RD << CTRL_COMMAND_SHIFT |
		(entry & CTRL_PHY_NUM_MASK) << CTRL_PHY_NUM_SHIFT |
		(addr & CTRL_PHY_ADDR_MASK) << CTRL_PHY_ADDR_SHIFT |
		(value & CTRL_WRITE_DATA_MASK);

	opae_writeq(ctrl, dev->base + PHY_GROUP_CTRL);
}

static int phy_indirect_read(struct phy_group_device *dev, u8 entry, u16 addr,
		u32 *value)
{
	u64 tmp;
	u64 ctrl = 0;

	ctrl = CMD_RD << CTRL_COMMAND_SHIFT |
		(entry & CTRL_PHY_NUM_MASK) << CTRL_PHY_NUM_SHIFT |
		(addr & CTRL_PHY_ADDR_MASK) << CTRL_PHY_ADDR_SHIFT;
	opae_writeq(ctrl, dev->base + PHY_GROUP_CTRL);

	if (phy_indirect_wait(dev))
		return -ETIMEDOUT;

	tmp = opae_readq(dev->base + PHY_GROUP_STAT);
	*value = tmp & STAT_READ_DATA_MASK;

	return 0;
}

int phy_group_read_reg(struct phy_group_device *dev, u8 entry,
		u16 addr, u32 *value)
{
	return phy_indirect_read(dev, entry, addr, value);
}

int phy_group_write_reg(struct phy_group_device *dev, u8 entry,
		u16 addr, u32 value)
{
	phy_indirect_write(dev, entry, addr, value);

	return 0;
}

struct phy_group_device *phy_group_probe(void *base)
{
	struct phy_group_device *dev;

	dev = opae_malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	dev->base = (u8 *)base;

	dev->info.info = opae_readq(dev->base + PHY_GROUP_INFO);
	dev->group_index = dev->info.group_number;
	dev->entries = dev->info.num_phys;
	dev->speed = dev->info.speed;
	dev->entry_size = PHY_GROUP_ENTRY_SIZE;

	return dev;
}
