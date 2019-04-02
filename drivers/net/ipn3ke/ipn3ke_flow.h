/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Intel Corporation
 */

#ifndef _IPN3KE_FLOW_H_
#define _IPN3KE_FLOW_H_

/**
 * Expand the length to DWORD alignment with 'Unused' field.
 *
 * FLOW KEY:
 *  | Unused |Ruler id (id)  | Key1 Key2 … (data) |
 *  |--------+---------------+--------------------|
 *  | 17bits |    3 bits     |   Total 108 bits   |
 * MSB                 --->                      LSB
 *
 * Note: And the MSb of key data is filled to 0 when it is less
 *       than 108 bit.
 */
#define IPN3KE_FLOW_KEY_UNUSED_BITS  17
#define IPN3KE_FLOW_KEY_ID_BITS      3
#define IPN3KE_FLOW_KEY_DATA_BITS    108

#define IPN3KE_FLOW_KEY_TOTAL_BITS \
		(IPN3KE_FLOW_KEY_UNUSED_BITS + \
		IPN3KE_FLOW_KEY_ID_BITS + \
		IPN3KE_FLOW_KEY_DATA_BITS)

#define IPN3KE_FLOW_KEY_ID_OFFSET \
		(IPN3KE_FLOW_KEY_UNUSED_BITS)

#define IPN3KE_FLOW_KEY_DATA_OFFSET \
		(IPN3KE_FLOW_KEY_ID_OFFSET + IPN3KE_FLOW_KEY_ID_BITS)

/**
 * Expand the length to DWORD alignment with 'Unused' field.
 *
 * FLOW RESULT:
 *  |  Unused | enable (acl) |    uid       |
 *  |---------+--------------+--------------|
 *  | 15 bits |    1 bit     |   16 bits    |
 * MSB              --->                   LSB
 */

#define IPN3KE_FLOW_RESULT_UNUSED_BITS 15
#define IPN3KE_FLOW_RESULT_ACL_BITS    1
#define IPN3KE_FLOW_RESULT_UID_BITS    16

#define IPN3KE_FLOW_RESULT_TOTAL_BITS \
		(IPN3KE_FLOW_RESULT_UNUSED_BITS + \
		IPN3KE_FLOW_RESULT_ACL_BITS + \
		IPN3KE_FLOW_RESULT_UID_BITS)

#define IPN3KE_FLOW_RESULT_ACL_OFFSET \
		(IPN3KE_FLOW_RESULT_UNUSED_BITS)

#define IPN3KE_FLOW_RESULT_UID_OFFSET \
		(IPN3KE_FLOW_RESULT_ACL_OFFSET + IPN3KE_FLOW_RESULT_ACL_BITS)

#define IPN3KE_FLOW_RESULT_UID_MAX \
		((1UL << IPN3KE_FLOW_RESULT_UID_BITS) - 1)

#ifndef BITS_PER_BYTE
#define BITS_PER_BYTE    8
#endif
#define BITS_TO_BYTES(bits) \
	(((bits) + BITS_PER_BYTE - 1) / BITS_PER_BYTE)

struct ipn3ke_flow_rule {
	uint8_t key[BITS_TO_BYTES(IPN3KE_FLOW_KEY_TOTAL_BITS)];
	uint8_t result[BITS_TO_BYTES(IPN3KE_FLOW_RESULT_TOTAL_BITS)];
};

struct rte_flow {
	TAILQ_ENTRY(rte_flow) next; /**< Pointer to the next flow structure. */

	struct ipn3ke_flow_rule rule;
};

TAILQ_HEAD(ipn3ke_flow_list, rte_flow);

extern const struct rte_flow_ops ipn3ke_flow_ops;

int ipn3ke_flow_init(void *dev);

#endif /* _IPN3KE_FLOW_H_ */
