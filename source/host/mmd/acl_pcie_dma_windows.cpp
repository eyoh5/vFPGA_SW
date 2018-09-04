// (C) 1992-2017 Intel Corporation.                            
// Intel, the Intel logo, Intel, MegaCore, NIOS II, Quartus and TalkBack words    
// and logos are trademarks of Intel Corporation or its subsidiaries in the U.S.  
// and/or other countries. Other marks and brands may be claimed as the property  
// of others. See Trademarks on intel.com for full list of Intel trademarks or    
// the Trademarks & Brands Names Database (if Intel) or See www.Intel.com/legal (if Altera) 
// Your use of Intel Corporation's design tools, logic functions and other        
// software and tools, and its AMPP partner logic functions, and any output       
// files any of the foregoing (including device programming or simulation         
// files), and any associated documentation or information are expressly subject  
// to the terms and conditions of the Altera Program License Subscription         
// Agreement, Intel MegaCore Function License Agreement, or other applicable      
// license agreement, including, without limitation, that your use is for the     
// sole purpose of programming logic devices manufactured by Intel and sold by    
// Intel or its authorized distributors.  Please refer to the applicable          
// agreement for further details.                                                 


/* ===- acl_pcie_dma_windows.cpp  ------------------------------------- C++ -*-=== */
/*                                                                                 */
/*                         Intel(R) OpenCL MMD Driver                                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */
/*                                                                                 */
/* This file implements the class to handle Windows-specific DMA operations.       */
/* The declaration of the class lives in the acl_pcie_dma_windows.h                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */


#if defined(WINDOWS)

// common and its own header files
#include "acl_pcie.h"
#include "acl_pcie_dma_windows.h"

// other header files inside MMD driver
#include "acl_pcie_device.h"
#include "acl_pcie_mm_io.h"
#include "acl_pcie_timer.h"
#include "acl_pcie_debug.h"
#include <iostream>
#include <stdlib.h>


#define ACL_PCIE_DMA_DEBUG(m, ...)  ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_BLOCKTX, m, __VA_ARGS__)



// The callback function to be scheduled inside the interrupt handler
// It will release the semaphore to allow new work to be scheduled and 
// perform the dma update function
void CALLBACK myWorkCallback(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WORK work){
   ACL_PCIE_DMA *m_dma = (ACL_PCIE_DMA *)context;

   ReleaseSemaphore(m_dma->m_workqueue_semaphore, 1, NULL);
   
   m_dma->update(true);
}

void CALLBACK myWorkUnpinCallback(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WORK work) {
   ACL_PCIE_DMA *m_dma = (ACL_PCIE_DMA *)context;
   
   m_dma->unpin_from_queue();
}

void CALLBACK myWorkPinCallback(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WORK work) {
   ACL_PCIE_DMA *m_dma = (ACL_PCIE_DMA *)context;
   
   m_dma->prepin_memory();
}

ACL_PCIE_DMA::ACL_PCIE_DMA( WDC_DEVICE_HANDLE dev, ACL_PCIE_MM_IO_MGR *io, ACL_PCIE_DEVICE *pcie ) :
   m_event( NULL ),
   m_pcie( NULL ),
   m_io( NULL ),
   m_timer( NULL )
{
   ACL_PCIE_ASSERT(dev  != INVALID_DEVICE, "passed in an invalid device when creating dma object.\n");
   ACL_PCIE_ASSERT(io   != NULL, "passed in an empty pointer for io when creating dma object.\n");
   ACL_PCIE_ASSERT(pcie != NULL, "passed in an empty pointer for pcie when creating dma object.\n");
   
   m_device = dev;
   m_io     = io;
   m_pcie   = pcie;
   
   HOSTCH_DESC *h = &hostch_data;

   const char *use_msi = getenv("ACL_PCIE_DMA_USE_MSI");
   if ( use_msi ) 
      m_use_polling = 0;
   else 
      m_use_polling = 1;

   memset( &m_active_mem, 0, sizeof(PINNED_MEM) );
   memset( &m_pre_pinned_mem, 0, sizeof(PINNED_MEM) );
   memset( &m_done_mem, 0, sizeof(PINNED_MEM) );
    
   // Initialize Host Channel
   memset( &h->m_hostch_rd_mem, 0, sizeof(PINNED_MEM) );
   memset( &h->m_hostch_wr_mem, 0, sizeof(PINNED_MEM) );
   memset( &h->m_hostch_rd_pointer, 0, sizeof(PINNED_MEM) );
   memset( &h->m_hostch_wr_pointer, 0, sizeof(PINNED_MEM) );
   memset( &h->m_sync_thread_pointer, 0, sizeof(PINNED_MEM) );
   h->push_valid = 0;
   h->pull_valid = 0;

   m_timer = new ACL_PCIE_TIMER();

   // create the threadpool to perform work the interrupt
   m_threadpool = CreateThreadpool(NULL);
   ACL_PCIE_ERROR_IF( m_threadpool == NULL, return, "failed to create threadpool.\n" );

   // set the number of work threads to 1 
   // so that no scheduled work will be running in parallel between them
   SetThreadpoolThreadMaximum(m_threadpool, 1);  
   bool status = SetThreadpoolThreadMinimum(m_threadpool, 1);
   ACL_PCIE_ERROR_IF( status == false, return, "failed to set # of work thread to 1.\n" );

   // create the work for threadpool and its semaphore
   InitializeThreadpoolEnvironment(&m_callback_env);   
   SetThreadpoolCallbackPool(&m_callback_env, m_threadpool);

   m_work = CreateThreadpoolWork(myWorkCallback, (void *)this, &m_callback_env);
   ACL_PCIE_ERROR_IF( m_work == NULL, return, "failed to create work for threadpool.\n" );
   
   m_workqueue_semaphore = CreateSemaphore(NULL, 1, 1, NULL);
   ACL_PCIE_ERROR_IF( m_workqueue_semaphore == NULL, return, "failed to create semaphore.\n" );
   
   ///////////////////////////////////////////////////////////////////////////////////////////
   // Unpin thread
   m_unpin_threadpool = CreateThreadpool(NULL);
   ACL_PCIE_ERROR_IF( m_unpin_threadpool == NULL, return, "failed to create threadpool.\n" );

   // set the number of work threads to 1 
   // so that no scheduled work will be running in parallel between them
   SetThreadpoolThreadMaximum(m_unpin_threadpool, 1);  
   status = SetThreadpoolThreadMinimum(m_unpin_threadpool, 1);
   ACL_PCIE_ERROR_IF( status == false, return, "failed to set # of work thread to 1.\n" );

   // create the work for threadpool and its semaphore
   InitializeThreadpoolEnvironment(&m_unpin_callback_env);   
   SetThreadpoolCallbackPool(&m_unpin_callback_env, m_unpin_threadpool);

   m_unpin_work = CreateThreadpoolWork(myWorkUnpinCallback, (void *)this, &m_unpin_callback_env);
   ACL_PCIE_ERROR_IF( m_unpin_work == NULL, return, "failed to create work for unpin threadpool.\n" );
   
   ///////////////////////////////////////////////////////////////////////////////////////////
   // pin thread
   m_pin_threadpool = CreateThreadpool(NULL);
   ACL_PCIE_ERROR_IF( m_pin_threadpool == NULL, return, "failed to create threadpool.\n" );

   // set the number of work threads to 1 
   // so that no scheduled work will be running in parallel between them
   SetThreadpoolThreadMaximum(m_pin_threadpool, 1);  
   status = SetThreadpoolThreadMinimum(m_pin_threadpool, 1);
   ACL_PCIE_ERROR_IF( status == false, return, "failed to set # of work thread to 1.\n" );

   // create the work for threadpool and its semaphore
   InitializeThreadpoolEnvironment(&m_pin_callback_env);   
   SetThreadpoolCallbackPool(&m_pin_callback_env, m_pin_threadpool);

   m_pin_work = CreateThreadpoolWork(myWorkPinCallback, (void *)this, &m_pin_callback_env);
   ACL_PCIE_ERROR_IF( m_pin_work == NULL, return, "failed to create work for unpin threadpool.\n" );
   
   
   ///////////////////////////////////////////////////////////////////////////////////////////
   // Contiguous DMA'able memory allocation for descriptor table
   
   DWORD WD_status;
   DWORD lock_options = static_cast<DWORD>(DMA_TO_FROM_DEVICE | DMA_ALLOW_64BIT_ADDRESS);
   size_t desc_table_size = sizeof(struct DMA_DESC_TABLE);
   size_t page_table_size = sizeof(struct HOSTCH_TABLE);

   // Lock DMA_DESC_TABLE
   WD_status = WDC_DMAContigBufLock(m_device, (PVOID *) &m_table_virt_addr, lock_options, (DWORD)desc_table_size, &m_table_dma_addr);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMAContigBufLock function failed.\n" );
   WD_status = WDC_DMASyncCpu( m_table_dma_addr );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMASyncCpu function failed.\n" );
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] Successfully locked DMA descriptor table memory.\n" );
   ACL_PCIE_ASSERT( m_table_dma_addr->dwPages == 1, "WDC_DMAContigBufLock function allocated more than 1 page.\n" );
   m_table_dma_phys_addr = m_table_dma_addr->Page[0].pPhysicalAddr;

   // Lock HOSTCH_TABLE push channel
   WD_status = WDC_DMAContigBufLock(m_device, (PVOID *) &h->push_page_table, lock_options, (DWORD)page_table_size, &hostch_data.push_page_table_addr);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMAContigBufLock function for Hostchannel failed. \n");
   WD_status = WDC_DMASyncCpu( hostch_data.push_page_table_addr );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMASyncCpu function for Hostchannel failed\n");
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] Successfully locked descriptor table for Hostchannel memory.\n" );
   ACL_PCIE_ASSERT( hostch_data.push_page_table_addr->dwPages == 1, "WDC_DMAContigBufLock function for HostChannel allocated more than 1 page.\n");
   hostch_data.push_page_table_bus_addr = hostch_data.push_page_table_addr->Page[0].pPhysicalAddr;

   // Lock HOSTCH_TABLE pull channel
   WD_status = WDC_DMAContigBufLock(m_device, (PVOID *) &h->pull_page_table, lock_options, (DWORD)page_table_size, &hostch_data.pull_page_table_addr);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMAContigBufLock function for Hostchannel failed. \n");
   WD_status = WDC_DMASyncCpu( hostch_data.pull_page_table_addr );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMASyncCpu function for Hostchannel failed\n");
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] Successfully locked descriptor table memory.\n" );
   ACL_PCIE_ASSERT( hostch_data.pull_page_table_addr->dwPages == 1, "WDC_DMAContigBufLock function for HostChannel allocated more than 1 page.\n");
   hostch_data.pull_page_table_bus_addr = hostch_data.pull_page_table_addr->Page[0].pPhysicalAddr;

   // set idle status to true when finish initialization
   m_idle = true;
}

