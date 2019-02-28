/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

#include "ifpga_feature_dev.h"
#include "opae_i2c.h"
#include "opae_spi.h"
#include "opae_at24_eeprom.h"
#include "opae_phy_group.h"
#include "opae_intel_max10.h"
#include "opae_mdio.h"

#define PWR_THRESHOLD_MAX       0x7F

int fme_get_prop(struct ifpga_fme_hw *fme, struct feature_prop *prop)
{
	struct feature *feature;

	if (!fme)
		return -ENOENT;

	feature = get_fme_feature_by_id(fme, prop->feature_id);

	if (feature && feature->ops && feature->ops->get_prop)
		return feature->ops->get_prop(feature, prop);

	return -ENOENT;
}

int fme_set_prop(struct ifpga_fme_hw *fme, struct feature_prop *prop)
{
	struct feature *feature;

	if (!fme)
		return -ENOENT;

	feature = get_fme_feature_by_id(fme, prop->feature_id);

	if (feature && feature->ops && feature->ops->set_prop)
		return feature->ops->set_prop(feature, prop);

	return -ENOENT;
}

int fme_set_irq(struct ifpga_fme_hw *fme, u32 feature_id, void *irq_set)
{
	struct feature *feature;

	if (!fme)
		return -ENOENT;

	feature = get_fme_feature_by_id(fme, feature_id);

	if (feature && feature->ops && feature->ops->set_irq)
		return feature->ops->set_irq(feature, irq_set);

	return -ENOENT;
}

/* fme private feature head */
static int fme_hdr_init(struct feature *feature)
{
	struct feature_fme_header *fme_hdr;

	fme_hdr = (struct feature_fme_header *)feature->addr;

	dev_info(NULL, "FME HDR Init.\n");
	dev_info(NULL, "FME cap %llx.\n",
		 (unsigned long long)fme_hdr->capability.csr);

	return 0;
}

static void fme_hdr_uinit(struct feature *feature)
{
	UNUSED(feature);

	dev_info(NULL, "FME HDR UInit.\n");
}

static int fme_hdr_get_revision(struct ifpga_fme_hw *fme, u64 *revision)
{
	struct feature_fme_header *fme_hdr
		= get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);
	struct feature_header header;

	header.csr = readq(&fme_hdr->header);
	*revision = header.revision;

	return 0;
}

static int fme_hdr_get_ports_num(struct ifpga_fme_hw *fme, u64 *ports_num)
{
	struct feature_fme_header *fme_hdr
		= get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);
	struct feature_fme_capability fme_capability;

	fme_capability.csr = readq(&fme_hdr->capability);
	*ports_num = fme_capability.num_ports;

	return 0;
}

static int fme_hdr_get_cache_size(struct ifpga_fme_hw *fme, u64 *cache_size)
{
	struct feature_fme_header *fme_hdr
		= get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);
	struct feature_fme_capability fme_capability;

	fme_capability.csr = readq(&fme_hdr->capability);
	*cache_size = fme_capability.cache_size;

	return 0;
}

static int fme_hdr_get_version(struct ifpga_fme_hw *fme, u64 *version)
{
	struct feature_fme_header *fme_hdr
		= get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);
	struct feature_fme_capability fme_capability;

	fme_capability.csr = readq(&fme_hdr->capability);
	*version = fme_capability.fabric_verid;

	return 0;
}

static int fme_hdr_get_socket_id(struct ifpga_fme_hw *fme, u64 *socket_id)
{
	struct feature_fme_header *fme_hdr
		= get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);
	struct feature_fme_capability fme_capability;

	fme_capability.csr = readq(&fme_hdr->capability);
	*socket_id = fme_capability.socket_id;

	return 0;
}

static int fme_hdr_get_bitstream_id(struct ifpga_fme_hw *fme,
				    u64 *bitstream_id)
{
	struct feature_fme_header *fme_hdr
		= get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);

	*bitstream_id = readq(&fme_hdr->bitstream_id);

	return 0;
}

