/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

#include "opae_osdep.h"
#include "opae_spi.h"

static void spi_indirect_write(struct altera_spi_device *dev, u32 reg,
		u32 value)
{
	u64 ctrl;

	opae_writeq(value & WRITE_DATA_MASK, dev->regs + SPI_WRITE);

	ctrl = CTRL_W | (reg >> 2);
	opae_writeq(ctrl, dev->regs + SPI_CTRL);
}

static u32 spi_indirect_read(struct altera_spi_device *dev, u32 reg)
{
	u64 tmp;
	u64 ctrl;
	u32 value;

	ctrl = CTRL_R | (reg >> 2);
	opae_writeq(ctrl, dev->regs + SPI_CTRL);

	/**
	 *  FIXME: Read one more time to avoid HW timing issue. This is
	 *  a short term workaround solution, and must be removed once
	 *  hardware fixing is done.
	 */
	tmp = opae_readq(dev->regs + SPI_READ);
	tmp = opae_readq(dev->regs + SPI_READ);

	value = (u32)tmp;

	return value;
}

void spi_cs_activate(struct altera_spi_device *dev, unsigned int chip_select)
{
	spi_indirect_write(dev, ALTERA_SPI_SLAVE_SEL, 1 << chip_select);
	spi_indirect_write(dev, ALTERA_SPI_CONTROL, ALTERA_SPI_CONTROL_SSO_MSK);
}

void spi_cs_deactivate(struct altera_spi_device *dev)
{
	spi_indirect_write(dev, ALTERA_SPI_CONTROL, 0);
}

static void spi_flush_rx(struct altera_spi_device *dev)
{
	if (spi_indirect_read(dev, ALTERA_SPI_STATUS) &
			ALTERA_SPI_STATUS_RRDY_MSK)
		spi_indirect_read(dev, ALTERA_SPI_RXDATA);
}

int spi_read(struct altera_spi_device *dev, unsigned int bytes, void *buffer)
{
	char data;
	char *rxbuf = buffer;

	if (bytes <= 0 || !rxbuf)
		return -EINVAL;

	/* empty read buffer */
	spi_flush_rx(dev);

	while (bytes--) {
		while (!(spi_indirect_read(dev, ALTERA_SPI_STATUS) &
				ALTERA_SPI_STATUS_RRDY_MSK))
			;
		data = spi_indirect_read(dev, ALTERA_SPI_RXDATA);
		if (buffer)
			*rxbuf++ = data;
	}

	return 0;
}

int spi_write(struct altera_spi_device *dev, unsigned int bytes, void *buffer)
{
	unsigned char data;
	char *txbuf = buffer;

	if (bytes <= 0 || !txbuf)
		return -EINVAL;

	while (bytes--) {
		while (!(spi_indirect_read(dev, ALTERA_SPI_STATUS) &
				ALTERA_SPI_STATUS_TRDY_MSK))
			;
		data = *txbuf++;
		spi_indirect_write(dev, ALTERA_SPI_TXDATA, data);
	}

	return 0;
}

static unsigned int spi_write_bytes(struct altera_spi_device *dev, int count)
{
	unsigned int val = 0;
	u16 *p16;
	u32 *p32;

	if (dev->txbuf) {
		switch (dev->data_width) {
		case 1:
			val = dev->txbuf[count];
			break;
		case 2:
			p16 = (u16 *)(dev->txbuf + 2*count);
			val = *p16;
			if (dev->endian == SPI_BIG_ENDIAN)
				val = cpu_to_be16(val);
			break;
		case 4:
			p32 = (u32 *)(dev->txbuf + 4*count);
			val = *p32;
			if (dev->endian == SPI_BIG_ENDIAN)
				val = (val);
			break;
		}
	}

	return val;
}

