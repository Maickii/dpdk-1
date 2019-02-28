/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#ifndef _OPAE_MDIO_H_
#define _OPAE_MDIO_H_

#include "opae_osdep.h"

/* retimer speed */
enum retimer_speed {
	MXD_1GB = 0,
	MXD_2_5GB,
	MXD_5GB,
	MXD_10GB,
	MXD_25GB,
	MXD_40GB,
	MXD_100GB,
	MXD_SPEED_UNKNOWN,
};

/* retimer info */
struct opae_retimer_info {
	int num_retimer;
	int num_port;
	enum retimer_speed support_speed;
};

/* retimer status*/
struct opae_retimer_status {
	enum retimer_speed speed;
	unsigned int line_link;
	unsigned int host_link;
};

/**
 * read MDIO need about 62us delay, SPI keep
 * reading before get valid data, so we let
 * SPI master read more than 100 bytes
 */
#define MDIO_READ_DELAY 100

/* register offset definition */
#define ALTERA_MDIO_DATA_OFST            0x80
#define ALTERA_MDIO_ADDRESS_OFST         0x84

struct altera_mdio_dev;

struct altera_mdio_dev {
	void *sub_dev;   /* sub dev link to spi tran device*/
	u32 start;  /* start address*/
	u32 end;    /* end of address */
	int index;
	int port_id;
	int phy_device_id;
};

struct altera_mdio_addr {
	union {
		unsigned int csr;
		struct{
			u8 devad:5;
			u8 rsvd1:3;
			u8 prtad:5;
			u8 rsvd2:3;
			u16 regad:16;
		};
	};
};

/* function declaration */
struct altera_mdio_dev *altera_mdio_probe(int index, u32 start,
		u32 end, void *sub_dev);
void altera_mdio_release(struct altera_mdio_dev *dev);
int altera_mdio_read(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 port_id, u32 reg, u32 *value);
int altera_mdio_write(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 port_id, u32 reg, u32 value);
int pkvl_reg_read(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 reg, u32 *value);
int pkvl_reg_write(struct altera_mdio_dev *dev, u32 dev_addr,
		u32 reg, u32 value);
int pkvl_set_speed_mode(struct altera_mdio_dev *dev, int port, int mode);
int pkvl_get_port_speed_status(struct altera_mdio_dev *dev,
		int port, unsigned int *speed);
int pkvl_get_port_line_link_status(struct altera_mdio_dev *dev,
		int port, unsigned int *link);
int pkvl_get_port_host_link_status(struct altera_mdio_dev *dev,
		int port, unsigned int *link);
#endif /* _OPAE_MDIO_H_ */