static int fme_hdr_get_bitstream_metadata(struct ifpga_fme_hw *fme,
					  u64 *bitstream_metadata)
{
	struct feature_fme_header *fme_hdr
		= get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);

	*bitstream_metadata = readq(&fme_hdr->bitstream_md);

	return 0;
}

static int
fme_hdr_get_prop(struct feature *feature, struct feature_prop *prop)
{
	struct ifpga_fme_hw *fme = feature->parent;

	switch (prop->prop_id) {
	case FME_HDR_PROP_REVISION:
		return fme_hdr_get_revision(fme, &prop->data);
	case FME_HDR_PROP_PORTS_NUM:
		return fme_hdr_get_ports_num(fme, &prop->data);
	case FME_HDR_PROP_CACHE_SIZE:
		return fme_hdr_get_cache_size(fme, &prop->data);
	case FME_HDR_PROP_VERSION:
		return fme_hdr_get_version(fme, &prop->data);
	case FME_HDR_PROP_SOCKET_ID:
		return fme_hdr_get_socket_id(fme, &prop->data);
	case FME_HDR_PROP_BITSTREAM_ID:
		return fme_hdr_get_bitstream_id(fme, &prop->data);
	case FME_HDR_PROP_BITSTREAM_METADATA:
		return fme_hdr_get_bitstream_metadata(fme, &prop->data);
	}

	return -ENOENT;
}

struct feature_ops fme_hdr_ops = {
	.init = fme_hdr_init,
	.uinit = fme_hdr_uinit,
	.get_prop = fme_hdr_get_prop,
};

/* thermal management */
static int fme_thermal_get_threshold1(struct ifpga_fme_hw *fme, u64 *thres1)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_tmp_threshold temp_threshold;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	temp_threshold.csr = readq(&thermal->threshold);
	*thres1 = temp_threshold.tmp_thshold1;

	return 0;
}

static int fme_thermal_set_threshold1(struct ifpga_fme_hw *fme, u64 thres1)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_header *fme_hdr;
	struct feature_fme_tmp_threshold tmp_threshold;
	struct feature_fme_capability fme_capability;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);
	fme_hdr = get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);

	spinlock_lock(&fme->lock);
	tmp_threshold.csr = readq(&thermal->threshold);
	fme_capability.csr = readq(&fme_hdr->capability);

	if (fme_capability.lock_bit == 1) {
		spinlock_unlock(&fme->lock);
		return -EBUSY;
	} else if (thres1 > 100) {
		spinlock_unlock(&fme->lock);
		return -EINVAL;
	} else if (thres1 == 0) {
		tmp_threshold.tmp_thshold1_enable = 0;
		tmp_threshold.tmp_thshold1 = thres1;
	} else {
		tmp_threshold.tmp_thshold1_enable = 1;
		tmp_threshold.tmp_thshold1 = thres1;
	}

	writeq(tmp_threshold.csr, &thermal->threshold);
	spinlock_unlock(&fme->lock);

	return 0;
}

static int fme_thermal_get_threshold2(struct ifpga_fme_hw *fme, u64 *thres2)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_tmp_threshold temp_threshold;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	temp_threshold.csr = readq(&thermal->threshold);
	*thres2 = temp_threshold.tmp_thshold2;

	return 0;
}

static int fme_thermal_set_threshold2(struct ifpga_fme_hw *fme, u64 thres2)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_header *fme_hdr;
	struct feature_fme_tmp_threshold tmp_threshold;
	struct feature_fme_capability fme_capability;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);
	fme_hdr = get_fme_feature_ioaddr_by_index(fme, FME_FEATURE_ID_HEADER);

	spinlock_lock(&fme->lock);
	tmp_threshold.csr = readq(&thermal->threshold);
	fme_capability.csr = readq(&fme_hdr->capability);

	if (fme_capability.lock_bit == 1) {
		spinlock_unlock(&fme->lock);
		return -EBUSY;
	} else if (thres2 > 100) {
		spinlock_unlock(&fme->lock);
		return -EINVAL;
	} else if (thres2 == 0) {
		tmp_threshold.tmp_thshold2_enable = 0;
		tmp_threshold.tmp_thshold2 = thres2;
	} else {
		tmp_threshold.tmp_thshold2_enable = 1;
		tmp_threshold.tmp_thshold2 = thres2;
	}

	writeq(tmp_threshold.csr, &thermal->threshold);
	spinlock_unlock(&fme->lock);

	return 0;
}