static void spi_fill_readbuffer(struct altera_spi_device *dev,
		unsigned int value, int count)
{
	u16 *p16;
	u32 *p32;

	if (dev->rxbuf) {
		switch (dev->data_width) {
		case 1:
			dev->rxbuf[count] = value;
			break;
		case 2:
			p16 = (u16 *)(dev->rxbuf + 2*count);
			if (dev->endian == SPI_BIG_ENDIAN)
				*p16 = cpu_to_be16((u16)value);
			else
				*p16 = (u16)value;
			break;
		case 4:
			p32 = (u32 *)(dev->rxbuf + 4*count);
			if (dev->endian == SPI_BIG_ENDIAN)
				*p32 = cpu_to_be32(value);
			else
				*p32 = value;
			break;
		}
	}
}

static int spi_txrx(struct altera_spi_device *dev)
{
	unsigned int count = 0;
	unsigned int rxd;
	unsigned int tx_data;
	unsigned int status;
	int retry = 0;

	while (count < dev->len) {
		tx_data = spi_write_bytes(dev, count);
		spi_indirect_write(dev, ALTERA_SPI_TXDATA, tx_data);

		while (1) {
			status = spi_indirect_read(dev, ALTERA_SPI_STATUS);
			if (status & ALTERA_SPI_STATUS_RRDY_MSK)
				break;
			if (retry++ > SPI_MAX_RETRY) {
				dev_err(dev, "%s, read timeout\n", __func__);
				return -EBUSY;
			}
		}

		rxd = spi_indirect_read(dev, ALTERA_SPI_RXDATA);
		spi_fill_readbuffer(dev, rxd, count);

		count++;
	}

	return 0;
}

int spi_command(struct altera_spi_device *dev, unsigned int chip_select,
		unsigned int wlen, void *wdata,
		unsigned int rlen, void *rdata)
{
	if (((wlen > 0) && !wdata) || ((rlen > 0) && !rdata)) {
		dev_err(dev, "error on spi command checking\n");
		return -EINVAL;
	}

	wlen = wlen / dev->data_width;
	rlen = rlen / dev->data_width;

	/* flush rx buffer */
	spi_flush_rx(dev);

	// TODO: GET MUTEX LOCK
	spi_cs_activate(dev, chip_select);
	if (wlen) {
		dev->txbuf = wdata;
		dev->rxbuf = rdata;
		dev->len = wlen;
		spi_txrx(dev);
	}
	if (rlen) {
		dev->rxbuf = rdata;
		dev->txbuf = NULL;
		dev->len = rlen;
		spi_txrx(dev);
	}
	spi_cs_deactivate(dev);
	// TODO: RELEASE MUTEX LOCK
	return 0;
}

struct altera_spi_device *altera_spi_init(void *base)
{
	struct altera_spi_device *spi_dev =
		opae_malloc(sizeof(struct altera_spi_device));

	if (!spi_dev)
		return NULL;

	spi_dev->regs = base;

	spi_dev->spi_param.info = opae_readq(spi_dev->regs + SPI_CORE_PARAM);

	spi_dev->data_width = spi_dev->spi_param.data_width / 8;
	spi_dev->endian = spi_dev->spi_param.endian;
	spi_dev->num_chipselect = spi_dev->spi_param.num_chipselect;
	dev_info(spi_dev, "spi param: type=%d, data width:%d, endian:%d, clock_polarity=%d, clock=%dMHz, chips=%d, cpha=%d\n",
			spi_dev->spi_param.type,
			spi_dev->data_width, spi_dev->endian,
			spi_dev->spi_param.clock_polarity,
			spi_dev->spi_param.clock,
			spi_dev->num_chipselect,
			spi_dev->spi_param.clock_phase);

	/* clear */
	spi_indirect_write(spi_dev, ALTERA_SPI_CONTROL, 0);
	spi_indirect_write(spi_dev, ALTERA_SPI_STATUS, 0);
	/* flush rxdata */
	spi_flush_rx(spi_dev);

	return spi_dev;
}

void altera_spi_release(struct altera_spi_device *dev)
{
	if (dev)
		opae_free(dev);
}
