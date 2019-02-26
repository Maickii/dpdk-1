# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Cesnet
# Copyright(c) 2018 Netcope Technologies, a.s. <info@netcope.com>
# All rights reserved.

include $(RTE_SDK)/mk/rte.vars.mk

#
# library name
#
LIB = librte_pmd_nfb.a

INCLUDES :=-I$(SRCDIR)


CFLAGS += -O3
CFLAGS += $(WERROR_FLAGS)
LDLIBS += -lnfb
LDLIBS += -lrte_eal -lrte_mbuf -lrte_mempool -lrte_ring
LDLIBS += -lrte_ethdev -lrte_net -lrte_kvargs
LDLIBS += -lrte_bus_pci

EXPORT_MAP := rte_pmd_nfb_version.map

LIBABIVER := 1

#
# all source are stored in SRCS-y
#
SRCS-$(CONFIG_RTE_LIBRTE_PMD_NFB) += nfb_ethdev.c
SRCS-$(CONFIG_RTE_LIBRTE_PMD_NFB) += nfb_rx.c
SRCS-$(CONFIG_RTE_LIBRTE_PMD_NFB) += nfb_tx.c
SRCS-$(CONFIG_RTE_LIBRTE_PMD_NFB) += nfb_stats.c
SRCS-$(CONFIG_RTE_LIBRTE_PMD_NFB) += nfb_rxmode.c

#
# Export include files
#
SYMLINK-y-include +=

include $(RTE_SDK)/mk/rte.lib.mk