static int fme_thermal_get_threshold_trip(struct ifpga_fme_hw *fme,
					  u64 *thres_trip)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_tmp_threshold temp_threshold;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	temp_threshold.csr = readq(&thermal->threshold);
	*thres_trip = temp_threshold.therm_trip_thshold;

	return 0;
}

static int fme_thermal_get_threshold1_reached(struct ifpga_fme_hw *fme,
					      u64 *thres1_reached)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_tmp_threshold temp_threshold;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	temp_threshold.csr = readq(&thermal->threshold);
	*thres1_reached = temp_threshold.thshold1_status;

	return 0;
}

static int fme_thermal_get_threshold2_reached(struct ifpga_fme_hw *fme,
					      u64 *thres1_reached)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_tmp_threshold temp_threshold;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	temp_threshold.csr = readq(&thermal->threshold);
	*thres1_reached = temp_threshold.thshold2_status;

	return 0;
}

static int fme_thermal_get_threshold1_policy(struct ifpga_fme_hw *fme,
					     u64 *thres1_policy)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_tmp_threshold temp_threshold;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	temp_threshold.csr = readq(&thermal->threshold);
	*thres1_policy = temp_threshold.thshold_policy;

	return 0;
}

static int fme_thermal_set_threshold1_policy(struct ifpga_fme_hw *fme,
					     u64 thres1_policy)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_tmp_threshold tmp_threshold;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	spinlock_lock(&fme->lock);
	tmp_threshold.csr = readq(&thermal->threshold);

	if (thres1_policy == 0) {
		tmp_threshold.thshold_policy = 0;
	} else if (thres1_policy == 1) {
		tmp_threshold.thshold_policy = 1;
	} else {
		spinlock_unlock(&fme->lock);
		return -EINVAL;
	}

	writeq(tmp_threshold.csr, &thermal->threshold);
	spinlock_unlock(&fme->lock);

	return 0;
}

static int fme_thermal_get_temperature(struct ifpga_fme_hw *fme, u64 *temp)
{
	struct feature_fme_thermal *thermal;
	struct feature_fme_temp_rdsensor_fmt1 temp_rdsensor_fmt1;

	thermal = get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);

	temp_rdsensor_fmt1.csr = readq(&thermal->rdsensor_fm1);
	*temp = temp_rdsensor_fmt1.fpga_temp;

	return 0;
}

static int fme_thermal_get_revision(struct ifpga_fme_hw *fme, u64 *revision)
{
	struct feature_fme_thermal *fme_thermal
		= get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_THERMAL_MGMT);
	struct feature_header header;

	header.csr = readq(&fme_thermal->header);
	*revision = header.revision;

	return 0;
}

#define FME_THERMAL_CAP_NO_TMP_THRESHOLD	0x1

static int fme_thermal_mgmt_init(struct feature *feature)
{
	struct feature_fme_thermal *fme_thermal;
	struct feature_fme_tmp_threshold_cap thermal_cap;

	UNUSED(feature);

	dev_info(NULL, "FME thermal mgmt Init.\n");

	fme_thermal = (struct feature_fme_thermal *)feature->addr;
	thermal_cap.csr = readq(&fme_thermal->threshold_cap);

	dev_info(NULL, "FME thermal cap %llx.\n",
		 (unsigned long long)fme_thermal->threshold_cap.csr);

	if (thermal_cap.tmp_thshold_disabled)
		feature->cap |= FME_THERMAL_CAP_NO_TMP_THRESHOLD;

	return 0;
}