ACL_PCIE_DMA::~ACL_PCIE_DMA()
{
   DWORD WD_status;
   stall_until_idle();
   
   // make sure no more work queued for threadpool
   WaitForThreadpoolWorkCallbacks(m_work, FALSE);

   // hostch_destroy is expected to be called by user but to make sure, call in the destructor
   hostch_destroy(ACL_HOST_CHANNEL_0);
   hostch_destroy(ACL_HOST_CHANNEL_1);

   // Unlock all the previously allocated tables from the constructor
   WD_status = WDC_DMABufUnlock(m_table_dma_addr);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMABufUnlock was not successful\n");
   WD_status = WDC_DMABufUnlock(hostch_data.push_page_table_addr);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMABufUnlock for HostChannel push page table addr was not successful\n");
   WD_status = WDC_DMABufUnlock(hostch_data.pull_page_table_addr);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMABufUnlock for HostChannel pull page table addr was not successful\n");

   CloseHandle(m_workqueue_semaphore);
   CloseThreadpoolWork(m_work);
   CloseThreadpool(m_threadpool);
   
   CloseThreadpoolWork(m_unpin_work);
   CloseThreadpool(m_unpin_threadpool);
   
   CloseThreadpoolWork(m_pin_work);
   CloseThreadpool(m_pin_threadpool);
   
   if( m_timer ) { delete m_timer;  m_timer = NULL; }
}

int ACL_PCIE_DMA::check_dma_interrupt(unsigned int *dma_update)
{
   if (!m_use_polling) {
      if (m_last_id > 0 && m_last_id <= ACL_PCIE_DMA_DESC_MAX_ENTRIES) {
         *dma_update = (m_table_virt_addr->header.flags[m_last_id-1]);
      } else {
         return 1;
      }
   }
   return 0;
}

void ACL_PCIE_DMA::unpin_from_queue()
{
   DWORD WD_status;
   ACL_PCIE_ASSERT( !m_dma_unpin_pending.empty(), "m_dma_unpin_pending is empty but unpin mem thread was called\n" );
   
   WD_DMA *dma = m_dma_unpin_pending.front();
   m_dma_unpin_pending.pop();
   
   WD_status = WDC_DMASyncIo(dma);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMASyncIo function failed.\n" );      
   WD_status = WDC_DMABufUnlock(dma );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMABufUnlock function failed.\n" );
}

void ACL_PCIE_DMA::prepin_memory()
{
   pin_memory(&m_pre_pinned_mem, true);
}

void ACL_PCIE_DMA::wait_finish()
{
   UINT32 wait_timer;
   
   while (1) {
      wait_timer = ACL_PCIE_DMA_TIMEOUT;
      while (wait_timer > 0)
      {
         wait_timer--;

         if (m_table_virt_addr->header.flags[m_last_id-1] == 1) {
            ACL_PCIE_DMA_DEBUG( ":::: [DMA] Wait done\n" );
            set_desc_table_header();
            if( WaitForSingleObject(m_workqueue_semaphore, 0L) == WAIT_OBJECT_0 ){
               SubmitThreadpoolWork(m_work);
            }
            return;
         }
      }
      
      ACL_PCIE_DMA_DEBUG( ":::: [DMA] Wait timed out. Sleeping for 1ms.\n" );
      Sleep(1);
  }
}

