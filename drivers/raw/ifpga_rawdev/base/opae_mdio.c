/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#include "opae_osdep.h"
#include "opae_spi.h"
#include "opae_mdio.h"
#include "opae_intel_max10.h"

#define PHY_MAX_ADDR 32
#define MAX_NUM_IDS  8
#define MDIO_PHYSID1 2
#define MDIO_PHYSID2 3
#define MDIO_DEVS2   6
#define MDIO_DEVS1   5

static int max10_mdio_reg_read(struct altera_mdio_dev *dev,
		unsigned int reg, unsigned int *val)
{
	struct spi_transaction_dev *spi_tran_dev =
		dev->sub_dev;

	if (!spi_tran_dev)
		return -ENODEV;

	return spi_transaction_read(spi_tran_dev,
			reg, 4, (unsigned char *)val);
}

int altera_mdio_read(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 port_addr, u32 reg, u32 *value)
{
	int ret;
	struct altera_mdio_addr mdio_addr = {.csr = 0};

	if (!dev)
		return -ENODEV;

	mdio_addr.devad = dev_addr;
	mdio_addr.prtad = port_addr;
	mdio_addr.regad = reg;

	dev_debug(dev, "%s reg=0x%x, dev:%x, port:%x, reg:0x%x\n", __func__,
			mdio_addr.csr, dev_addr, port_addr, reg);

	ret = max10_reg_write(dev->start + ALTERA_MDIO_ADDRESS_OFST,
			mdio_addr.csr);
	if (ret)
		return -EIO;

	return max10_mdio_reg_read(dev, dev->start + ALTERA_MDIO_DATA_OFST,
			value);
}

int altera_mdio_write(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 port_addr, u32 reg, u32 value)
{
	int ret;
	struct altera_mdio_addr mdio_addr = {.csr = 0};

	if (!dev)
		return -ENODEV;

	mdio_addr.devad = dev_addr;
	mdio_addr.prtad = port_addr;
	mdio_addr.regad = reg;

	ret = max10_reg_write(dev->start + ALTERA_MDIO_ADDRESS_OFST,
			mdio_addr.csr);
	if (ret)
		return -EIO;

	return max10_reg_write(dev->start + ALTERA_MDIO_DATA_OFST,
			value);
}

int pkvl_reg_read(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 reg, u32 *val)
{
	int port_id = dev->port_id;

	if (port_id < 0)
		return -ENODEV;

	return altera_mdio_read(dev, dev_addr, port_id, reg, val);
}

int pkvl_reg_write(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 reg, u32 val)
{
	int port_id = dev->port_id;

	if (port_id < 0)
		return -ENODEV;

	return altera_mdio_write(dev, dev_addr, port_id, reg, val);
}

static int pkvl_reg_set_mask(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 reg, u32 mask, u32 val)
{
	int ret;
	u32 v;

	ret = pkvl_reg_read(dev, dev_addr, reg, &v);
	if (ret)
		return -EIO;

	v = (v&~mask) | (val & mask);

	return pkvl_reg_write(dev, dev_addr, reg, v);
}

static int get_phy_package_id(struct altera_mdio_dev *dev,
		int addr, int dev_addr, int *id)
{
	int ret;
	u32 val = 0;

	ret = altera_mdio_read(dev, dev_addr, addr, MDIO_DEVS2, &val);
	if (ret)
		return -EIO;

	*id = (val & 0xffff) << 16;

	ret = altera_mdio_read(dev, dev_addr, addr, MDIO_DEVS1, &val);
	if (ret)
		return -EIO;

	*id |= (val & 0xffff);

	return 0;
}

static int get_phy_device_id(struct altera_mdio_dev *dev,
		int addr, int dev_addr, int *id)
{
	int ret;
	u32 val = 0;

	ret = altera_mdio_read(dev, dev_addr, addr, MDIO_PHYSID1, &val);
	if (ret)
		return -EIO;

	*id = (val & 0xffff) << 16;

	ret = altera_mdio_read(dev, dev_addr, addr, MDIO_PHYSID2, &val);
	if (ret)
		return -EIO;

	*id |= (val & 0xffff);

	return 0;
}

static int get_phy_c45_ids(struct altera_mdio_dev *dev,
		int addr, int *phy_id, int *device_id)
{
	int i;
	int ret;
	int id;

	for (i = 1; i < MAX_NUM_IDS; i++) {
		ret = get_phy_package_id(dev, addr, i, phy_id);
		if (ret)
			return -EIO;

		if ((*phy_id & 0x1fffffff) != 0x1fffffff)
			break;
	}

	ret = get_phy_device_id(dev, addr, 1, &id);
	if (ret)
		return -EIO;

	*device_id = id;

	return 0;
}