static void fme_thermal_mgmt_uinit(struct feature *feature)
{
	UNUSED(feature);

	dev_info(NULL, "FME thermal mgmt UInit.\n");
}

static int
fme_thermal_set_prop(struct feature *feature, struct feature_prop *prop)
{
	struct ifpga_fme_hw *fme = feature->parent;

	if (feature->cap & FME_THERMAL_CAP_NO_TMP_THRESHOLD)
		return -ENOENT;

	switch (prop->prop_id) {
	case FME_THERMAL_PROP_THRESHOLD1:
		return fme_thermal_set_threshold1(fme, prop->data);
	case FME_THERMAL_PROP_THRESHOLD2:
		return fme_thermal_set_threshold2(fme, prop->data);
	case FME_THERMAL_PROP_THRESHOLD1_POLICY:
		return fme_thermal_set_threshold1_policy(fme, prop->data);
	}

	return -ENOENT;
}

static int
fme_thermal_get_prop(struct feature *feature, struct feature_prop *prop)
{
	struct ifpga_fme_hw *fme = feature->parent;

	if (feature->cap & FME_THERMAL_CAP_NO_TMP_THRESHOLD &&
	    prop->prop_id != FME_THERMAL_PROP_TEMPERATURE &&
	    prop->prop_id != FME_THERMAL_PROP_REVISION)
		return -ENOENT;

	switch (prop->prop_id) {
	case FME_THERMAL_PROP_THRESHOLD1:
		return fme_thermal_get_threshold1(fme, &prop->data);
	case FME_THERMAL_PROP_THRESHOLD2:
		return fme_thermal_get_threshold2(fme, &prop->data);
	case FME_THERMAL_PROP_THRESHOLD_TRIP:
		return fme_thermal_get_threshold_trip(fme, &prop->data);
	case FME_THERMAL_PROP_THRESHOLD1_REACHED:
		return fme_thermal_get_threshold1_reached(fme, &prop->data);
	case FME_THERMAL_PROP_THRESHOLD2_REACHED:
		return fme_thermal_get_threshold2_reached(fme, &prop->data);
	case FME_THERMAL_PROP_THRESHOLD1_POLICY:
		return fme_thermal_get_threshold1_policy(fme, &prop->data);
	case FME_THERMAL_PROP_TEMPERATURE:
		return fme_thermal_get_temperature(fme, &prop->data);
	case FME_THERMAL_PROP_REVISION:
		return fme_thermal_get_revision(fme, &prop->data);
	}

	return -ENOENT;
}

struct feature_ops fme_thermal_mgmt_ops = {
	.init = fme_thermal_mgmt_init,
	.uinit = fme_thermal_mgmt_uinit,
	.get_prop = fme_thermal_get_prop,
	.set_prop = fme_thermal_set_prop,
};

static int fme_pwr_get_consumed(struct ifpga_fme_hw *fme, u64 *consumed)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_status pm_status;

	pm_status.csr = readq(&fme_power->status);

	*consumed = pm_status.pwr_consumed;

	return 0;
}

static int fme_pwr_get_threshold1(struct ifpga_fme_hw *fme, u64 *threshold)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_ap_threshold pm_ap_threshold;

	pm_ap_threshold.csr = readq(&fme_power->threshold);

	*threshold = pm_ap_threshold.threshold1;

	return 0;
}

static int fme_pwr_set_threshold1(struct ifpga_fme_hw *fme, u64 threshold)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_ap_threshold pm_ap_threshold;

	spinlock_lock(&fme->lock);
	pm_ap_threshold.csr = readq(&fme_power->threshold);

	if (threshold <= PWR_THRESHOLD_MAX) {
		pm_ap_threshold.threshold1 = threshold;
	} else {
		spinlock_unlock(&fme->lock);
		return -EINVAL;
	}

	writeq(pm_ap_threshold.csr, &fme_power->threshold);
	spinlock_unlock(&fme->lock);

	return 0;
}