void ACL_PCIE_DMA::send_dma_desc()
{
   // Disabling interrupt is used in hostch_create function during polling

   if (m_read) {
      m_io->dma->write32(ACL_PCIE_DMA_RC_WR_DESC_BASE_LOW, m_table_dma_phys_addr & 0xffffffffUL);
      m_io->dma->write32(ACL_PCIE_DMA_RC_WR_DESC_BASE_HIGH, m_table_dma_phys_addr >> 32);
      m_io->dma->write32(ACL_PCIE_DMA_EP_WR_FIFO_BASE_LOW, ACL_PCIE_DMA_ONCHIP_WR_FIFO_BASE_LO);
      m_io->dma->write32(ACL_PCIE_DMA_EP_WR_FIFO_BASE_HIGH, ACL_PCIE_DMA_ONCHIP_WR_FIFO_BASE_HI);
      m_io->dma->write32(ACL_PCIE_DMA_WR_TABLE_SIZE, ACL_PCIE_DMA_TABLE_SIZE-1);
      if (m_interrupt_disabled)
         m_io->dma->write32(ACL_PCIE_DMA_WR_INT_CONTROL, ACL_PCIE_DMA_DISABLE_INT);
      else
         m_io->dma->write32(ACL_PCIE_DMA_WR_INT_CONTROL, ACL_PCIE_DMA_ENABLE_INT);
      MemoryBarrier();
      m_io->dma->write32(ACL_PCIE_DMA_WR_LAST_PTR, m_last_id-1);
   } else {
      m_io->dma->write32(ACL_PCIE_DMA_RC_RD_DESC_BASE_LOW, m_table_dma_phys_addr & 0xffffffffUL);
      m_io->dma->write32(ACL_PCIE_DMA_RC_RD_DESC_BASE_HIGH, m_table_dma_phys_addr >> 32);
      m_io->dma->write32(ACL_PCIE_DMA_EP_RD_FIFO_BASE_LOW, ACL_PCIE_DMA_ONCHIP_RD_FIFO_BASE_LO);
      m_io->dma->write32(ACL_PCIE_DMA_EP_RD_FIFO_BASE_HIGH, ACL_PCIE_DMA_ONCHIP_RD_FIFO_BASE_HI);
      m_io->dma->write32(ACL_PCIE_DMA_RD_TABLE_SIZE, ACL_PCIE_DMA_TABLE_SIZE-1);
      if (m_interrupt_disabled)
         m_io->dma->write32(ACL_PCIE_DMA_RD_INT_CONTROL, ACL_PCIE_DMA_DISABLE_INT);
      else
         m_io->dma->write32(ACL_PCIE_DMA_RD_INT_CONTROL, ACL_PCIE_DMA_ENABLE_INT);
      MemoryBarrier();
      m_io->dma->write32(ACL_PCIE_DMA_RD_LAST_PTR, m_last_id-1);
   }
}

void ACL_PCIE_DMA::setup_dma_desc()
{
   m_io->dma->write32(ACL_PCIE_DMA_RC_WR_DESC_BASE_LOW, m_table_dma_phys_addr & 0xffffffffUL);
   m_io->dma->write32(ACL_PCIE_DMA_RC_WR_DESC_BASE_HIGH, m_table_dma_phys_addr >> 32);
   m_io->dma->write32(ACL_PCIE_DMA_EP_WR_FIFO_BASE_LOW, ACL_PCIE_DMA_ONCHIP_WR_FIFO_BASE_LO);
   m_io->dma->write32(ACL_PCIE_DMA_EP_WR_FIFO_BASE_HIGH, ACL_PCIE_DMA_ONCHIP_WR_FIFO_BASE_HI);
   m_io->dma->write32(ACL_PCIE_DMA_WR_TABLE_SIZE, ACL_PCIE_DMA_TABLE_SIZE-1);

   m_io->dma->write32(ACL_PCIE_DMA_RC_RD_DESC_BASE_LOW, m_table_dma_phys_addr & 0xffffffffUL);
   m_io->dma->write32(ACL_PCIE_DMA_RC_RD_DESC_BASE_HIGH, m_table_dma_phys_addr >> 32);
   m_io->dma->write32(ACL_PCIE_DMA_EP_RD_FIFO_BASE_LOW, ACL_PCIE_DMA_ONCHIP_RD_FIFO_BASE_LO);
   m_io->dma->write32(ACL_PCIE_DMA_EP_RD_FIFO_BASE_HIGH, ACL_PCIE_DMA_ONCHIP_RD_FIFO_BASE_HI);
   m_io->dma->write32(ACL_PCIE_DMA_RD_TABLE_SIZE, ACL_PCIE_DMA_TABLE_SIZE-1);  
}

void ACL_PCIE_DMA::set_read_desc(DMA_ADDR source, UINT64 dest, UINT32 ctl_dma_len)
{
   m_active_descriptor->src_addr_ldw = (source & 0xffffffffUL);
   m_active_descriptor->src_addr_udw = (source >> 32);
   m_active_descriptor->dest_addr_ldw = (dest & 0xffffffffUL);
   m_active_descriptor->dest_addr_udw = (dest >> 32);
   m_active_descriptor->ctl_dma_len = (ctl_dma_len | (m_last_id << 18));
   m_active_descriptor->reserved[0] = 0;
   m_active_descriptor->reserved[1] = 0;
   m_active_descriptor->reserved[2] = 0;
}

void ACL_PCIE_DMA::set_write_desc(UINT64 source, DMA_ADDR dest, UINT32 ctl_dma_len)
{
   m_active_descriptor->src_addr_ldw = (source & 0xffffffffUL);
   m_active_descriptor->src_addr_udw = (source >> 32);
   m_active_descriptor->dest_addr_ldw = (dest & 0xffffffffUL);
   m_active_descriptor->dest_addr_udw = (dest >> 32);
   m_active_descriptor->ctl_dma_len = (ctl_dma_len | (m_last_id << 18));
   m_active_descriptor->reserved[0] = 0;
   m_active_descriptor->reserved[1] = 0;
   m_active_descriptor->reserved[2] = 0;
}

void ACL_PCIE_DMA::set_hostch_page_entry(HOSTCH_ENTRY *page_entry, UINT64 page_addr, UINT32 page_num)
{
   page_entry->page_addr_ldw = (page_addr & 0xffffffffUL);
   page_entry->page_addr_udw = (page_addr >> 32);
   page_entry->page_num = page_num;
   page_entry->reserved[0] = 0;
   page_entry->reserved[1] = 0;
   page_entry->reserved[2] = 1;
   page_entry->reserved[3] = 0;
   page_entry->reserved[4] = 0;
}

void ACL_PCIE_DMA::set_desc_table_header()
{
   int i;
   for (i = 0; i < ACL_PCIE_DMA_DESC_MAX_ENTRIES; i++)
      m_table_virt_addr->header.flags[i] = 0;
}

// Perform operations required when a DMA interrupt comes
void ACL_PCIE_DMA::service_interrupt()
{
   if (!m_use_polling) {
      // only submit a new work to the pool when there is not work in queued
      if( WaitForSingleObject(m_workqueue_semaphore, 0L) == WAIT_OBJECT_0 ){
         set_desc_table_header();
         SubmitThreadpoolWork(m_work);
      }
   }
}

void ACL_PCIE_DMA::spin_loop_ns(UINT64 wait_ns)
{
   cl_ulong start = m_timer->get_time_ns();
   cl_ulong finish;
   
   do {
      finish = m_timer->get_time_ns();
   } while(finish-start < wait_ns);
}


void ACL_PCIE_DMA::check_last_id(UINT32 *last_id)
{
   ACL_PCIE_ASSERT( *last_id <= (ACL_PCIE_DMA_RESET_ID+1), "last id was greater than 255.\n" );
   
   if (*last_id == (ACL_PCIE_DMA_RESET_ID+1)) {
      *last_id = 0;
      return;
   } else if (*last_id == ACL_PCIE_DMA_TABLE_SIZE) {
      *last_id = 0;
      return;
   }
   ACL_PCIE_ASSERT( *last_id < (ACL_PCIE_DMA_TABLE_SIZE), "last id was greater than 127.\n" );
}