static int mdio_phy_scan(struct altera_mdio_dev *dev, int *port_id,
		int *phy_id, int *device_id)
{
	int i;
	int ret;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		ret = get_phy_c45_ids(dev, i, phy_id, device_id);
		if (ret)
			return -EIO;

		if ((*phy_id & 0x1fffffff) != 0x1fffffff) {
			*port_id = i;
			break;
		}
	}

	return 0;
}

#define PKVL_READ pkvl_reg_read
#define PKVL_WRITE pkvl_reg_write
#define PKVL_SET_MASK pkvl_reg_set_mask

static int pkvl_check_smbus_cmd(struct altera_mdio_dev *dev)
{
	int retry = 0;
	u32 val;

	for (retry = 0; retry < 10; retry++) {
		PKVL_READ(dev, 31, 0xf443, &val);
		if ((val & 0x3) == 0)
			break;
		opae_udelay(1);
	}

	if (val & 0x3) {
		dev_err(dev, "pkvl execute indirect smbus cmd fail\n");
		return -EBUSY;
	}

	return 0;
}

static int pkvl_execute_smbus_cmd(struct altera_mdio_dev *dev)
{
	int ret;

	ret = pkvl_check_smbus_cmd(dev);
	if (ret)
		return ret;

	PKVL_WRITE(dev, 31, 0xf443, 0x1);

	ret = pkvl_check_smbus_cmd(dev);
	if (ret)
		return ret;

	return 0;
}

static int pkvl_indirect_smbus_set(struct altera_mdio_dev *dev,
		u32 addr, u32 reg, u32 hv, u32 lv, u32 *v)
{
	int ret;

	PKVL_WRITE(dev, 31, 0xf441, 0x21);
	PKVL_WRITE(dev, 31, 0xf442,
			((addr & 0xff) << 8) | (reg & 0xff));
	PKVL_WRITE(dev, 31, 0xf445, hv);
	PKVL_WRITE(dev, 31, 0xf444, lv);
	PKVL_WRITE(dev, 31, 0xf440, 0);

	ret = pkvl_execute_smbus_cmd(dev);
	if (ret)
		return ret;

	PKVL_READ(dev, 31, 0xf446, v);
	PKVL_WRITE(dev, 31, 0xf443, 0);

	return 0;
}

static int pkvl_serdes_intr_set(struct altera_mdio_dev *dev,
		u32 reg, u32 hv, u32 lv)
{
	u32 addr;
	u32 v;
	int ret;

	addr = (reg & 0xff00) >> 8;

	ret = pkvl_indirect_smbus_set(dev, addr, 0x3, hv, lv, &v);
	if (ret)
		return ret;

	if ((v & 0x7) != 1) {
		dev_err(dev, "%s(0x%x, 0x%x, 0x%x) fail\n",
				__func__, reg, hv, lv);
		return -EBUSY;
	}

	return 0;
}

#define PKVL_SERDES_SET pkvl_serdes_intr_set