static int fme_pwr_get_threshold2(struct ifpga_fme_hw *fme, u64 *threshold)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_ap_threshold pm_ap_threshold;

	pm_ap_threshold.csr = readq(&fme_power->threshold);

	*threshold = pm_ap_threshold.threshold2;

	return 0;
}

static int fme_pwr_set_threshold2(struct ifpga_fme_hw *fme, u64 threshold)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_ap_threshold pm_ap_threshold;

	spinlock_lock(&fme->lock);
	pm_ap_threshold.csr = readq(&fme_power->threshold);

	if (threshold <= PWR_THRESHOLD_MAX) {
		pm_ap_threshold.threshold2 = threshold;
	} else {
		spinlock_unlock(&fme->lock);
		return -EINVAL;
	}

	writeq(pm_ap_threshold.csr, &fme_power->threshold);
	spinlock_unlock(&fme->lock);

	return 0;
}

static int fme_pwr_get_threshold1_status(struct ifpga_fme_hw *fme,
					 u64 *threshold_status)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_ap_threshold pm_ap_threshold;

	pm_ap_threshold.csr = readq(&fme_power->threshold);

	*threshold_status = pm_ap_threshold.threshold1_status;

	return 0;
}

static int fme_pwr_get_threshold2_status(struct ifpga_fme_hw *fme,
					 u64 *threshold_status)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_ap_threshold pm_ap_threshold;

	pm_ap_threshold.csr = readq(&fme_power->threshold);

	*threshold_status = pm_ap_threshold.threshold2_status;

	return 0;
}

static int fme_pwr_get_rtl(struct ifpga_fme_hw *fme, u64 *rtl)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_status pm_status;

	pm_status.csr = readq(&fme_power->status);

	*rtl = pm_status.fpga_latency_report;

	return 0;
}

static int fme_pwr_get_xeon_limit(struct ifpga_fme_hw *fme, u64 *limit)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_xeon_limit xeon_limit;

	xeon_limit.csr = readq(&fme_power->xeon_limit);

	if (!xeon_limit.enable)
		xeon_limit.pwr_limit = 0;

	*limit = xeon_limit.pwr_limit;

	return 0;
}

static int fme_pwr_get_fpga_limit(struct ifpga_fme_hw *fme, u64 *limit)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
				FME_FEATURE_ID_POWER_MGMT);
	struct feature_fme_pm_fpga_limit fpga_limit;

	fpga_limit.csr = readq(&fme_power->fpga_limit);

	if (!fpga_limit.enable)
		fpga_limit.pwr_limit = 0;

	*limit = fpga_limit.pwr_limit;

	return 0;
}

static int fme_pwr_get_revision(struct ifpga_fme_hw *fme, u64 *revision)
{
	struct feature_fme_power *fme_power
		= get_fme_feature_ioaddr_by_index(fme,
						  FME_FEATURE_ID_POWER_MGMT);
	struct feature_header header;

	header.csr = readq(&fme_power->header);
	*revision = header.revision;

	return 0;
}

static int fme_power_mgmt_init(struct feature *feature)
{
	UNUSED(feature);

	dev_info(NULL, "FME power mgmt Init.\n");

	return 0;
}

static void fme_power_mgmt_uinit(struct feature *feature)
{
	UNUSED(feature);

	dev_info(NULL, "FME power mgmt UInit.\n");
}

static int fme_power_mgmt_get_prop(struct feature *feature,
				   struct feature_prop *prop)
{
	struct ifpga_fme_hw *fme = feature->parent;