// Relinquish the CPU to let any other thread to run
// Return 0 since there is no useful work to be performed here
int ACL_PCIE_DMA::yield()
{
   Sleep(0);
   return 0;
}

// Add a byte-offset to a void* pointer
inline void *ACL_PCIE_DMA::compute_address( void* base, uintptr_t offset )
{
   uintptr_t p = reinterpret_cast<uintptr_t>(base);
   return reinterpret_cast<void*>(p + offset);
}

int ACL_PCIE_DMA::hostch_buffer_lock(void *addr, size_t len, PINNED_MEM *new_mem, int direction)
{ 
   DWORD WD_status;
   // No active segment of pinned memory - pin one
   DWORD lock_options = static_cast<DWORD>((direction?DMA_FROM_DEVICE:DMA_TO_DEVICE) | DMA_ALLOW_64BIT_ADDRESS);
   
   WD_status = WDC_DMASGBufLock(m_device, addr, lock_options, (DWORD)len, &new_mem->dma );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "HostCh : WDC_DMAContigBufLock function for Hostchannel failed.\n" );
   WD_status = WDC_DMASyncCpu( new_mem->dma );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "HostCh: WDC_DMASyncCpu function for Hostchannel failed.\n" );

   new_mem->pages_rem = new_mem->dma->dwPages;
   new_mem->next_page = new_mem->dma->Page;
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh Pinning 0x%x bytes at 0x%p.\n", len, addr );
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh Pinned %d pages for 0x%x bytes of memory.\n", new_mem->dma->dwPages, new_mem->dma->dwBytes );
    
   return 0;
}


// Only 1 pin_memory can be running at a time
void ACL_PCIE_DMA::pin_memory(PINNED_MEM *new_mem, bool prepin)
{
   DWORD WD_status;
   // No active segment of pinned memory - pin one
   DWORD lock_options = static_cast<DWORD>((m_read?DMA_FROM_DEVICE:DMA_TO_DEVICE) | DMA_ALLOW_64BIT_ADDRESS);
   m_bytes_rem    = prepin ? (m_bytes_rem-m_last_pinned_size) : (m_bytes-m_bytes_sent);
   UINT32 last_id = prepin ? 0 : m_last_id;
   check_last_id(&last_id);
   size_t last_id_size_offset = last_id*PAGE_SIZE;
   size_t lock_size    = (m_bytes_rem > ACL_PCIE_DMA_MAX_PINNED_MEM_SIZE-last_id_size_offset) ? ACL_PCIE_DMA_MAX_PINNED_MEM_SIZE-last_id_size_offset : m_bytes_rem;
   void* lock_addr    = prepin ? compute_address(m_last_pinned_addr, m_last_pinned_size) : compute_address(m_host_addr, m_bytes_sent);
   uintptr_t last_page_portion = (reinterpret_cast<uintptr_t>(lock_addr) + lock_size) & ACL_PCIE_DMA_PAGE_ADDR_MASK;
   // If doing max pinning, check if will *end* on page boundary. If not, better
   // to pin a bit less and end up on the boundary. This way, will have fewer
   // descriptors to send.
   if (lock_size == (ACL_PCIE_DMA_MAX_PINNED_MEM_SIZE-last_id_size_offset) && last_page_portion != 0) {
      lock_size -= (size_t)last_page_portion;
      m_prepin_handle_last = 0;
      m_handle_last = prepin ? m_handle_last : 0;
   } else if (last_page_portion != 0) {
      m_prepin_handle_last = prepin ? 1 : 0;
      m_handle_last = prepin ? m_handle_last : 1;
   } else {
      m_prepin_handle_last = 0;
      m_handle_last = prepin ? m_handle_last : 0;
   }
   
   assert(lock_size < MAXDWORD);
   WD_status = WDC_DMASGBufLock(m_device, lock_addr, lock_options, (DWORD)lock_size, &new_mem->dma );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMASGBufLock function failed.\n" );
   WD_status = WDC_DMASyncCpu( new_mem->dma );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMASyncCpu function failed.\n" );

   new_mem->pages_rem = new_mem->dma->dwPages;
   new_mem->next_page = new_mem->dma->Page;
   
   m_last_pinned_size = lock_size;
   m_last_pinned_addr = lock_addr;

   ACL_PCIE_DMA_DEBUG( ":::: [DMA] Pinning 0x%x bytes at 0x%p.\n", lock_size, lock_addr );
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] Pinned %d pages for 0x%x bytes of memory.\n", new_mem->dma->dwPages, new_mem->dma->dwBytes );

}

// Unpin Memory
void ACL_PCIE_DMA::unpin_memory(PINNED_MEM *old_mem)
{
   WD_DMA *dma = old_mem->dma;
   DWORD WD_status;
   
   WD_status = WDC_DMASyncIo(dma);
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMASyncIo function failed.\n" );      
   WD_status = WDC_DMABufUnlock(dma );
   ACL_PCIE_ASSERT( WD_status == WD_STATUS_SUCCESS, "WDC_DMABufUnlock function failed.\n" );
   old_mem->dma = NULL;
}

// Handle pages unaligned to 4KB.
// DMA engine can only transfer power of 2 sizes.
// Transfer 2KB, 1KB, 512B, 256B, 128B then 64B.
void ACL_PCIE_DMA::non_aligned_page_handler()
{
   DWORD i;
   UINT32 max_transfer;
   UINT32 transfer_bytes_w, transfer_bytes, transfer_words, largest_remaining_chunk;
   
   check_last_id(&m_last_id);
   max_transfer = ACL_PCIE_DMA_TABLE_SIZE - m_last_id;
   
   largest_remaining_chunk = 0;
   transfer_bytes_w = ACL_PCIE_DMA_NON_ALIGNED_TRANS_LOG;
   largest_remaining_chunk = m_remaining_first_page >> transfer_bytes_w;
   while (largest_remaining_chunk == 0)
   {
      ACL_PCIE_ASSERT( transfer_bytes_w > 1, "DMA engine detected less than 4Bytes transfer. Not supported.\n" );
      transfer_bytes_w--;
      largest_remaining_chunk = m_remaining_first_page >> transfer_bytes_w;
   }
   
   transfer_bytes = 1i64 << transfer_bytes_w;
   transfer_words = transfer_bytes/4;
   
   if (largest_remaining_chunk >= max_transfer) {
      for (i = 0; i < max_transfer; i++) {
         m_active_descriptor = &(m_table_virt_addr->descriptors[i]);
         if (m_read) {
            set_write_desc(m_dev_addr, m_first_page.pPhysicalAddr, transfer_words);
         } else {
            set_read_desc(m_first_page.pPhysicalAddr, m_dev_addr, transfer_words);
         }
         m_last_id++;
         m_dev_addr += transfer_bytes;
         m_first_page.pPhysicalAddr += transfer_bytes;
      }
      largest_remaining_chunk = max_transfer;
      m_remaining_first_page -= transfer_bytes*max_transfer;
   } else if (largest_remaining_chunk > 0) {
      for (i = 0; i < largest_remaining_chunk; i++) {
         m_active_descriptor = &(m_table_virt_addr->descriptors[i]);
         if (m_read) {
            set_write_desc(m_dev_addr, m_first_page.pPhysicalAddr, transfer_words);
         } else {
            set_read_desc(m_first_page.pPhysicalAddr, m_dev_addr, transfer_words);
         }
         m_last_id++;
         m_dev_addr += transfer_bytes;
         m_first_page.pPhysicalAddr += transfer_bytes;
      }
      m_remaining_first_page -= transfer_bytes*largest_remaining_chunk;
   }
   
   m_bytes_sent += transfer_bytes*largest_remaining_chunk;
   MemoryBarrier();
   m_interrupt_disabled = FALSE;
   send_dma_desc();
   MemoryBarrier();
   
   if (m_remaining_first_page == 0)
   {
      ++m_active_mem.next_page;
      --m_active_mem.pages_rem;
   }
}

