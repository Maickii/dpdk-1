# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Intel Corporation

subdir('base')
objs = [base_objs]

sources = files(
	'ice_ethdev.c',
	'ice_rxtx.c'
	)

deps += ['hash']
includes += include_directories('base')

if arch_subdir == 'x86'
	dpdk_conf.set('RTE_LIBRTE_ICE_RX_ALLOW_BULK_ALLOC', 1)
	dpdk_conf.set('RTE_LIBRTE_ICE_INC_VECTOR', 1)
	sources += files('ice_rxtx_vec_sse.c')
endif