	switch (prop->prop_id) {
	case FME_PWR_PROP_CONSUMED:
		return fme_pwr_get_consumed(fme, &prop->data);
	case FME_PWR_PROP_THRESHOLD1:
		return fme_pwr_get_threshold1(fme, &prop->data);
	case FME_PWR_PROP_THRESHOLD2:
		return fme_pwr_get_threshold2(fme, &prop->data);
	case FME_PWR_PROP_THRESHOLD1_STATUS:
		return fme_pwr_get_threshold1_status(fme, &prop->data);
	case FME_PWR_PROP_THRESHOLD2_STATUS:
		return fme_pwr_get_threshold2_status(fme, &prop->data);
	case FME_PWR_PROP_RTL:
		return fme_pwr_get_rtl(fme, &prop->data);
	case FME_PWR_PROP_XEON_LIMIT:
		return fme_pwr_get_xeon_limit(fme, &prop->data);
	case FME_PWR_PROP_FPGA_LIMIT:
		return fme_pwr_get_fpga_limit(fme, &prop->data);
	case FME_PWR_PROP_REVISION:
		return fme_pwr_get_revision(fme, &prop->data);
	}

	return -ENOENT;
}

static int fme_power_mgmt_set_prop(struct feature *feature,
				   struct feature_prop *prop)
{
	struct ifpga_fme_hw *fme = feature->parent;

	switch (prop->prop_id) {
	case FME_PWR_PROP_THRESHOLD1:
		return fme_pwr_set_threshold1(fme, prop->data);
	case FME_PWR_PROP_THRESHOLD2:
		return fme_pwr_set_threshold2(fme, prop->data);
	}

	return -ENOENT;
}

struct feature_ops fme_power_mgmt_ops = {
	.init = fme_power_mgmt_init,
	.uinit = fme_power_mgmt_uinit,
	.get_prop = fme_power_mgmt_get_prop,
	.set_prop = fme_power_mgmt_set_prop,
};

static int spi_self_checking(void)
{
	u32 val;
	int ret;

	ret = max10_reg_read(0x30043c, &val);
	if (ret)
		return -EIO;

	if (val != 0x87654321) {
		dev_err(NULL, "Read MAX10 test register fail: 0x%x\n", val);
		return -EIO;
	}

	dev_info(NULL, "Read MAX10 test register success, SPI self-test done\n");

	return 0;
}

static int fme_spi_init(struct feature *feature)
{
	struct feature_fme_spi *spi;
	struct ifpga_fme_hw *fme = (struct ifpga_fme_hw *)feature->parent;
	struct altera_spi_device *spi_master;
	struct intel_max10_device *max10;
	int ret = 0;

	spi = (struct feature_fme_spi *)feature->addr;

	dev_info(fme, "FME SPI Master (Max10) Init.\n");
	dev_debug(fme, "FME SPI base addr %llx.\n",
		 (unsigned long long)spi);
	dev_debug(fme, "spi param=0x%lx\n", opae_readq(feature->addr + 0x8));

	spi_master = altera_spi_init(feature->addr);
	if (!spi_master)
		return -ENODEV;

	max10 = intel_max10_device_probe(spi_master, 0);
	if (!max10) {
		ret = -ENODEV;
		dev_err(fme, "max10 init fail\n");
		goto spi_fail;
	}

	fme->max10_dev = max10;

	/* SPI self test */
	if (spi_self_checking())
		return -EIO;

	return ret;

spi_fail:
	altera_spi_release(spi_master);
	return ret;
}

static void fme_spi_uinit(struct feature *feature)
{
	struct ifpga_fme_hw *fme = (struct ifpga_fme_hw *)feature->parent;

	if (fme->max10_dev)
		intel_max10_device_remove(fme->max10_dev);
}

struct feature_ops fme_spi_master_ops = {
	.init = fme_spi_init,
	.uinit = fme_spi_uinit,

};

