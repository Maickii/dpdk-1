/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#ifndef _MISC_H_
#define _MISC_H_

/**
 * @file misc.h
 * Contains miscelaneous functions/structures/macros used internally
 * by ipsec library.
 */

/*
 * Move bad (unprocessed) mbufs beyond the good (processed) ones.
 * dr[] contains the indexes of bad mbufs insinde the mb[].
 */
static inline void
mbuf_bad_move(struct rte_mbuf *mb[], const uint32_t dr[], uint32_t num,
	uint32_t drn)
{
	uint32_t i, j, k;
	struct rte_mbuf *drb[drn];

	j = 0;
	k = 0;

	/* copy bad ones into a temp place */
	for (i = 0; i != num; i++) {
		if (j != drn && i == dr[j])
			drb[j++] = mb[i];
		else
			mb[k++] = mb[i];
	}

	/* copy bad ones after the good ones */
	for (i = 0; i != drn; i++)
		mb[k + i] = drb[i];
}

#endif /* _MISC_H_ */
