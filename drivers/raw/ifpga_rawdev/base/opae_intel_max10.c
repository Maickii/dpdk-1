/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

#include "opae_intel_max10.h"

static struct intel_max10_device *g_max10;

int max10_reg_read(unsigned int reg, unsigned int *val)
{
	if (!g_max10)
		return -ENODEV;

	return spi_transaction_read(g_max10->spi_tran_dev,
			reg, 4, (unsigned char *)val);
}

int max10_reg_write(unsigned int reg, unsigned int val)
{
	if (!g_max10)
		return -ENODEV;

	return spi_transaction_write(g_max10->spi_tran_dev,
			reg, 4, (unsigned char *)&val);
}

struct resource mdio_resource[INTEL_MAX10_MAX_MDIO_DEVS] = {
	{
		.start = 0x200100,
		.end = 0x2001ff,
	},
	{
		.start = 0x200200,
		.end = 0x2002ff,
	},
};

struct intel_max10_device *
intel_max10_device_probe(struct altera_spi_device *spi,
		int chipselect)
{
	struct intel_max10_device *dev;
	int i;

	dev = opae_malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	dev->spi_master = spi;

	dev->spi_tran_dev = spi_transaction_init(spi, chipselect);
	if (!dev->spi_tran_dev) {
		dev_err(dev, "%s spi tran init fail\n", __func__);
		goto free_dev;
	}

	g_max10 = dev;

	for (i = 0; i < INTEL_MAX10_MAX_MDIO_DEVS; i++) {
		dev->mdio[i] = altera_mdio_probe(i, mdio_resource[i].start,
				mdio_resource[i].end, dev->spi_tran_dev);
		if (!dev->mdio[i]) {
			dev_err(dev, "%s mido init fail\n", __func__);
			goto mdio_fail;
		}
	}

	/* FIXME: should read this info from MAX10 device table */
	dev->num_retimer = INTEL_MAX10_MAX_MDIO_DEVS;
	dev->num_port = PKVL_NUMBER_PORTS;

	return dev;

mdio_fail:
	for (i = 0; i < INTEL_MAX10_MAX_MDIO_DEVS; i++)
		if (dev->mdio[i])
			opae_free(dev->mdio[i]);

	spi_transaction_remove(dev->spi_tran_dev);
free_dev:
	g_max10 = NULL;
	opae_free(dev);

	return NULL;
}

int intel_max10_device_remove(struct intel_max10_device *dev)
{
	int i;

	if (!dev)
		return 0;

	if (dev->spi_tran_dev)
		spi_transaction_remove(dev->spi_tran_dev);

	for (i = 0; i < INTEL_MAX10_MAX_MDIO_DEVS; i++)
		if (dev->mdio[i])
			altera_mdio_release(dev->mdio[i]);

	g_max10 = NULL;

	opae_free(dev);

	return 0;
}