static int i2c_mac_rom_test(struct altera_i2c_dev *dev)
{
	char buf[20];
	int ret;
	char read_buf[20] = {0,};
	const char *string = "1a2b3c4d5e";
	unsigned int i;

	opae_memcpy(buf, string, strlen(string));

	printf("data writing into mac rom:\n");
	for (i = 0; i < strlen(string); i++)
		printf("%x ", *((char *)buf+i));
	printf("\n");

	ret = at24_eeprom_write(dev, AT24512_SLAVE_ADDR, 0,
			(u8 *)buf, strlen(string));
	if (ret < 0)
		printf("write i2c error:%d\n", ret);

	ret = at24_eeprom_read(dev, AT24512_SLAVE_ADDR, 0,
			(u8 *)read_buf, strlen(string));
	if (ret < 0)
		printf("read i2c error:%d\n", ret);

	printf("read from mac rom\n");
	for (i = 0; i < strlen(string); i++)
		printf("%x ", *((char *)read_buf+i));
	printf("\n");

	if (!memcmp(buf, read_buf, strlen(string))) {
		printf("%s test success!\n", __func__);
		return -EFAULT;
	}

	printf("%s test fail\n", __func__);

	return 0;
}

static int fme_i2c_init(struct feature *feature)
{
	struct feature_fme_i2c *i2c;
	struct ifpga_fme_hw *fme = (struct ifpga_fme_hw *)feature->parent;

	i2c = (struct feature_fme_i2c *)feature->addr;

	dev_info(NULL, "FME I2C Master Init.\n");

	fme->i2c_master = altera_i2c_probe(i2c);
	if (!fme->i2c_master)
		return -ENODEV;

	if (i2c_mac_rom_test(fme->i2c_master))
		return -ENODEV;

	return 0;
}

static void fme_i2c_uninit(struct feature *feature)
{
	struct ifpga_fme_hw *fme = (struct ifpga_fme_hw *)feature->parent;

	altera_i2c_remove(fme->i2c_master);
}

struct feature_ops fme_i2c_master_ops = {
	.init = fme_i2c_init,
	.uinit = fme_i2c_uninit,
};

static int fme_phy_group_init(struct feature *feature)
{
	struct ifpga_fme_hw *fme = (struct ifpga_fme_hw *)feature->parent;
	struct phy_group_device *dev;

	dev = (struct phy_group_device *)phy_group_probe(feature->addr);
	if (!dev)
		return -ENODEV;

	fme->phy_dev[dev->group_index] = dev;

	dev_info(NULL, "FME PHY Group %d Init.\n", dev->group_index);
	dev_info(NULL, "FME PHY Group register base address %llx.\n",
			(unsigned long long)dev->base);

	return 0;
}

static void fme_phy_group_uinit(struct feature *feature)
{
	UNUSED(feature);
}

struct feature_ops fme_phy_group_ops = {
	.init = fme_phy_group_init,
	.uinit = fme_phy_group_uinit,
};

static int fme_hssi_eth_init(struct feature *feature)
{
	UNUSED(feature);
	return 0;
}

static void fme_hssi_eth_uinit(struct feature *feature)
{
	UNUSED(feature);
}

struct feature_ops fme_hssi_eth_ops = {
	.init = fme_hssi_eth_init,
	.uinit = fme_hssi_eth_uinit,
};

static int fme_emif_init(struct feature *feature)
{
	UNUSED(feature);
	return 0;
}

static void fme_emif_uinit(struct feature *feature)
{
	UNUSED(feature);
}

struct feature_ops fme_emif_ops = {
	.init = fme_emif_init,
	.uinit = fme_emif_uinit,
};

static int fme_check_retimter_ports(struct ifpga_fme_hw *fme, int port)
{
	struct intel_max10_device *dev;
	int ports;

	dev = (struct intel_max10_device *)fme->max10_dev;
	if (!dev)
		return -ENODEV;

	ports = dev->num_retimer * dev->num_port;

	if (port > ports || port < 0)
		return -EINVAL;

	return 0;
}

int fme_mgr_read_mac_rom(struct ifpga_fme_hw *fme, int offset,
		void *buf, int size)
{
	struct altera_i2c_dev *dev;

	dev = fme->i2c_master;
	if (!dev)
		return -ENODEV;

	if (fme_check_retimter_ports(fme, offset/size))
		return -EINVAL;

	return at24_eeprom_read(dev, AT24512_SLAVE_ADDR, offset, buf, size);
}