// Check if user's 'ack' API updated end pointer of circular buf
// Update end pointer in IP
int ACL_PCIE_DMA::hostch_push_update ()
{
   HOSTCH_DESC *h = &hostch_data;

   if (h->rd_buf_end_pointer != *h->user_rd_end_pointer){
      h->rd_buf_end_pointer = *h->user_rd_end_pointer;
   } else {
      h->loop_counter = (h->loop_counter > 0) ? h->loop_counter - 1 : h->loop_counter;
      return 1;
   }
   h->loop_counter = HOSTCH_LOOP_COUNTER;

   m_io->dma->write32(ACL_HOST_CHANNEL_0_HOST_ENDP, (UINT32) h->rd_buf_end_pointer);

   return 0;
}

// Check if user's 'ack' API updated front pointer of circular buf
// Update end pointer in IP
int ACL_PCIE_DMA::hostch_pull_update ()
{
   HOSTCH_DESC *h = &hostch_data;

   if (h->wr_buf_front_pointer != *h->user_wr_front_pointer){
      h->wr_buf_front_pointer = *h->user_wr_front_pointer;
   } else {
      h->loop_counter = (h->loop_counter > 0) ? h->loop_counter - 1 : h->loop_counter;
      return 1;
   }
   h->loop_counter = HOSTCH_LOOP_COUNTER;

   m_io->dma->write32(ACL_HOST_CHANNEL_1_HOST_FRONTP, h->wr_buf_front_pointer);
   return 0;
}


// Transfer data between host and device
// This function returns right after the transfer is scheduled
// Return 0 on success
int ACL_PCIE_DMA::read_write(void *host_addr, size_t dev_addr, size_t bytes, aocl_mmd_op_t e, bool reading)
{
   ACL_PCIE_ASSERT( m_event == NULL, "non-empty event before a new DMA read/write.\n" );
   ACL_PCIE_ASSERT( m_active_mem.dma == NULL, "there is still active pinned memory before a new DMA read/write.\n" );


   // Copy the parameters over and mark the job as running
   m_event     = e;
   m_read      = reading;
   m_bytes     = bytes;
   m_host_addr = host_addr;
   m_dev_addr  = dev_addr;

   // Start processing the request
   m_bytes_sent               = 0;
   m_handle_last              = 0;
   m_last_id                  = ACL_PCIE_DMA_RESET_ID;
   m_prepinned                = 0;
   
   if (m_read) {
      m_io->dma->read32( ACL_PCIE_DMA_WR_LAST_PTR, &m_last_id );
      m_last_id++;
   }
   else {
      m_io->dma->read32( ACL_PCIE_DMA_RD_LAST_PTR, &m_last_id );
      m_last_id++;
   }

   m_idle                     = false;

   // setup the work inside the threadpool to perform the first DMA transaction
   ACL_PCIE_ERROR_IF( WaitForSingleObject(m_workqueue_semaphore, 0L) != WAIT_OBJECT_0, return -1, 
            "failed to schedule the first work for DMA read/write.\n" );

   SubmitThreadpoolWork(m_work);
  
   return 0; // success
}


