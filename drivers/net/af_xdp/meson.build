# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2019 Intel Corporation

if host_machine.system() != 'linux'
	build = false
endif

bpf_dep = dependency('libbpf', required: false)
if bpf_dep.found()
	build = true
else
	bpf_dep = cc.find_library('libbpf', required: false)
	if bpf_dep.found() and cc.has_header('xsk.h', dependencies: bpf_dep) and cc.has_header('linux/if_xdp.h')
		build = true
		pkgconfig_extra_libs += '-lbpf'
	else
		build = false
	endif
endif
sources = files('rte_eth_af_xdp.c')
ext_deps += bpf_dep
