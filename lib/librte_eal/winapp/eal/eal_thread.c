/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#include <windows.h>

#include "rte_common.h"

typedef uintptr_t eal_thread_t;

int
eal_thread_create(eal_thread_t *thread __rte_unused)
{
	return 0;
}