// function to be scheduled to execute whenever an interrupt arrived
bool ACL_PCIE_DMA::update( bool forced )
{
   cl_ulong start;
   int status;
   UINT32 max_transfer;
   unsigned int i;
   HOSTCH_DESC *h = &hostch_data;


   if(!forced)
      return false;

   if (h->pull_valid && m_idle) {
      // Check user memory to see if there was update to user buffer pointer for pull
      status = hostch_pull_update();
   }
   
   if (h->push_valid && m_idle) {
      // Check user memory to see if there was update to user buffer pointer for push
      status = hostch_push_update();
   }
   
   if ((h->push_valid | h->pull_valid) && m_idle && (h->thread_sync_valid && h->loop_counter > 0)) {
      // setup the work inside the threadpool to perform the first DMA transaction
      ACL_PCIE_ERROR_IF( WaitForSingleObject(m_workqueue_semaphore, 0L) != WAIT_OBJECT_0, return false, 
               "HostCh : failed to schedule the first work for DMA read/write.\n" );
      SubmitThreadpoolWork(m_work);
      return false;

   } else if (m_idle && (h->thread_sync_valid && h->loop_counter == 0)) {
      *h->user_thread_sync = 0;
      return false;

   } else if (m_idle) {
      return false;
   }


   ACL_PCIE_DMA_DEBUG( ":::: [DMA] Bytes left %u\n", m_bytes-m_bytes_sent );
   // Process any descriptors that have completed
   set_desc_table_header();
   cl_ulong finish = 0;
   if ( ACL_PCIE_DEBUG >= VERBOSITY_BLOCKTX )
     finish = m_timer->get_time_ns();

   // Check if the transaction is complete
   if(m_bytes_sent == m_bytes)
   {
      if (m_active_mem.dma != NULL)
         unpin_memory(&m_active_mem);
      ACL_PCIE_DMA_DEBUG( ":::: [DMA] Transaction complete!\n" );
      ACL_PCIE_ASSERT( m_active_mem.dma == NULL, "there is still active pinned memory after the DMA read/write.\n" );
      WaitForThreadpoolWorkCallbacks(m_unpin_work, false);
      if (!m_dma_unpin_pending.empty()) {
         ACL_PCIE_DMA_DEBUG( ":::: [DMA] Done, but pinned memory still in queue. Wait until queue is empty.\n");
         if( WaitForSingleObject(m_workqueue_semaphore, 0L) == WAIT_OBJECT_0 ){
            SubmitThreadpoolWork(m_work);
         }
         Sleep(0);
         return true;
      }
      
      m_last_id = ACL_PCIE_DMA_RESET_ID;
      m_idle = true;
      
      if (m_event) 
      {
         // Use a temporary variable to save the event data and reset m_event before calling event_update_fn 
         // to avoid race condition that the main thread may start a new DMA transfer before this work-thread
         // is able to reset the m_event.
         aocl_mmd_op_t temp_event = m_event;
         m_event = NULL; 

         m_pcie->event_update_fn( temp_event, 0 );  
      }

     if ((h->push_valid | h->pull_valid) && (h->thread_sync_valid && h->loop_counter > 0)) {
         ACL_PCIE_ERROR_IF(WaitForSingleObject(m_workqueue_semaphore, 0L) != WAIT_OBJECT_0, return false, 
            "HostCh : failed to schedule the first work for DMA read/write.\n");
         SubmitThreadpoolWork(m_work);
      }

      return true;
   }
   
   // Check if we are done with previously pinned memory.
   if (m_active_mem.dma == NULL || m_active_mem.pages_rem == 0)
   {
      m_done_mem = m_active_mem;
      
      WaitForThreadpoolWorkCallbacks(m_pin_work, false);
      
      // Get pre-pinned memory if there are any.
      if (m_pre_pinned_mem.dma != NULL) {
         m_active_mem = m_pre_pinned_mem;
         m_pre_pinned_mem.dma = NULL;
         m_handle_last = m_prepin_handle_last;
         m_prepinned = 0;
      } else if (m_prepinned) {
         if( WaitForSingleObject(m_workqueue_semaphore, 0L) == WAIT_OBJECT_0 ){
            SubmitThreadpoolWork(m_work);
         }
         Sleep(1);
         return true;
      } else {
         pin_memory(&m_active_mem, false);
      }
      
      // Check if the first page is aligned to 4KB
      m_first_page.pPhysicalAddr = m_active_mem.next_page->pPhysicalAddr;
      // If we begin with an offset, we can't use the full page
      m_first_page.dwBytes =  m_active_mem.next_page->dwBytes;
      m_remaining_first_page = m_active_mem.next_page->dwBytes;
      m_aligned = true;
      ACL_PCIE_DMA_DEBUG( ":::: [DMA] First page has %u bytes\n", m_remaining_first_page );
   }
   
   // Transfer non-aligned first page.
   if ((m_remaining_first_page&ACL_PCIE_DMA_PAGE_ADDR_MASK) != 0)
   {
      m_aligned = false;
      ACL_PCIE_DMA_DEBUG( ":::: [DMA] Non-aligned page transfer. %u bytes left\n", m_remaining_first_page );
      non_aligned_page_handler();
      if (m_use_polling)
         wait_finish();
      return true;
   }
   
   // Transfer non-aligned last page.
   if (m_handle_last && (m_active_mem.pages_rem == 1))
   {
      m_first_page.pPhysicalAddr = m_active_mem.next_page->pPhysicalAddr;
      m_first_page.dwBytes =  m_active_mem.next_page->dwBytes;
      m_remaining_first_page = m_active_mem.next_page->dwBytes;
      ACL_PCIE_DMA_DEBUG( ":::: [DMA] Last page has %u bytes\n", m_remaining_first_page );
      non_aligned_page_handler();
      if (m_use_polling)
         wait_finish();
      return true;
   }
   
   // Main DMA execution
   // 1. Transfers up to 128 4KB aligned pages
   // 2. Launch a thread to unpin memory
   // 3. Launch a thread to pre-pin next memory
   if (m_active_mem.pages_rem > m_handle_last) {
      
      // Calculate how many descriptors can be sent
      check_last_id(&m_last_id);
      ACL_PCIE_DMA_DEBUG( ":::: [DMA] last id was %u\n", m_last_id );
      max_transfer = (m_active_mem.pages_rem - m_handle_last > ACL_PCIE_DMA_TABLE_SIZE - m_last_id) ?
            ACL_PCIE_DMA_TABLE_SIZE - m_last_id : m_active_mem.pages_rem - m_handle_last;
            
      ACL_PCIE_DMA_DEBUG( ":::: [DMA] max_transfer %u\n", max_transfer );

      // Build descriptor table
      for (i = 0; i < max_transfer; i++) {
         m_active_descriptor = &(m_table_virt_addr->descriptors[i]);
         if (m_read) {
            set_write_desc(m_dev_addr, m_active_mem.next_page->pPhysicalAddr, PAGE_SIZE/4);
            if ( m_active_mem.next_page->dwBytes == PAGE_SIZE) {
               ++m_active_mem.next_page;
               m_active_mem.pages_rem--;
            }
            else {
               ACL_PCIE_DMA_DEBUG( ":::: [DMA] page size is larger than 4K for read. Page size is %u bytes\n", m_active_mem.next_page->dwBytes );
               m_active_mem.next_page->dwBytes -= PAGE_SIZE;
               m_active_mem.next_page->pPhysicalAddr += PAGE_SIZE;
            }
            
            m_dev_addr += PAGE_SIZE;
            m_bytes_sent += PAGE_SIZE;
            
         } else {
            set_read_desc(m_active_mem.next_page->pPhysicalAddr, m_dev_addr, PAGE_SIZE/4);
            if ( m_active_mem.next_page->dwBytes == PAGE_SIZE) {
               ++m_active_mem.next_page;
               m_active_mem.pages_rem--;
            }
            else {
               ACL_PCIE_DMA_DEBUG( ":::: [DMA] page size is larger than 4K for write. Page size is %u bytes\n", m_active_mem.next_page->dwBytes );
               m_active_mem.next_page->dwBytes -= PAGE_SIZE;
               m_active_mem.next_page->pPhysicalAddr += PAGE_SIZE;
            }
            
            m_dev_addr += PAGE_SIZE;
            m_bytes_sent += PAGE_SIZE;
         }
         m_last_id++;
      }
     
      MemoryBarrier();
      // Send descriptor table to DMA
      start = m_timer->get_time_ns();
      m_interrupt_disabled = FALSE;
      send_dma_desc();
      int pinning = 0;
      int unpinning = 0;
      cl_ulong unpin_start = 0, unpin_finish = 0;
      
      // Launch unpin thread
      if (m_done_mem.dma != NULL)
      {
         unpin_start = m_timer->get_time_ns();
         unpinning = 1;
         
         // wait for previous unpin to finish
         WaitForThreadpoolWorkCallbacks(m_unpin_work, false);
         m_dma_unpin_pending.push( m_done_mem.dma );
         
         // Make sure Push into unpin queue comes before launching unpin thread
         MemoryBarrier();
         
         // Launch unpin thread
         SubmitThreadpoolWork(m_unpin_work);
         m_done_mem.dma = NULL;
         unpin_finish = m_timer->get_time_ns();
      }
      
      // Launch pre-pin thread
      cl_ulong pin_start=0, pin_finish=0;
      if (((m_bytes_rem-m_last_pinned_size) > 0) && (m_prepinned == 0) && (m_aligned))
      {
         pin_start = m_timer->get_time_ns();
         pinning = 1;
         m_prepinned = 1;
         
         // This wait should pass right through.
         // There is another wait above, before switching active and prepin memory
         WaitForThreadpoolWorkCallbacks(m_pin_work, false);
         SubmitThreadpoolWork(m_pin_work);
         pin_finish = m_timer->get_time_ns();
      }
      
      if (m_use_polling) {
         wait_finish();
         finish = m_timer->get_time_ns();
         ACL_PCIE_DMA_DEBUG(":::: [DMA] Descriptor (%d bytes) completed in %.2f us - %.2f MB/s :: pinning %i in %.2f us :: unpinning %i in %.2f us :: pages rem %i :: handle last %i\n", 
               max_transfer*4096,
               (finish - start) / 1000.0,
               1000000000.0 * max_transfer*4096 / (finish - start) / (1024.0 * 1024.0),
               pinning,
               (pin_finish - pin_start) / 1000.0,
               unpinning,
               (unpin_finish - unpin_start) / 1000.0,
               m_active_mem.pages_rem,
               m_handle_last);
      }

      return true;
   }
   
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] Nothing happened\n" );
   return true;
}

