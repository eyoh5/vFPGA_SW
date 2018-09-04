/* 
 * Copyright (c) 2017, Intel Corporation.
 * Intel, the Intel logo, Intel, MegaCore, NIOS II, Quartus and TalkBack 
 * words and logos are trademarks of Intel Corporation or its subsidiaries 
 * in the U.S. and/or other countries. Other marks and brands may be 
 * claimed as the property of others.   See Trademarks on intel.com for 
 * full list of Intel trademarks or the Trademarks & Brands Names Database 
 * (if Intel) or See www.Intel.com/legal (if Altera).
 * All rights reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD 3-Clause license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *      - Neither Intel nor the names of its contributors may be 
 *        used to endorse or promote products derived from this 
 *        software without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Defines used only by aclpci_dma.c. */


#if USE_DMA

/* Enable Linux-specific defines in the hw_pcie_dma.h file */
#define LINUX
#include <linux/workqueue.h>
#include "hw_pcie_dma.h"
#include "hw_host_channel.h"
#include "aclpci_queue.h"

struct dma_t {
  void *ptr;         /* if ptr is NULL, the whole struct considered invalid */
  size_t len;
  enum dma_data_direction dir;
  struct page **pages;     /* one for each struct page */
  dma_addr_t *dma_addrs;   /* one for each struct page */
  unsigned int num_pages;
};

struct pinned_mem {
  struct dma_t dma;
  struct page **next_page;
  dma_addr_t *next_dma_addr;
  unsigned int pages_rem;
  unsigned int first_page_offset;
  unsigned int last_page_offset;
};

struct work_struct_t{
  struct work_struct work;
  void *data;
};

struct hostch_entry {                  
  u32 page_addr_ldw;
  u32 page_addr_udw;
  u32 page_num;
  u32 reserved[5];
} __attribute__ ((packed));

struct hostch_table {
  struct hostch_entry page_entry[4096];
} __attribute__ ((packed));


struct aclpci_hostch_desc {
  size_t buffer_size;
  unsigned int loop_counter;
  
  int push_valid;
  int pull_valid;
  
  // User memory circular buffer
  void *user_rd_buffer;
  void *user_wr_buffer;
  
  struct hostch_table *push_page_table;
  struct hostch_table *pull_page_table;

  dma_addr_t push_page_table_bus_addr;
  dma_addr_t pull_page_table_bus_addr;
  
  struct pinned_mem m_hostch_rd_mem;
  struct pinned_mem m_hostch_wr_mem;
  
  // User memory circular buffer front and end pointers
  size_t *user_rd_front_pointer;
  size_t *user_rd_end_pointer;
  size_t *user_wr_front_pointer;
  size_t *user_wr_end_pointer;
  
  dma_addr_t user_rd_front_pointer_bus_addr;
  dma_addr_t user_wr_end_pointer_bus_addr;
  
  struct pinned_mem m_hostch_rd_pointer;
  struct pinned_mem m_hostch_wr_pointer;
  
  // keep track of push end pointer
  size_t rd_buf_end_pointer;
  
  // keep track of pull front pointer
  size_t wr_buf_front_pointer;
  
  // User and driver thread synchronizer
  int thread_sync_valid;
  size_t *user_thread_sync;
  
  dma_addr_t user_thread_sync_bus_addr;
  
  struct pinned_mem m_sync_thread_pointer;
  
};

struct dma_desc_entry {                  
  u32 src_addr_ldw;
  u32 src_addr_udw;
  u32 dest_addr_ldw;
  u32 dest_addr_udw;
  u32 ctl_dma_len;
  u32 reserved[3];
} __attribute__ ((packed));

struct dma_desc_header {
    volatile u32 flags[ACL_PCIE_DMA_DESC_MAX_ENTRIES];
} __attribute__ ((packed));


struct dma_desc_table {
  struct dma_desc_header header;
    struct dma_desc_entry descriptors[ACL_PCIE_DMA_DESC_MAX_ENTRIES];
} __attribute__ ((packed));

struct aclpci_dma {

  // hostchannel struct
  struct aclpci_hostch_desc hostch_data;

  // Pci-E DMA IP description table
  struct dma_desc_table *desc_table_rd_cpu_virt_addr;
  struct dma_desc_table *desc_table_wr_cpu_virt_addr;

  dma_addr_t desc_table_rd_bus_addr;
  dma_addr_t desc_table_wr_bus_addr;

  // Local copy of last transfer id. Read once when DMA transfer starts
  int dma_rd_last_id;
  int dma_wr_last_id;
  int m_page_last_id;

  // Pinned memory we're currently building DMA transactions for
  struct pinned_mem m_active_mem;
  struct pinned_mem m_pre_pinned_mem;
  struct pinned_mem m_done_mem;

  // The transaction we are currently working on
  unsigned long m_cur_dma_addr;
  int m_handle_last;

  struct pci_dev *m_pci_dev;
  struct aclpci_dev *m_aclpci;

  // workqueue and work structure for bottom-half interrupt routine
  struct workqueue_struct *my_wq;
  struct work_struct_t *my_work;
  
  // Transfer information
  size_t m_device_addr;
  void* m_host_addr;
  int m_read;
  size_t m_bytes;
  size_t m_bytes_sent;
  int m_idle;

  u64 m_update_time, m_pin_time, m_start_time;
  u64 m_lock_time, m_unlock_time;
  
  // Time measured to us accuracy to measure DMA transfer time
  struct timeval m_us_dma_start_time;
  int m_us_valid;
};

#else
struct aclpci_dma {};
#endif