static int pkvl_set_line_side_mode(struct altera_mdio_dev *dev,
		int port, int mode)
{
	u32 val = 0;

	/* check PKVL exist */
	PKVL_READ(dev, 1, 0, &val);
	if (val == 0 || val == 0xffff) {
		dev_err(dev, "reading reg 0x0 from PKVL fail\n");
		return -ENODEV;
	}

	PKVL_WRITE(dev, 31, 0xf003, 0);
	PKVL_WRITE(dev, 3, 0x2000 + 0x200*port,
			0x2040);
	PKVL_SET_MASK(dev, 7, 0x200*port, 1<<12, 0);
	PKVL_SET_MASK(dev, 7, 0x11+0x200*port,
			0xf3a0, 0);
	PKVL_SET_MASK(dev, 7, 0x8014+0x200*port,
			0x330, 0);
	PKVL_WRITE(dev, 7, 0x12+0x200*port, 0);
	PKVL_WRITE(dev, 7, 0x8015+0x200*port, 0);
	PKVL_SET_MASK(dev, 3, 0xf0ba, 0x8000 | (0x800<<port),
			0x8000);
	PKVL_SET_MASK(dev, 3, 0xf0a6, 0x8000 | (0x800<<port),
			0x8000);
	PKVL_WRITE(dev, 3, 0xf378, 0);
	PKVL_WRITE(dev, 3, 0xf258 + 0x80 * port, 0);
	PKVL_WRITE(dev, 3, 0xf259 + 0x80 * port, 0);
	PKVL_WRITE(dev, 3, 0xf25a + 0x80 * port, 0);
	PKVL_WRITE(dev, 3, 0xf25b + 0x80 * port, 0);
	PKVL_SET_MASK(dev, 3, 0xf26f + 0x80 * port,
			3<<14, 0);

	PKVL_SET_MASK(dev, 3, 0xf060, 1<<2, 0);
	PKVL_WRITE(dev, 3, 0xf053, 0);
	PKVL_WRITE(dev, 3, 0xf056, 0);
	PKVL_WRITE(dev, 3, 0xf059, 0);
	PKVL_WRITE(dev, 7, 0x8200, 0);
	PKVL_WRITE(dev, 7, 0x8400, 0);
	PKVL_WRITE(dev, 7, 0x8600, 0);
	PKVL_WRITE(dev, 3, 0xf0e7, 0);

	if (mode == MXD_10GB) {
		PKVL_SET_MASK(dev, 3, 0xf25c + 0x80 * port,
				0x2, 0x2);
		PKVL_WRITE(dev, 3, 0xf220 + 0x80 * port, 0x1918);
		PKVL_WRITE(dev, 3, 0xf221 + 0x80 * port, 0x1819);
		PKVL_WRITE(dev, 3, 0xf230 + 0x80 * port, 0x7);
		PKVL_WRITE(dev, 3, 0xf231 + 0x80 * port, 0xaff);
		PKVL_WRITE(dev, 3, 0xf232 + 0x80 * port, 0);
		PKVL_WRITE(dev, 3, 0xf250 + 0x80 * port, 0x1111);
		PKVL_WRITE(dev, 3, 0xf251 + 0x80 * port, 0x1111);
		PKVL_SET_MASK(dev, 3, 0xf258 + 0x80 * port,
				0x7, 0x7);
	}

	PKVL_SET_MASK(dev, 3, 0xf25c + 0x80 * port, 0x2, 0x2);
	PKVL_WRITE(dev, 3, 0xf22b + 0x80 * port, 0x1918);
	PKVL_WRITE(dev, 3, 0xf246 + 0x80 * port, 0x4033);
	PKVL_WRITE(dev, 3, 0xf247 + 0x80 * port, 0x4820);
	PKVL_WRITE(dev, 3, 0xf255 + 0x80 * port, 0x1100);
	PKVL_SET_MASK(dev, 3, 0xf259 + 0x80 * port, 0xc0,
			0xc0);

	if (port == 0) {
		if (mode == MXD_10GB) {
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0x9004);
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0x9800);
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0xa002);
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0xa800);
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0xb012);
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0xb800);
		} else if (mode == MXD_25GB) {
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0x9800);
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0xa809);
			PKVL_SERDES_SET(dev, 0x503, 0x3d, 0xb800);
		}
	}

	/* last step */
	PKVL_WRITE(dev, 3, 0xf000 + port, 0x8020 | mode);
	PKVL_READ(dev, 3, 0xf000 + port, &val);
	dev_info(dev, "PKVL:%d port:%d line side mode : 0x%x\n",
			dev->index, port, val);
	return 0;
}