// Poll DMA transfer
// Only used during host channel create
// Used to transfer the page table of pinned down MMD circular buffer to host channel IP
// The size of this transfer is known to be small
void ACL_PCIE_DMA::poll_wait() {
   UINT32 wait_timer;

   while (1) {
      wait_timer = ACL_PCIE_DMA_TIMEOUT;
      while (wait_timer > 0)
      {
         wait_timer--;

         if(m_table_virt_addr->header.flags[m_last_id-1] == 1) {
            ACL_PCIE_DMA_DEBUG(":::: [DMA] HostCh : Wait done\n");
            set_desc_table_header();
            
            if(m_read)
               m_io->dma->write32(ACL_PCIE_DMA_WR_INT_CONTROL, ACL_PCIE_DMA_ENABLE_INT);
            else
               m_io->dma->write32(ACL_PCIE_DMA_RD_INT_CONTROL, ACL_PCIE_DMA_ENABLE_INT);
            m_interrupt_disabled = FALSE;
            
            return;
         }
         // Delay the CPU from checking the memory for 1us. CPU is still running this thread.
         // but reduces memory access from CPU
         spin_loop_ns(1000);
      }

     // If DMA hasn't finished yet, free up the CPU for 1ms
     ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh : Poll wait failed while transferring host channel page table to IP. Sleeping for 1ms.\n");
     Sleep(1);
   }
}

// Set IP's parameters for host channel.
// Parameters are txs address to write updated front/end pointer to on host memory,
// Address to DMA data to, to stream data into kernel
void ACL_PCIE_DMA::hostch_start(int channel)
{
   HOSTCH_DESC *h = &hostch_data;  

   if (channel == (int) ACL_HOST_CHANNEL_0_ID) {
      h->user_rd_front_pointer_bus_addr = h->m_hostch_rd_pointer.dma->Page[0].pPhysicalAddr;

      m_io->dma->write32(ACL_HOST_CHANNEL_0_TXS_ADDR_LOW, h->user_rd_front_pointer_bus_addr & 0xffffffffUL);
      m_io->dma->write32(ACL_HOST_CHANNEL_0_TXS_ADDR_HIGH, (h->user_rd_front_pointer_bus_addr)>>32);
      m_io->dma->write32(ACL_HOST_CHANNEL_0_IP_ADDR_LOW, ACL_HOST_CHANNEL_0_DMA_ADDR & 0xffffffffUL);
      m_io->dma->write32(ACL_HOST_CHANNEL_0_IP_ADDR_HIGH, ACL_HOST_CHANNEL_0_DMA_ADDR>>32);
      m_io->dma->write32(ACL_HOST_CHANNEL_0_BUF_SIZE, h->buffer_size);
      m_io->dma->write32(ACL_HOST_CHANNEL_0_HOST_ENDP, 0);
      m_io->dma->write32(ACL_HOST_CHANNEL_0_LOGIC_EN, 1);

   } else if (channel == (int) ACL_HOST_CHANNEL_1_ID) {  
      h->user_wr_end_pointer_bus_addr = h->m_hostch_wr_pointer.dma->Page[0].pPhysicalAddr + sizeof(size_t);

      m_io->dma->write32(ACL_HOST_CHANNEL_1_TXS_ADDR_LOW, h->user_wr_end_pointer_bus_addr & 0xffffffffUL);
      m_io->dma->write32(ACL_HOST_CHANNEL_1_TXS_ADDR_HIGH, (h->user_wr_end_pointer_bus_addr)>>32);
      m_io->dma->write32(ACL_HOST_CHANNEL_1_IP_ADDR_LOW, ACL_HOST_CHANNEL_1_DMA_ADDR & 0xffffffffUL);
      m_io->dma->write32(ACL_HOST_CHANNEL_1_IP_ADDR_HIGH, ACL_HOST_CHANNEL_1_DMA_ADDR>>32);
      m_io->dma->write32(ACL_HOST_CHANNEL_1_BUF_SIZE, h->buffer_size);
      m_io->dma->write32(ACL_HOST_CHANNEL_1_HOST_FRONTP, 0);
      m_io->dma->write32(ACL_HOST_CHANNEL_1_LOGIC_EN, 1);
   }
}

void ACL_PCIE_DMA::hostch_thread_sync(void *user_addr)
{
   int status;
   HOSTCH_DESC *h = &hostch_data;

   if ((user_addr == NULL) & (h->thread_sync_valid)) {
      if ((h->push_valid | h->pull_valid) && m_idle && (*h->user_thread_sync == 0)) {
         h->loop_counter = HOSTCH_LOOP_COUNTER;
         SubmitThreadpoolWork(m_work);
         *h->user_thread_sync = 1;
      }
   } else {
      status = hostch_buffer_lock(user_addr, sizeof(size_t), &(h->m_sync_thread_pointer), 1);
      h->user_thread_sync =  (size_t *) h->m_sync_thread_pointer.dma->pUserAddr;
      h->loop_counter = HOSTCH_LOOP_COUNTER;
      *h->user_thread_sync = 0;
      h->thread_sync_valid = 1;
   }
}


