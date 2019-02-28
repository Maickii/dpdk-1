/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#ifndef _OPAE_INTEL_MAX10_H_
#define _OPAE_INTEL_MAX10_H_

#include "opae_osdep.h"
#include "opae_spi.h"
#include "opae_mdio.h"

#define INTEL_MAX10_MAX_MDIO_DEVS 2
#define PKVL_NUMBER_PORTS  4

struct intel_max10_device {
	struct altera_spi_device *spi_master;
	struct spi_transaction_dev *spi_tran_dev;
	struct altera_mdio_dev *mdio[INTEL_MAX10_MAX_MDIO_DEVS];
	int num_retimer; /* number of retimer */
	int num_port;   /* number of ports in retimer */
};

struct resource {
	u32 start;
	u32 end;
	u32 flags;
};

int max10_reg_read(unsigned int reg, unsigned int *val);
int max10_reg_write(unsigned int reg, unsigned int val);
struct intel_max10_device *
intel_max10_device_probe(struct altera_spi_device *spi,
		int chipselect);
int intel_max10_device_remove(struct intel_max10_device *dev);

#endif