static int pkvl_set_host_side_mode(struct altera_mdio_dev *dev,
		int port, int mode)
{
	u32 val = 0;

	PKVL_WRITE(dev, 4, 0x2000 + 0x200 * port, 0x2040);
	PKVL_SET_MASK(dev, 7, 0x1000 + 0x200 * port,
			1<<12, 0);
	PKVL_SET_MASK(dev, 7, 0x1011 + 0x200 * port,
			0xf3a0, 0);
	PKVL_SET_MASK(dev, 7, 0x9014 + 0x200 * port,
			0x330, 0);
	PKVL_WRITE(dev, 7, 0x1012 + 0x200 * port, 0);
	PKVL_WRITE(dev, 7, 0x9015 + 0x200 * port, 0);
	PKVL_SET_MASK(dev, 4, 0xf0ba, 0x8000 | (0x800 << port),
			0x8000);
	PKVL_SET_MASK(dev, 4, 0xf0a6, 0x8000 | (0x800 << port),
			0x8000);
	PKVL_WRITE(dev, 4, 0xf378, 0);
	PKVL_WRITE(dev, 4, 0xf258 + 0x80 * port, 0);
	PKVL_WRITE(dev, 4, 0xf259 + 0x80 * port, 0);
	PKVL_WRITE(dev, 4, 0xf25a + 0x80 * port, 0);
	PKVL_WRITE(dev, 4, 0xf25b + 0x80 * port, 0);
	PKVL_SET_MASK(dev, 4, 0xf26f + 0x80 * port,
			3<<14, 0);
	PKVL_SET_MASK(dev, 4, 0xf060, 1<<2, 0);
	PKVL_WRITE(dev, 4, 0xf053, 0);
	PKVL_WRITE(dev, 4, 0xf056, 0);
	PKVL_WRITE(dev, 4, 0xf059, 0);
	PKVL_WRITE(dev, 7, 0x9200, 0);
	PKVL_WRITE(dev, 7, 0x9400, 0);
	PKVL_WRITE(dev, 7, 0x9600, 0);
	PKVL_WRITE(dev, 4, 0xf0e7, 0);

	if (mode == MXD_10GB) {
		PKVL_SET_MASK(dev, 4, 0xf25c + 0x80 * port,
				0x2, 0x2);
		PKVL_WRITE(dev, 4, 0xf220 + 0x80 * port, 0x1918);
		PKVL_WRITE(dev, 4, 0xf221 + 0x80 * port, 0x1819);
		PKVL_WRITE(dev, 4, 0xf230 + 0x80 * port, 0x7);
		PKVL_WRITE(dev, 4, 0xf231 + 0x80 * port, 0xaff);
		PKVL_WRITE(dev, 4, 0xf232 + 0x80 * port, 0);
		PKVL_WRITE(dev, 4, 0xf250 + 0x80 * port, 0x1111);
		PKVL_WRITE(dev, 4, 0xf251 + 0x80 * port, 0x1111);
		PKVL_SET_MASK(dev, 4, 0xf258 + 0x80 * port,
				0x7, 0x7);
	}

	PKVL_SET_MASK(dev, 4, 0xf25c + 0x80 * port, 0x2, 0x2);
	PKVL_WRITE(dev, 4, 0xf22b + 0x80 * port, 0x1918);
	PKVL_WRITE(dev, 4, 0xf246 + 0x80 * port, 0x4033);
	PKVL_WRITE(dev, 4, 0xf247 + 0x80 * port, 0x4820);
	PKVL_WRITE(dev, 4, 0xf255 + 0x80 * port, 0x1100);
	PKVL_SET_MASK(dev, 4, 0xf259 + 0x80 * port, 0xc0, 0xc0);

	PKVL_SERDES_SET(dev, 0x103 + 0x100 * port, 0x3d, 0x9004);
	PKVL_SERDES_SET(dev, 0x103 + 0x100 * port, 0x3d, 0xa002);
	PKVL_SERDES_SET(dev, 0x103 + 0x100 * port, 0x3d, 0xb012);

	PKVL_WRITE(dev, 4, 0xf000 + port, 0x8020 | mode);
	PKVL_READ(dev, 4, 0xf000 + port, &val);

	dev_info(dev, "PKVL:%d port:%d host side mode:0x%x\n",
			dev->index, port, val);

	return 0;
}

int pkvl_set_speed_mode(struct altera_mdio_dev *dev, int port, int mode)
{
	int ret;

	ret = pkvl_set_line_side_mode(dev, port, mode);
	if (ret)
		return ret;

	return pkvl_set_host_side_mode(dev, port, mode);
}

int pkvl_get_port_speed_status(struct altera_mdio_dev *dev,
		int port, unsigned int *speed)
{
	int ret;

	ret = pkvl_reg_read(dev, 4, 0xf000 + port, speed);
	if (ret)
		return ret;

	*speed = *speed & 0x7;

	return 0;
}

int pkvl_get_port_line_link_status(struct altera_mdio_dev *dev,
		int port, unsigned int *link)
{
	int ret;

	ret = pkvl_reg_read(dev, 3, 0xa002 + 0x200 * port, link);
	if (ret)
		return ret;

	*link = (*link & (1<<2)) ? 1:0;

	return 0;
}

int pkvl_get_port_host_link_status(struct altera_mdio_dev *dev,
		int port, unsigned int *link)
{
	int ret;

	ret = pkvl_reg_read(dev, 4, 0xa002 + 0x200 * port, link);
	if (ret)
		return ret;

	*link = (*link & (1<<2)) ? 1:0;

	return 0;
}

static struct altera_mdio_dev *altera_spi_mdio_init(int index, u32 start,
		u32 end, void *sub_dev)
{
	struct altera_mdio_dev *dev;
	int ret;
	int port_id = 0;
	int phy_id = 0;
	int device_id = 0;

	dev = opae_malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	dev->sub_dev = sub_dev;
	dev->start = start;
	dev->end = end;
	dev->port_id = -1;
	dev->index = index;

	ret = mdio_phy_scan(dev, &port_id, &phy_id, &device_id);
	if (ret) {
		dev_err(dev, "Cannot found Phy Device on MIDO Bus\n");
		opae_free(dev);
		return NULL;
	}

	dev->port_id = port_id;
	dev->phy_device_id = device_id;

	dev_info(dev, "Found MDIO Phy Device %d, port_id=%d, phy_id=0x%x, device_id=0x%x\n",
			index, port_id, phy_id, device_id);

	return dev;
}

struct altera_mdio_dev *altera_mdio_probe(int index, u32 start, u32 end,
		void *sub_dev)
{
	return altera_spi_mdio_init(index, start, end, sub_dev);
}

void altera_mdio_release(struct altera_mdio_dev *dev)
{
	if (dev)
		opae_free(dev);
}