int ACL_PCIE_DMA::hostch_create(void *user_addr, void *buf_pointer, size_t size, int reading)
{
   int status, i;
   HOSTCH_DESC *h = &hostch_data;

   DMA_ADDR dma_address;
   h->buffer_size = size;

   setup_dma_desc();

   m_io->dma->read32( ACL_PCIE_DMA_RD_LAST_PTR, &m_last_id);
   ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh: read dma_rd_last_id %u\n", (unsigned) m_last_id);

   // Set variables before calling dma helper functions
   m_last_id++;
   m_read = 0;

   // Only create push channel if it's not already open
   if (reading && !h->push_valid) {
      h->user_rd_buffer = user_addr;

      // Pin push user buffer
      status = hostch_buffer_lock(user_addr, size, &(h->m_hostch_rd_mem), reading);
      status |= hostch_buffer_lock(buf_pointer, 2*sizeof(size_t), &(h->m_hostch_rd_pointer), 1); 

      // Map circular push buffer's end pointer so that the driver can poll on it for update from user space
      h->user_rd_front_pointer = (size_t *) h->m_hostch_rd_pointer.dma->pUserAddr;
      h->user_rd_end_pointer = h->user_rd_front_pointer+ 1;

      // Send the circular push buffer's pinned address to IP, so IP can initiate DMA transfer by itself.
      for (i = 0; i < (size/PAGE_SIZE); i++) {
         dma_address = h->m_hostch_rd_mem.next_page->pPhysicalAddr;
         set_hostch_page_entry(&(h->push_page_table->page_entry[i]), (UINT64) dma_address, (UINT32) i);
         ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh: push page entry[%u] = %#016x size = %#016x\n", (unsigned) i, (UINT64) dma_address, h->m_hostch_rd_mem.next_page->dwBytes);

         // Make 4KB pages from an array of pages of m_hostch_rd_mem
         // WDC_DMASGBufLock might have allocated 8KB instead of 4KB
         if(h->m_hostch_rd_mem.next_page->dwBytes == PAGE_SIZE) {
            ++h->m_hostch_rd_mem.next_page;
            h->m_hostch_rd_mem.pages_rem--;
         } else {
            h->m_hostch_rd_mem.next_page->dwBytes -= PAGE_SIZE;
            h->m_hostch_rd_mem.next_page->pPhysicalAddr += PAGE_SIZE;
         }
      }

      set_desc_table_header();
      check_last_id(&m_last_id);

      // Set variable before calling dma helper functions
      m_active_descriptor = &(m_table_virt_addr->descriptors[0]);
      set_read_desc(h->push_page_table_bus_addr, (UINT64)(ACL_PCIE_DMA_RD_FIFO_BASE), (32*size/PAGE_SIZE)/4);
      m_last_id++;

      // Read Interrupt will be disabled from send_dma_desc till poll_wait
      m_interrupt_disabled = TRUE;
      send_dma_desc();
      poll_wait();

      // Reset and enable the push channel on IP
      UINT32 data;
      m_io->pcie_cra->write32(HOSTCH_CONTROL_ADDR_PUSH + HOSTCH_BASE, 0);
      m_io->pcie_cra->read32(HOSTCH_CONTROL_ADDR_PUSH + HOSTCH_BASE, &data);
      m_io->pcie_cra->write32(HOSTCH_CONTROL_ADDR_PUSH + HOSTCH_BASE, 1);
      m_io->pcie_cra->read32(HOSTCH_CONTROL_ADDR_PUSH + HOSTCH_BASE, &data);

      // Set IP's control registers for push channel
      hostch_start((int) ACL_HOST_CHANNEL_0_ID);

      h->push_valid = 1;

      // Only launch queue if pull channel is not open and if there is no DMA transfer
      if (!h->pull_valid && m_idle) {
         ACL_PCIE_ERROR_IF( WaitForSingleObject(m_workqueue_semaphore, 0L) != WAIT_OBJECT_0, return -1, 
                  "HostCh : failed to schedule the first work for DMA read/write.\n" );
         SubmitThreadpoolWork(m_work);
      } 
      return 0;

   } else if ((reading == 0) && !h->pull_valid) {
      h->user_wr_buffer = user_addr;

      // Pin pull user buffer
      status = hostch_buffer_lock(user_addr, size, &(h->m_hostch_wr_mem), reading);
      status |= hostch_buffer_lock(buf_pointer, 2*sizeof(size_t), &(h->m_hostch_wr_pointer), 1);

      // Map circular pull buffer's end pointer so that the driver can poll on it for update from user space
      h->user_wr_front_pointer = (size_t *) h->m_hostch_wr_pointer.dma->pUserAddr;
      h->user_wr_end_pointer = h->user_wr_front_pointer + 1;

      // Send the circular pull buffer's pinned address to IP, so IP can initiate DMA transfer by itself.
      for (i = 0; i < (size/PAGE_SIZE); i++) {
         dma_address = h->m_hostch_wr_mem.next_page->pPhysicalAddr;
         set_hostch_page_entry(&(h->pull_page_table->page_entry[i]), (UINT64) dma_address, (UINT32) i);
         ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh: pull page entry[%u] = %#016x size = %#016x\n", (unsigned) i, (UINT64) dma_address, h->m_hostch_wr_mem.next_page->dwBytes);

         // Make 4KB pages from an array of pages of m_hostch_wr_mem
         // WDC_DMASGBufLock might have allocated 8KB instead of 4KB
         if(h->m_hostch_wr_mem.next_page->dwBytes == PAGE_SIZE) {
            ++h->m_hostch_wr_mem.next_page;
            h->m_hostch_wr_mem.pages_rem--;
         } else {  
            h->m_hostch_wr_mem.next_page->dwBytes -= PAGE_SIZE;
            h->m_hostch_wr_mem.next_page->pPhysicalAddr += PAGE_SIZE;      
         }
      }

      set_desc_table_header();
      check_last_id(&m_last_id);

      // Set variable before calling dma helper functions
      m_active_descriptor = &(m_table_virt_addr->descriptors[0]);
      set_read_desc(h->pull_page_table_bus_addr, (UINT64)(ACL_PCIE_DMA_WR_FIFO_BASE), (32*size/PAGE_SIZE)/4);
      m_last_id++;

      // Read Interrupt will be disabled from send_dma_desc till poll_wait
      m_interrupt_disabled = TRUE;
      send_dma_desc();
      poll_wait();

      // Reset and enable the pull channel on IP
      UINT32 temp;
      m_io->pcie_cra->write32(HOSTCH_CONTROL_ADDR_PULL + HOSTCH_BASE, 0);
      m_io->pcie_cra->read32(HOSTCH_CONTROL_ADDR_PULL + HOSTCH_BASE, &temp);
      m_io->pcie_cra->write32(HOSTCH_CONTROL_ADDR_PULL + HOSTCH_BASE, 1);
      m_io->pcie_cra->read32(HOSTCH_CONTROL_ADDR_PULL + HOSTCH_BASE, &temp);

      // Set IP's control registers for pull channel
      hostch_start((int) ACL_HOST_CHANNEL_1_ID);

      h->pull_valid = 1;

      // Only launch queue if push channel is not open and if there is no DMA transfer
      if (!h->push_valid && m_idle) {
      ACL_PCIE_ERROR_IF( WaitForSingleObject(m_workqueue_semaphore, 0L) != WAIT_OBJECT_0, return -1, 
               "HostCh : failed to schedule the first work for DMA read/write.\n" );
      SubmitThreadpoolWork(m_work);

      }
      return 0;

   } else {
      return ERROR_INVALID_CHANNEL;
   }
}

// Destroy channel call from user.
// Unlock all buffers and reset IP
int ACL_PCIE_DMA::hostch_destroy(int reading) {
   HOSTCH_DESC *h = &hostch_data;

   if (reading) {
      if (h->pull_valid) {
         ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh: destroying pull host channel.");
         m_io->dma->write32(ACL_HOST_CHANNEL_0_LOGIC_EN, 0);
         MemoryBarrier();
         m_io->pcie_cra->write32(HOSTCH_CONTROL_ADDR_PULL + HOSTCH_BASE, 0);
         MemoryBarrier();

         if (h->m_hostch_wr_mem.dma != NULL)
            unpin_memory(&h->m_hostch_wr_mem);
         if (h->m_hostch_wr_pointer.dma != NULL)
            unpin_memory(&h->m_hostch_wr_pointer);
         h->pull_valid = 0;

         if (!h->push_valid) {
            if (h->thread_sync_valid) {
               h->thread_sync_valid = 0;
               if (h->m_sync_thread_pointer.dma != NULL)
                  unpin_memory(&h->m_sync_thread_pointer);
            }
         if (m_idle)
            WaitForThreadpoolWorkCallbacks(m_work, false);
         }
      }
   } else if (!reading) {
      if (h->push_valid) {
         ACL_PCIE_DMA_DEBUG( ":::: [DMA] HostCh: destroying push host channel.");
         m_io->dma->write32(ACL_HOST_CHANNEL_1_LOGIC_EN, 0);
         MemoryBarrier();
         m_io->pcie_cra->write32(HOSTCH_CONTROL_ADDR_PUSH + HOSTCH_BASE, 0);
         MemoryBarrier();

         if (h->m_hostch_rd_mem.dma != NULL)
            unpin_memory(&h->m_hostch_rd_mem);
         if (h->m_hostch_rd_pointer.dma != NULL)
            unpin_memory(&h->m_hostch_rd_pointer);
         h->push_valid = 0;

         if (!h->pull_valid) {
            if (h->thread_sync_valid) {
               h->thread_sync_valid = 0;
               if (h->m_sync_thread_pointer.dma != NULL)
                  unpin_memory(&h->m_sync_thread_pointer);
            }
         if (m_idle)
            WaitForThreadpoolWorkCallbacks(m_work, false);
         }
      }
   }

   return 0;
}


#endif // WINDOWS

