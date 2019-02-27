# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Cesnet
# Copyright(c) 2018 Netcope Technologies, a.s. <info@netcope.com>
# All rights reserved.

dep = cc.find_library('nfb', required: true)
build = dep.found()
ext_deps += dep
sources = files('nfb_rx.c', 'nfb_tx.c', 'nfb_stats.c', 'nfb_ethdev.c', 'nfb_rxmode.c')