int fme_mgr_write_mac_rom(struct ifpga_fme_hw *fme, int offset,
		void *buf, int size)
{
	struct altera_i2c_dev *dev;

	dev = fme->i2c_master;
	if (!dev)
		return -ENODEV;

	if (fme_check_retimter_ports(fme, offset/size))
		return -EINVAL;

	return at24_eeprom_write(dev, AT24512_SLAVE_ADDR, offset, buf, size);
}

int fme_mgr_read_phy_reg(struct ifpga_fme_hw *fme, int phy_group,
		u8 entry, u16 reg, u32 *value)
{
	struct phy_group_device *dev;

	if (phy_group > (MAX_PHY_GROUP_DEVICES - 1))
		return -EINVAL;

	dev = (struct phy_group_device *)fme->phy_dev[phy_group];
	if (!dev)
		return -ENODEV;

	if (entry > dev->entries)
		return -EINVAL;


	return phy_group_read_reg(dev, entry, reg, value);
}

int fme_mgr_write_phy_reg(struct ifpga_fme_hw *fme, int phy_group,
		u8 entry, u16 reg, u32 value)
{
	struct phy_group_device *dev;

	if (phy_group > (MAX_PHY_GROUP_DEVICES - 1))
		return -EINVAL;

	dev = (struct phy_group_device *)fme->phy_dev[phy_group];
	if (!dev)
		return -ENODEV;

	return phy_group_write_reg(dev, entry, reg, value);
}

int fme_mgr_get_retimer_info(struct ifpga_fme_hw *fme,
		struct opae_retimer_info *info)
{
	struct intel_max10_device *dev;

	dev = (struct intel_max10_device *)fme->max10_dev;
	if (!dev)
		return -ENODEV;

	info->num_retimer = dev->num_retimer;
	info->num_port = dev->num_port;

	return 0;
}

int fme_mgr_set_retimer_speed(struct ifpga_fme_hw *fme, int speed)
{
	struct intel_max10_device *dev;
	int i, j, num;
	int ret = 0;

	dev = (struct intel_max10_device *)fme->max10_dev;
	if (!dev)
		return -ENODEV;

	num = dev->num_retimer < INTEL_MAX10_MAX_MDIO_DEVS ?
		dev->num_retimer : INTEL_MAX10_MAX_MDIO_DEVS;

	for (i = 0; i < num; i++)
		for (j = 0; j < dev->num_port; j++) {
			ret = pkvl_set_speed_mode(dev->mdio[i], j, speed);
			if (ret) {
				printf("pkvl_%d set port_%d speed %d fail\n",
						i, j, speed);
				break;
			}
		}

	return ret;
}

int fme_mgr_get_retimer_status(struct ifpga_fme_hw *fme, int port,
		struct opae_retimer_status *status)
{
	struct intel_max10_device *dev;
	struct altera_mdio_dev *mdio;
	int ports;
	int ret;

	dev = (struct intel_max10_device *)fme->max10_dev;
	if (!dev)
		return -ENODEV;

	ports = dev->num_retimer * dev->num_port;

	if (port > ports || port < 0)
		return -EINVAL;

	mdio = dev->mdio[port/dev->num_port];
	port = port % dev->num_port;

	ret = pkvl_get_port_speed_status(mdio, port, &status->speed);
	if (ret)
		goto error;

	ret = pkvl_get_port_line_link_status(mdio, port, &status->line_link);
	if (ret)
		goto error;

	ret = pkvl_get_port_host_link_status(mdio, port, &status->host_link);
	if (ret)
		goto error;

	dev_info(NULL, "get retimer status: pkvl:%d, port:%d, speed:%d, line:%d, host:%d\n",
			mdio->index, port, status->speed,
			status->line_link, status->host_link);

	return 0;

error:
	return ret;
}
