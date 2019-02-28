#ifndef _OPAE_SPI_H
#define _OPAE_SPI_H

#include "opae_osdep.h"

#define ALTERA_SPI_RXDATA	0
#define ALTERA_SPI_TXDATA	4
#define ALTERA_SPI_STATUS	8
#define ALTERA_SPI_CONTROL	12
#define ALTERA_SPI_SLAVE_SEL	20

#define ALTERA_SPI_STATUS_ROE_MSK	0x8
#define ALTERA_SPI_STATUS_TOE_MSK	0x10
#define ALTERA_SPI_STATUS_TMT_MSK	0x20
#define ALTERA_SPI_STATUS_TRDY_MSK	0x40
#define ALTERA_SPI_STATUS_RRDY_MSK	0x80
#define ALTERA_SPI_STATUS_E_MSK		0x100

#define ALTERA_SPI_CONTROL_IROE_MSK	0x8
#define ALTERA_SPI_CONTROL_ITOE_MSK	0x10
#define ALTERA_SPI_CONTROL_ITRDY_MSK	0x40
#define ALTERA_SPI_CONTROL_IRRDY_MSK	0x80
#define ALTERA_SPI_CONTROL_IE_MSK	0x100
#define ALTERA_SPI_CONTROL_SSO_MSK	0x400

#define SPI_CORE_PARAM 0x8
#define SPI_CTRL 0x10
#define CTRL_R    BIT_ULL(9)
#define CTRL_W    BIT_ULL(8)
#define CTRL_ADDR_MASK GENMASK_ULL(2, 0)
#define SPI_READ 0x18
#define READ_DATA_VALID BIT_ULL(32)
#define READ_DATA_MASK GENMASK_ULL(31, 0)
#define SPI_WRITE 0x20
#define WRITE_DATA_MASK GENMASK_ULL(31, 0)

#define SPI_MAX_RETRY 100000

struct spi_core_param {
	union {
		u64 info;
		struct {
			u8 type:1;
			u8 endian:1;
			u8 data_width:6;
			u8 num_chipselect:6;
			u8 clock_polarity:1;
			u8 clock_phase:1;
			u8 stages:2;
			u8 resvd:4;
			u16 clock:10;
			u16 peripheral_id:16;
			u8 controller_type:1;
			u16 resvd1:15;
		};
	};
};

struct altera_spi_device {
	u8 *regs;
	struct spi_core_param spi_param;
	int data_width; /* how many bytes for data width */
	int endian;
	#define SPI_BIG_ENDIAN  0
	#define SPI_LITTLE_ENDIAN 1
	int num_chipselect;
	unsigned char *rxbuf;
	unsigned char *txbuf;
	unsigned int len;
};

#define HEADER_LEN 8
#define RESPONSE_LEN 4
#define SPI_TRANSACTION_MAX_LEN 1024
#define TRAN_SEND_MAX_LEN (SPI_TRANSACTION_MAX_LEN + HEADER_LEN)
#define TRAN_RESP_MAX_LEN SPI_TRANSACTION_MAX_LEN
#define PACKET_SEND_MAX_LEN (2*TRAN_SEND_MAX_LEN + 4)
#define PACKET_RESP_MAX_LEN (2*TRAN_RESP_MAX_LEN + 4)
#define BYTES_SEND_MAX_LEN  (2*PACKET_SEND_MAX_LEN)
#define BYTES_RESP_MAX_LEN (2*PACKET_RESP_MAX_LEN)

struct spi_tran_buffer {
	unsigned char tran_send[TRAN_SEND_MAX_LEN];
	unsigned char tran_resp[TRAN_RESP_MAX_LEN];
	unsigned char packet_send[PACKET_SEND_MAX_LEN];
	unsigned char packet_resp[PACKET_RESP_MAX_LEN];
	unsigned char bytes_send[BYTES_SEND_MAX_LEN];
	unsigned char bytes_resp[2*BYTES_RESP_MAX_LEN];
};

struct spi_transaction_dev {
	struct altera_spi_device *dev;
	int chipselect;
	struct spi_tran_buffer *buffer;
};

struct spi_tran_header {
	u8 trans_type;
	u8 reserve;
	u16 size;
	u32 addr;
};

int spi_write(struct altera_spi_device *dev, unsigned int bytes, void *buffer);
int spi_read(struct altera_spi_device *dev, unsigned int bytes,
		void *buffer);
int spi_command(struct altera_spi_device *dev, unsigned int chip_select,
		unsigned int wlen, void *wdata, unsigned int rlen, void *rdata);
void spi_cs_deactivate(struct altera_spi_device *dev);
void spi_cs_activate(struct altera_spi_device *dev, unsigned int chip_select);
struct altera_spi_device *altera_spi_init(void *base);
void altera_spi_release(struct altera_spi_device *dev);
int spi_transaction_read(struct spi_transaction_dev *dev, unsigned int addr,
		unsigned int size, unsigned char *data);
int spi_transaction_write(struct spi_transaction_dev *dev, unsigned int addr,
		unsigned int size, unsigned char *data);
struct spi_transaction_dev *spi_transaction_init(struct altera_spi_device *dev,
		int chipselect);
void spi_transaction_remove(struct spi_transaction_dev *dev);
#endif
