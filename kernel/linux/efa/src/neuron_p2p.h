// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef __NEURON_P2P_H__
#define __NEURON_P2P_H__

struct neuron_p2p_page_info {
    u64 physical_address; // PA's that map to the VA (page aligned as defined in va_info)
    u32 page_count; // page count each page is shift_page_size size
};

struct neuron_p2p_va_info {
    void *virtual_address; // Virtual address for which the PA's need to be obtained
    u64 size; // The actual size of the memory pointed by the virtual_address
    u32 shift_page_size; // log2 of the page size
    u32 device_index; // Neuron Device index.
    u32 entries; // Number of page_info entries
    struct neuron_p2p_page_info page_info[];
};

/** Given the virtual address and length returns the physical address
 *
 * @param[in] virtual_address   - Virtual address of device memory
 * @param[in] length            - Length of the memory
 * @param[out] va_info          - Set of physical addresses
 * @param[in] free_callback     - Callback function to be called. This will be called with a lock held.
 * @param[in] data              - Data to be used for the callback
 *
 * @return 0            - Success.
 */
int neuron_p2p_register_va(u64 virtual_address, u64 length, struct neuron_p2p_va_info **vainfo, void (*free_callback) (void *data), void *data);

/** Give the pa, release the pa from being used by third-party device
 *
 * @param[in] va_info           - Set of physical addresses
 *
 * @return 0            - Success.
 */
int neuron_p2p_unregister_va(struct neuron_p2p_va_info *vainfo);

#endif
