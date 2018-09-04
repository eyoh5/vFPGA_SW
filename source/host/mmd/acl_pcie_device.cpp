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


/* ===- acl_pcie_device.cpp  ------------------------------------------ C++ -*-=== */
/*                                                                                 */
/*                         Intel(R) OpenCL MMD Driver                                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */
/*                                                                                 */
/* This file implements the class to handle operations on a single device.         */
/* The declaration of the class lives in the acl_pcie_device.h                     */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */


#if defined(WINDOWS)
#define NOMINMAX
#  include <time.h>
#endif   // WINDOWS

// common and its own header files
#include "acl_pcie.h"
#include "acl_pcie_device.h"

// other header files inside MMD driver
#include "acl_pcie_config.h"
#include "acl_pcie_dma.h"
#include "acl_pcie_mm_io.h"
#include "acl_pcie_debug.h"
#include "pkg_editor.h"

// other standard header files
#include <sstream>
#include <stdlib.h>
#include <fstream>
#include <string.h>
#include <limits>
#include "acl_pcie_hostch.h"

#if defined(LINUX)
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <sys/mman.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <unistd.h>
#endif   // LINUX



static int num_open_devices = 0;

#if defined(WINDOWS)
WDC_DEVICE_HANDLE open_device_windows(ACL_PCIE_DEVICE_DESCRIPTION *info, int dev_num);

// Interrupt service routine for all interrupts on the PCIe interrupt line
// PCIe interrupts in Windows XP are level-based.  The KMD is responsible for
// masking off the interrupt until this routine can service the request at
// user-mode priority.
extern void pcie_interrupt_handler( void* data );
#endif   // WINDOWS
#if defined(LINUX)
WDC_DEVICE_HANDLE open_device_linux(ACL_PCIE_DEVICE_DESCRIPTION *info, int dev_num);
#endif   // LINUX



ACL_PCIE_DEVICE::ACL_PCIE_DEVICE( int dev_num, const char *name, int handle, int user_signal_number ) :
   kernel_interrupt(NULL),
   kernel_interrupt_user_data(NULL),
   event_update(NULL),
   event_update_user_data(NULL),
   m_io( NULL ),
   m_dma( NULL ),
   m_hostch( NULL ),
   m_config( NULL ),
   m_handle( -1 ),
   m_device( INVALID_DEVICE ),
#if ACL_USE_DMA==1
   m_use_dma_for_big_transfers(true),
#else
   m_use_dma_for_big_transfers(false),
#endif
   m_mmd_irq_handler_enable( false ),
   m_initialized( false ),
   m_being_programmed( false )
{
   ACL_PCIE_ASSERT(name != NULL, "passed in an empty name pointer when creating device object.\n");

   int status = 0;

   // Set debug level from the environment variable ACL_PCIE_DEBUG
   // Determine if warning messages should be disabled depends on ACL_PCIE_WARNING
   if (num_open_devices == 0) {  
      set_mmd_debug();
      set_mmd_warn_msg();
   }

   strncpy( m_name, name, (MAX_NAME_LENGTH-1) );
   m_name[(MAX_NAME_LENGTH-1)] = '\0';

   m_handle         = handle;
   m_info.vendor_id = ACL_PCI_INTELFPGA_VENDOR_ID;
   m_info.device_id = 0;   // search for all device id   

#if defined(WINDOWS)
   m_device = open_device_windows(&m_info, dev_num);
#endif   // WINDOWS
#if defined(LINUX)
   m_device = open_device_linux  (&m_info, dev_num);
#endif   // LINUX

   // Return to caller if this is simply an invalid device.
   if (m_device == INVALID_DEVICE) {  return;  }
   
   // Initialize device IO and CONFIG objects
   //eyoh
   m_io     = new ACL_PCIE_MM_IO_MGR( m_device, m_num_partitions );
   m_config = new ACL_PCIE_CONFIG   ( m_device );

   // Set the segment ID to 0 first forcing cached "segment" to all 1s
   m_segment=(size_t)~0;
   if ( this->set_segment( 0x0 ) ) {  return;  }

   // performance basic I/O tests
   if ( this->version_id_test() ) {   return;   }
   if ( this->wait_for_uniphy() ) {   return;   }

   // Get PCIE information 
   unsigned int pcie_gen, pcie_num_lanes; 
   char pcie_slot_info_str[128] = {0}; 

   status = m_config->query_pcie_info(&pcie_gen, &pcie_num_lanes, pcie_slot_info_str); 
   ACL_PCIE_ERROR_IF(status, return,  
      "[%s] fail to query PCIe related information.\n", m_name);    
   sprintf(m_info.pcie_info_str, "dev_id = %04X, bus:slot.func = %s, Gen%u x%u",  
      m_info.device_id, pcie_slot_info_str, pcie_gen, pcie_num_lanes); 

   // Initialize the DMA object and enable interrupts on the DMA controller
   m_dma    = new ACL_PCIE_DMA( m_device, m_io, this );

   m_user_signal_number = user_signal_number;

   // Initialize the Host Channel object
   m_hostch = new ACL_PCIE_HOSTCH( m_device, m_io, this, m_dma );

   if ( this->enable_interrupts(m_user_signal_number) ) {  return;  }
   
   m_skip_quartus_version_check = 0;
   char *str_test_quartus_ver = getenv("ACL_SKIP_QUARTUS_VERSION_CHECK");
   if (str_test_quartus_ver) m_skip_quartus_version_check = 1;
   
   // Done!
   m_initialized = true;
   ACL_PCIE_DEBUG_MSG(":: [%s] successfully initialized (device id: %x).\n", m_name, m_info.device_id);
   ACL_PCIE_DEBUG_MSG("::           Using DMA for big transfers? %s\n", 
            ( m_use_dma_for_big_transfers ? "yes" : "no" ) );

}

ACL_PCIE_DEVICE::~ACL_PCIE_DEVICE()
{
   int status = this->disable_interrupts();
   ACL_PCIE_ERROR_IF(status, /* do nothing */ , 
      "[%s] fail disable interrupt in device destructor.\n", m_name);

   if(m_hostch)    { delete m_hostch; m_hostch = NULL; }
   if(m_dma)       { delete m_dma;    m_dma = NULL;    }
   if(m_config)    { delete m_config; m_config = NULL; }
   if(m_io)        { delete m_io;     m_io = NULL;     }

   if(is_valid()) { 
      --num_open_devices;
#if defined(WINDOWS)
      DWORD WD_status = WDC_PciDeviceClose(m_device);
      ACL_PCIE_ERROR_IF( WD_status != WD_STATUS_SUCCESS, return, 
         "[%s] failed to close the device handle.\n", m_name);

      if (num_open_devices == 0) {
         WD_status = WDC_DriverClose();
         ACL_PCIE_ERROR_IF( WD_status != WD_STATUS_SUCCESS, return, 
            "failed to close the WinDriver library.\n" );
      }
#endif   // WINDOWS
#if defined(LINUX)
      close (m_device);
#endif   // LINUX
   }
}



#if defined(WINDOWS)
WDC_DEVICE_HANDLE open_device_windows(ACL_PCIE_DEVICE_DESCRIPTION *info, int dev_num)
{
   DWORD WD_status;
   WDC_PCI_SCAN_RESULT pci_scan_result;
   WD_PCI_CARD_INFO    device_info;
   WDC_DEVICE_HANDLE   device = INVALID_DEVICE;
   DWORD pci_class_code_rev = 0;
   DWORD pci_subsystem_ids = 0;

   // Only open the WDC library handle once
   if (num_open_devices == 0) {
      const char *license = JUNGO_LICENSE;
      WD_status = WDC_DriverOpen( WDC_DRV_OPEN_DEFAULT, license );
      ACL_PCIE_ERROR_IF( WD_status != WD_STATUS_SUCCESS, return NULL, 
         "can't load the WinDriver library.\n" );
   }

   // Scan for our PCIe device
   WD_status = WDC_PciScanDevices( info->vendor_id, info->device_id, &pci_scan_result );
   ACL_PCIE_ERROR_IF( WD_status != WD_STATUS_SUCCESS, goto fail, 
      "failed to scan for the PCI device.\n");

   if( (unsigned int)dev_num >= pci_scan_result.dwNumDevices || dev_num < 0) {
      ACL_PCIE_DEBUG_MSG(":: [acl" ACL_BOARD_PKG_NAME "%d] Device not found\n", dev_num);
      goto fail;
   }

   // Query the device information
   device_info.pciSlot = pci_scan_result.deviceSlot[dev_num];
   WD_status = WDC_PciGetDeviceInfo( &device_info );
   ACL_PCIE_ERROR_IF( WD_status != WD_STATUS_SUCCESS, goto fail, 
      "[acl" ACL_BOARD_PKG_NAME "%d] failed to query device info.\n", dev_num);

   // Save the device id for the selected board
   info->device_id = pci_scan_result.deviceId[dev_num].dwDeviceId;

   // Open a device handle
   WD_status = WDC_PciDeviceOpen( &device, &device_info, NULL, NULL, NULL, NULL );
   ACL_PCIE_ERROR_IF( WD_status != WD_STATUS_SUCCESS, goto fail, 
      "[acl" ACL_BOARD_PKG_NAME "%d] failed to open the device.\n", dev_num);

   // Read SubSystem IDs out of PCI config space
   WD_status = WDC_PciReadCfg( device, 0x2C, &pci_subsystem_ids, sizeof(pci_subsystem_ids) );
   if ( (ACL_PCIE_READ_BIT_RANGE(pci_subsystem_ids,31,16) != ACL_PCI_SUBSYSTEM_DEVICE_ID) || 
        (ACL_PCIE_READ_BIT_RANGE(pci_subsystem_ids,15,0) != ACL_PCI_SUBSYSTEM_VENDOR_ID) ) {
     ACL_PCIE_DEBUG_MSG(":: [acl" ACL_BOARD_PKG_NAME "%d] PCI SubSystem IDs do not match, found %08x but expected %04x%04x\n", pci_subsystem_ids, ACL_PCI_SUBSYSTEM_DEVICE_ID, ACL_PCI_SUBSYSTEM_VENDOR_ID);
     WDC_PciDeviceClose(device);
     goto fail;
   }

   // Read Class code out of PCI config space
   WD_status = WDC_PciReadCfg( device, 8, &pci_class_code_rev, sizeof(pci_class_code_rev) );
   ACL_PCIE_DEBUG_MSG(":: [acl" ACL_BOARD_PKG_NAME "%d] PCI Class Code and Rev is: %x\n", dev_num, pci_class_code_rev);
   if ( ( (pci_class_code_rev & (0xff00ff00)) >> 8 ) != ACL_PCI_CLASSCODE ) {
     ACL_PCIE_DEBUG_MSG(":: [acl" ACL_BOARD_PKG_NAME "%d] PCI Class Code does not match, expected %x, read %d\n", dev_num, ACL_PCI_CLASSCODE, (pci_class_code_rev & 0xff00ff00) >> 8 );
     WDC_PciDeviceClose(device);
     goto fail;
   }

   // Check PCI Revision 
   if ( (pci_class_code_rev & 0x0ff) != ACL_PCI_REVISION ) {
     ACL_PCIE_DEBUG_MSG(":: [acl" ACL_BOARD_PKG_NAME "%d] PCI Revision does not match\n", dev_num);
     WDC_PciDeviceClose(device);
     goto fail;
   }

   ++num_open_devices;
   return device;

fail:
   // get here after opening the driver and then failing something else.
   if (num_open_devices == 0) WDC_DriverClose();
   return INVALID_DEVICE;
}
#endif // WINDOWS
#if defined(LINUX)
WDC_DEVICE_HANDLE open_device_linux(ACL_PCIE_DEVICE_DESCRIPTION *info, int dev_num)
{
   char buf[128] = {0};
   char expected_ver_string[128] = {0};

   sprintf(buf,"/dev/acl" ACL_BOARD_PKG_NAME "%d", dev_num);
   ssize_t device = open (buf, O_RDWR);

   // Return INVALID_DEVICE when the device is not available
   if (device == -1) {
      return INVALID_DEVICE; 
   }

   // Make sure the Linux kernel driver is recent
   struct acl_cmd driver_cmd = { ACLPCI_CMD_BAR, ACLPCI_CMD_GET_DRIVER_VERSION,
                              NULL, buf, 0 };
   read (device, &driver_cmd, 0);

   sprintf(expected_ver_string, "%s.%s", ACL_BOARD_PKG_NAME, KERNEL_DRIVER_VERSION_EXPECTED);
   ACL_PCIE_ERROR_IF( strstr(buf, expected_ver_string) != buf, return INVALID_DEVICE,
      "Kernel driver mismatch: The board kernel driver version is %s, but\nthis host program expects %s.\n  Please reinstall the driver using aocl install.\n", buf, expected_ver_string );

   // Save the device id for the selected board
   driver_cmd.bar_id         = ACLPCI_CMD_BAR;
   driver_cmd.command        = ACLPCI_CMD_GET_PCI_DEV_ID;
   driver_cmd.device_addr    = NULL;
   driver_cmd.user_addr      = &info->device_id;
   driver_cmd.size           = sizeof(info->device_id);
   read (device, &driver_cmd, sizeof(driver_cmd));

   // Set the FD_CLOEXEC flag for the file handle to disable the child to 
   // inherit this file handle. So the jtagd will not hold the file handle
   // of the device and keep sending bogus interrupts after we call quartus_pgm.
   int oldflags = fcntl( device, F_GETFD, 0);
   fcntl( device, F_SETFD, oldflags | FD_CLOEXEC );

   ++num_open_devices;
   return device;
}

#endif   // LINUX



// Perform operations required when an interrupt is received for this device
void ACL_PCIE_DEVICE::service_interrupt(unsigned int irq_type_flag)
{
   unsigned int kernel_update = 0;
   unsigned int dma_update    = 0;

   int status = this->get_interrupt_type(&kernel_update, &dma_update, irq_type_flag);
   ACL_PCIE_ERROR_IF(status, return, "[%s] fail to service the interrupt.\n", m_name);

   ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_IRQ,
      ":: [%s] Irq service routine called, kernel_update=%d, dma_update=%d \n", 
      m_name, kernel_update, dma_update);

   if (kernel_update && kernel_interrupt != NULL) {
      #if defined(WINDOWS)
      status = this->mask_irqs();
      ACL_PCIE_ERROR_IF(status, return, "[%s] failed to mask kernel interrupt.\n", m_name);   
      #endif
      // A kernel-status interrupt - update the status of running kernels
      ACL_PCIE_ASSERT(kernel_interrupt, 
         "[%s] received kernel interrupt before the handler is installed.\n", m_name);
      kernel_interrupt(m_handle, kernel_interrupt_user_data);
   } else if (dma_update) {
      // A DMA-status interrupt - let the DMA object handle this
      m_dma->service_interrupt();
   }

   // Unmask the kernel_irq to enable the interrupt again.
   if(m_mmd_irq_handler_enable){
      status = this->unmask_irqs();   
   } else if(kernel_update) {
      status = this->unmask_kernel_irq();  
   }
   ACL_PCIE_ERROR_IF(status, return, "[%s] fail to service the interrupt.\n", m_name);   

   return;
}



// Enable all interrupts (DMA and Kernel)
// Won't enable kernel irq unless kernel interrupt callback has been initialized
// Return 0 on success
int ACL_PCIE_DEVICE::unmask_irqs()
{
   int status = 0;
   if ( kernel_interrupt == NULL ) {
      // No masking for DMA interrupt.

   } else {
      status = m_io->pcie_cra->write32( PCIE_CRA_IRQ_ENABLE, 
          ACL_PCIE_GET_BIT(ACL_PCIE_KERNEL_IRQ_VEC));
   }
   ACL_PCIE_ERROR_IF(status, return -1, "[%s] fail to unmask all interrupts.\n", m_name);

   return 0; // success
}

// Disable all interrupts to service kernel that triggered interrupt
// If other kernels finish while the interrupt is masked, MSI will trigger again when
// interrupts are re-enabled.
int ACL_PCIE_DEVICE::mask_irqs()
{
   int status = 0;
   UINT32 val = 0;
   status = m_io->pcie_cra->write32 ( PCIE_CRA_IRQ_ENABLE, val);
   ACL_PCIE_ERROR_IF(status, return -1, "[%s] fail to mask the kernel interrupts.\n", m_name);
   
   return 0; // success
}

// Enable the kernel interrupt only
// Return 0 on success
int ACL_PCIE_DEVICE::unmask_kernel_irq()
{
   int status = 0;
   UINT32 val = 0;

   status |= m_io->pcie_cra->read32 ( PCIE_CRA_IRQ_ENABLE, &val);
   val    |= ACL_PCIE_GET_BIT(ACL_PCIE_KERNEL_IRQ_VEC);
   status |= m_io->pcie_cra->write32( PCIE_CRA_IRQ_ENABLE, val);

   ACL_PCIE_ERROR_IF(status, return -1, "[%s] fail to unmask the kernel interrupts.\n", m_name);
   
   return 0; // success
}

// Disable the interrupt
// Return 0 on success
int ACL_PCIE_DEVICE::disable_interrupts()
{
   int status;

   if(m_mmd_irq_handler_enable) {
      ACL_PCIE_DEBUG_MSG(":: [%s] Disabling interrupts.\n", m_name);

      status = m_io->pcie_cra->write32( PCIE_CRA_IRQ_ENABLE, 0 );
      ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to disable pcie interrupt.\n", m_name);

#if defined(WINDOWS)
      // Disable KMD interrupt handling for Windows
      DWORD WD_status = WDC_IntDisable(m_device);
      ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, "[%s] failed to disable interrupt in KMD.\n", m_name);
#endif   // WINDOWS
      m_mmd_irq_handler_enable = false;
   }
   
   return 0; // success
}

#if defined(WINDOWS)

// Enable PCI express interrupts.  Set up the KMD to mask the interrupt enable bit when
//    an interrupt is received to prevent the level-sensitive interrupt from immediately
//    firing again.
// Return 0 on success
int ACL_PCIE_DEVICE::enable_interrupts(int user_signal_number)
{
   int status;
   WDC_DEVICE *pDevice;

   ACL_PCIE_DEBUG_MSG(":: [%s] Enabling PCIe interrupts.\n", m_name);

   // Mask off hardware interrupts before enabling them
   status = m_io->pcie_cra->write32( PCIE_CRA_IRQ_ENABLE, 0 );
   ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to mask off all interrupts before enabling them.\n", m_name);

   // The device handle is actually just a pointer to the device object
   pDevice = (WDC_DEVICE*)(m_device);

   // Zero off the IRQ acknowledgement commands
   memset( irq_ack_cmds, 0, NUM_ACK_CMDS * sizeof(WD_TRANSFER) );

   // Set up the list of commands to mask out the interrupt until it is properly processed
      // CMD0 - Read the IRQ status register
   irq_ack_cmds[0].cmdTrans = RM_DWORD;
   irq_ack_cmds[0].dwPort = pDevice->pAddrDesc[m_io->pcie_cra->bar_id()].kptAddr +
                            m_io->pcie_cra->convert_to_bar_addr( PCIE_CRA_IRQ_STATUS );
   irq_ack_cmds[0].dwBytes = 0;
   irq_ack_cmds[0].fAutoinc = 0;
   irq_ack_cmds[0].dwOptions = 0;
   irq_ack_cmds[0].Data.Dword = 0;
      // CMD1 - Verify that the RxmIRQ bit is enabled (i.e. we own this IRQ)
   irq_ack_cmds[1].cmdTrans = CMD_MASK;
   irq_ack_cmds[1].dwPort = 0;
   irq_ack_cmds[1].dwBytes = 0;
   irq_ack_cmds[1].fAutoinc = 0;
   irq_ack_cmds[1].dwOptions = 0;
   irq_ack_cmds[1].Data.Dword = ACL_PCIE_GET_BIT(ACL_PCIE_KERNEL_IRQ_VEC);
      // CMD2 - Mask off RxmIRQ requests until we've processed this interrupt
   irq_ack_cmds[2].cmdTrans = WM_DWORD;
   irq_ack_cmds[2].dwPort = pDevice->pAddrDesc[m_io->pcie_cra->bar_id()].kptAddr +
                            m_io->pcie_cra->convert_to_bar_addr( PCIE_CRA_IRQ_ENABLE );
   irq_ack_cmds[2].dwBytes = 0;
   irq_ack_cmds[2].fAutoinc = 0;
   irq_ack_cmds[2].dwOptions = 0;
   irq_ack_cmds[2].Data.Dword = 0;

   ACL_PCIE_DEBUG_MSG(":: [%s] Interrupt handler:\n", m_name);
   ACL_PCIE_DEBUG_MSG("::             KMD Bar%d addr 0x%p\n", 
      m_io->pcie_cra->bar_id(), pDevice->pAddrDesc[m_io->pcie_cra->bar_id()].kptAddr);
   ACL_PCIE_DEBUG_MSG("::             Read  <- 0x%x\n", irq_ack_cmds[0].dwPort);
   ACL_PCIE_DEBUG_MSG("::             Mask     0x%x\n", irq_ack_cmds[1].Data.Dword);
   ACL_PCIE_DEBUG_MSG("::             Write -> 0x%x\n", irq_ack_cmds[2].dwPort);

   // Enable interrupts in the KMD
   DWORD WD_status = WDC_IntEnable( 
            m_device,                  // The device handle
            NULL,              // Array of commands to execute in the KMD
            0,              // Size of the above array
            0,                         // Options
            &pcie_interrupt_handler,   // Function pointer to the ISR
            static_cast<void*>(this),  // Custom ISR arguments
            FALSE                      // Custom kernal-mode ISR acceleration
         );
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, "[%s] failed to enable interrupts in the KMD.\n", m_name);

   status = this->unmask_irqs();
   ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to enable interrupts.\n", m_name);

   m_mmd_irq_handler_enable = true;
   return 0; // success
}

// Use irq status to determine type of interrupt
// Result is returned in kernel_update/dma_update arguments.
// Return 0 on success
int ACL_PCIE_DEVICE::get_interrupt_type (unsigned int *kernel_update, unsigned int *dma_update, unsigned int irq_type_flag)
{
   UINT32 irq_status;
   unsigned int dma_status;
   int    status;

   status = m_io->pcie_cra->read32( PCIE_CRA_IRQ_STATUS, &irq_status );
   ACL_PCIE_ERROR_IF(status, return -1, "[%s] fail to interrupt type.\n", m_name);

   *kernel_update = ACL_PCIE_READ_BIT( irq_status, ACL_PCIE_KERNEL_IRQ_VEC );
   
   status = m_dma->check_dma_interrupt( &dma_status );
   if (status != 1) {
      *dma_update = dma_status;
   }
   
   return 0; // success
}

#endif   // WINDOWS
#if defined(LINUX)

// For Linux, it will set-up a signal handler for signals for kernel driver
// Return 0 on success
int ACL_PCIE_DEVICE::enable_interrupts(int user_signal_number)
{
   int status;
   ACL_PCIE_DEBUG_MSG(":: [%s] Enabling PCIe interrupts on Linux (via signals).\n", m_name);

   // All interrupt controls are in the kernel driver.
   m_mmd_irq_handler_enable = false;

   // Send the globally allocated signal number to the driver
   struct acl_cmd signal_number_cmd;
   signal_number_cmd.bar_id         = ACLPCI_CMD_BAR;
   signal_number_cmd.command        = ACLPCI_CMD_SET_SIGNAL_NUMBER;
   signal_number_cmd.device_addr    = NULL;
   signal_number_cmd.user_addr      = &user_signal_number;
   signal_number_cmd.size           = sizeof(user_signal_number);
   status = write (m_device, &signal_number_cmd, sizeof(signal_number_cmd));
   ACL_PCIE_ERROR_IF( status, return -1, "[%s] failed to set signal number for interrupts.\n", m_name );
   
   // Sanity check, did the driver get it
   int readback_signal_number;
   signal_number_cmd.user_addr      = &readback_signal_number;
   signal_number_cmd.command        = ACLPCI_CMD_GET_SIGNAL_NUMBER;
   signal_number_cmd.size           = sizeof(readback_signal_number);
   status = read (m_device, &signal_number_cmd, sizeof(signal_number_cmd));
   ACL_PCIE_ERROR_IF( status, return -1, "[%s] failed to get signal number for interrupts.\n", m_name );
   ACL_PCIE_ERROR_IF( readback_signal_number != user_signal_number, return -1,
      "[%s] got wrong signal number %d, expected %d\n", m_name, readback_signal_number, user_signal_number );
   
   // Set "our" device id (the handle id received from acl_pcie.cpp) to correspond to 
   // the device managed by the driver. Will get back this id 
   // with signal from the driver. Will allow us to differentiate
   // the source of kernel-done signals with multiple boards.

   // the last bit is reserved as a flag for DMA completion
   int result = m_handle << 1;
   struct acl_cmd read_cmd = { ACLPCI_CMD_BAR, 
                                 ACLPCI_CMD_SET_SIGNAL_PAYLOAD, 
                                 NULL, 
                                 &result };
   status = write (m_device, &read_cmd, sizeof(result));
   ACL_PCIE_ERROR_IF( status, return -1, "[%s] failed to enable interrupts.\n", m_name );

   return 0; // success
}

// Determine the interrupt type using the irq_type_flag
// Return 0 on success
int ACL_PCIE_DEVICE::get_interrupt_type (unsigned int *kernel_update, unsigned int *dma_update, unsigned int irq_type_flag)
{
   // For Linux, the interrupt type is mutually exclusive
   *kernel_update = irq_type_flag ? 0: 1;
   *dma_update = 1 - *kernel_update;

   return 0; // success
}

#endif // LINUX



// Called by the host program when there are spare cycles
int ACL_PCIE_DEVICE::yield()
{
   // Give the DMA object a chance to crunch any pending data
   return m_dma->yield();
}



// Set kernel interrupt and event update callbacks
// return 0 on success
int ACL_PCIE_DEVICE::set_kernel_interrupt(aocl_mmd_interrupt_handler_fn fn, void * user_data) 
{
   int status;

   kernel_interrupt = fn;
   kernel_interrupt_user_data = user_data;
 
   if ( m_device != INVALID_DEVICE ) {
      status = this->unmask_kernel_irq();
      ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to set kernel interrupt callback funciton.\n", m_name);
   }
   
   return 0; // success
}

int ACL_PCIE_DEVICE::set_status_handler(aocl_mmd_status_handler_fn fn, void * user_data)
{
   event_update = fn;
   event_update_user_data = user_data;
   
   return 0; // success
}

// The callback function set by "set_status_handler"
// It's used to notify/update the host whenever an event is finished
void ACL_PCIE_DEVICE::event_update_fn(aocl_mmd_op_t op, int status)
{
   ACL_PCIE_ASSERT(event_update, "[%s] event_update is called with a empty update function pointer.\n", m_name);

   ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_OP,":: [%s] Update for event e=%p.\n", m_name, op);
   event_update(m_handle, event_update_user_data, op, status);
}

// Forward get buffer call to host channel
void *ACL_PCIE_DEVICE::hostchannel_get_buffer( size_t *buffer_size, int channel, int *status)
{
    return m_hostch->get_buffer (buffer_size, channel, status);
}
// Forward ack call to host channel
size_t ACL_PCIE_DEVICE::hostchannel_ack_buffer( size_t send_size , int channel, int *status)
{
    return m_hostch->ack_buffer (send_size, channel, status);
}

// Memory I/O
// return 0 on success
int ACL_PCIE_DEVICE::write_block( aocl_mmd_op_t e, aocl_mmd_interface_t mmd_interface, void *host_addr, size_t dev_addr, size_t size, int part_handle )
{
   ACL_PCIE_ASSERT(event_update, "[%s] event_update callback function is not provided.\n", m_name);
   int status = -1; // assume failure

   switch(mmd_interface)
   {
      case AOCL_MMD_KERNEL:
         status = (*(m_io->kernel_if_map))[part_handle]->write_block( dev_addr, size, host_addr );
         break;
      case AOCL_MMD_MEMORY:
         status = read_write_block (e, host_addr, dev_addr, size, false /*writing*/);
         break;
      case AOCL_MMD_PLL:
         status = m_io->pll->write_block( dev_addr, size, host_addr );
         break;
      default:
         ACL_PCIE_ASSERT(0, "[%s] unknown MMD interface.\n", m_name);
   }
   
   ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to write block.\n", m_name);
   
   return 0; // success
}

int ACL_PCIE_DEVICE::read_block( aocl_mmd_op_t e, aocl_mmd_interface_t mmd_interface, void *host_addr, size_t dev_addr, size_t size, int part_handle )
{
   ACL_PCIE_ASSERT(event_update, "[%s] event_update callback function is not provided.\n", m_name);
   int status = -1; // assume failure

   switch(mmd_interface)
   {
      case AOCL_MMD_KERNEL:
         status = (*(m_io->kernel_if_map))[part_handle]->read_block( dev_addr, size, host_addr );
         break;
      case AOCL_MMD_MEMORY:
         status = read_write_block (e, host_addr, dev_addr, size, true /*reading*/);
         break;
      case AOCL_MMD_PLL:
         status = m_io->pll->read_block( dev_addr, size, host_addr );
         break;
      default:
         ACL_PCIE_ASSERT(0, "[%s] unknown MMD interface.\n", m_name);
   }
   
   ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to read block.\n", m_name);
   
   return 0; // success
}

// Copy a block between two locations in device memory
// return 0 on success
int ACL_PCIE_DEVICE::copy_block( aocl_mmd_op_t e, aocl_mmd_interface_t mmd_interface, size_t src, size_t dst, size_t size, int part_handle )
{
   ACL_PCIE_ASSERT(event_update, "[%s] event_update callback function is not provided.\n", m_name);
   ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_OP,
      ":: [%s] Copying " SIZE_FMT_U " bytes data from 0x" SIZE_FMT_X " (device) to 0x" SIZE_FMT_X " (device), with e=%p\n", 
      m_name, size, src, dst, e);

#define BLOCK_SIZE (8*1024*1024)
#if defined(WINDOWS)
   __declspec(align(128)) static unsigned char data[BLOCK_SIZE];
#endif   // WINDOWS
#if defined(LINUX)
   static unsigned char data[BLOCK_SIZE] __attribute__((aligned(128)));
#endif   // LINUX

   do {
      size_t transfer_size = (size > BLOCK_SIZE) ? BLOCK_SIZE : size;
      read_block ( NULL /* blocking read  */, mmd_interface, data, src, transfer_size, part_handle );
      write_block( NULL /* blocking write */, mmd_interface, data, dst, transfer_size, part_handle );

      src  += transfer_size;
      dst  += transfer_size;
      size -= transfer_size;
   } while (size > 0);

   if (e)  { this->event_update_fn(e, 0); }

   return 0; // success
}

// Forward create hostchannel call to host channel
int ACL_PCIE_DEVICE::create_hostchannel( char * name, size_t queue_depth, int direction)
{
    return m_hostch->create_hostchannel(name, queue_depth, direction);
}

// Forward destroy hostchannel call to host channel
int ACL_PCIE_DEVICE::destroy_channel(int channel)
{
    return m_hostch->destroy_hostchannel(channel);
}


// Read or Write a block of data to device memory. 
// Use either DMA or directly read/write through BAR
// Return 0 on success
int ACL_PCIE_DEVICE::read_write_block( aocl_mmd_op_t e, void *host_addr, size_t dev_addr, size_t size, bool reading )
{
   const uintptr_t uintptr_host = reinterpret_cast<uintptr_t>(host_addr);

   int    status   = 0;
   size_t dma_size = 0;
   
   if(reading){
      ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_OP,
         ":: [%s] Reading " SIZE_FMT_U " bytes data from 0x" SIZE_FMT_X " (device) to %p (host), with e=%p\n", 
         m_name, size, dev_addr, host_addr, e);
   } else {
      ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_OP,
         ":: [%s] Writing " SIZE_FMT_U " bytes data from %p (host) to 0x" SIZE_FMT_X " (device), with e=%p\n", 
         m_name, size, host_addr, dev_addr, e);
   }

   // Return immediately if size is zero
   if( size == 0 ) {
      if (e)  { this->event_update_fn(e, 0); }
      return 0;
   }

   bool aligned = ((uintptr_host & DMA_ALIGNMENT_BYTE_MASK) | (dev_addr & DMA_ALIGNMENT_BYTE_MASK)) == 0;
   if ( m_use_dma_for_big_transfers && aligned && (size >= 1024) )
   {
      // DMA transfers must END at aligned boundary.
      // If that's not the case, use DMA up to such boundary, and regular
      // read/write for the remaining part.
      dma_size = size - (size & DMA_ALIGNMENT_BYTE_MASK);
   } else if( m_use_dma_for_big_transfers && (size >= 1024) ) {
      ACL_PCIE_WARN_MSG("[%s] NOT using DMA to transfer " SIZE_FMT_U " bytes from %s to %s because of lack of alignment\n" 
         "**                 host ptr (%p) and/or dev offset (0x" SIZE_FMT_X ") is not aligned to %u bytes\n", 
         m_name, size, (reading ? "device":"host"), (reading ? "host":"device"), host_addr, dev_addr, DMA_ALIGNMENT_BYTES);
   }

   // Perform read/write through BAR if the data is not fit for DMA or if there is remaining part from DMA
   if ( dma_size < size ) {
      void * host_addr_new = reinterpret_cast<void *>(uintptr_host + dma_size);
      size_t dev_addr_new  = dev_addr + dma_size;
      size_t remain_size   = size - dma_size;

      ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_OP,
         ":: [%s] Perform read/write through BAR for remaining " SIZE_FMT_U " bytes (out of " SIZE_FMT_U " bytes)\n", 
         m_name, remain_size, size); 

      status = read_write_block_bar( host_addr_new, dev_addr_new, remain_size, reading );
      ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to perform read/write through BAR.\n", m_name);
   }

   if ( dma_size != 0 ) {
      m_dma->read_write (host_addr, dev_addr, dma_size, e, reading);

      // Block if event is NULL
      if (e == NULL) {  m_dma->stall_until_idle();  }
   } else {
      if (e != NULL) {  this->event_update_fn(e, 0);  }
   }

   return 0; // success
}

// Read or Write a block of data to device memory through BAR
// Return 0 on success
int ACL_PCIE_DEVICE::read_write_block_bar( void *host_addr, size_t dev_addr, size_t size, bool reading )
{
   void * cur_host_addr = host_addr;
   size_t cur_dev_addr  = dev_addr;
   size_t bytes_transfered = 0;

   for (bytes_transfered=0; bytes_transfered<size; )
   {
      // decide the size to transfer for current iteration
      size_t cur_size = ACL_PCIE_MEMWINDOW_SIZE - ( cur_dev_addr%ACL_PCIE_MEMWINDOW_SIZE );
      if (bytes_transfered + cur_size >= size) {
         cur_size = size - bytes_transfered;
      }

      // set the proper window segment
      set_segment( cur_dev_addr );
      size_t window_rel_ptr_start = cur_dev_addr % ACL_PCIE_MEMWINDOW_SIZE;
      size_t window_rel_ptr       = window_rel_ptr_start;

      // A simple blocking read
      // The address should be in the global memory range, we assume
      // any offsets are already accounted for in the offset
      ACL_PCIE_ASSERT( window_rel_ptr + cur_size <= ACL_PCIE_MEMWINDOW_SIZE, 
         "[%s] trying to access out of the range of the memory window.\n", m_name);

      // Workaround a bug in Jungo driver.
      // First, transfer the non 8 bytes data at the front, one byte at a time
      // Then, transfer multiple of 8 bytes (size of size_t) using read/write_block
      // At the end, transfer the remaining bytes, one byte at a time
      size_t dev_odd_start = std::min (sizeof(size_t) - window_rel_ptr % sizeof(size_t), cur_size);
      if (dev_odd_start != sizeof(size_t)) {
         read_write_small_size( cur_host_addr, window_rel_ptr, dev_odd_start, reading );
         incr_ptrs (&cur_host_addr, &window_rel_ptr, &bytes_transfered, dev_odd_start );
         cur_size -= dev_odd_start;
      }

      size_t tail_size  = cur_size % sizeof(size_t);
      size_t size_mul_8 = cur_size - tail_size;

      if (size_mul_8 != 0) {
         if ( reading ) {
            m_io->mem->read_block ( window_rel_ptr, size_mul_8, cur_host_addr );
         } else {
            m_io->mem->write_block( window_rel_ptr, size_mul_8, cur_host_addr );
         }
         incr_ptrs (&cur_host_addr, &window_rel_ptr, &bytes_transfered, size_mul_8);
      }

      if (tail_size != 0) {
         read_write_small_size( cur_host_addr, window_rel_ptr, tail_size, reading );
         incr_ptrs (&cur_host_addr, &window_rel_ptr, &bytes_transfered, tail_size );
         cur_size -= tail_size;
      }

      // increase the current device address to be transferred
      cur_dev_addr += (window_rel_ptr - window_rel_ptr_start);
   }

   return 0; // success
}

// Read or Write a small size of data to device memory, one byte at a time
// Return 0 on success
int ACL_PCIE_DEVICE::read_write_small_size (void *host_addr, size_t dev_addr, size_t size, bool reading)
{
   UINT8 *ucharptr_host = static_cast<UINT8 *>(host_addr);
   int status;

   for(size_t i = 0; i < size; ++i) {
      if(reading) {
         status = m_io->mem->read8 ( dev_addr+i, ucharptr_host+i);
      } else {
         status = m_io->mem->write8( dev_addr+i, ucharptr_host[i]);
      }
      ACL_PCIE_ERROR_IF(status, return -1, "[%s] failed to read write with odd size.\n", m_name);
   }

   return 0; // success
}

// Set the segment that the memory windows is accessing to
// Return 0 on success
int ACL_PCIE_DEVICE::set_segment( size_t addr )
{
   UINT64 segment_readback;
   UINT64 cur_segment = addr & ~(ACL_PCIE_MEMWINDOW_SIZE-1);
   DWORD  status = 0;

   // Only execute the PCI write if we need to *change* segments
   if ( cur_segment != m_segment )
   {
      // PCIe reordering rules could cause the segment change to get reordered,
	   // so read before and after!
      status |= m_io->window->read64 ( 0 , &segment_readback );

      status |= m_io->window->write64( 0 , cur_segment );
      m_segment = cur_segment;
      ACL_PCIE_DEBUG_MSG_VERBOSE(VERBOSITY_BLOCKTX,":::::: [%s] Changed segment id to %llu.\n", m_name, m_segment);

      status |= m_io->window->read64 ( 0 , &segment_readback );
   }

   ACL_PCIE_ERROR_IF(status, return -1, 
      "[%s] failed to set segment for memory access windows.\n", m_name);

   return 0; // success
}

void ACL_PCIE_DEVICE::incr_ptrs (void **host, size_t *dev, size_t *counter, size_t incr)
{
   const uintptr_t uintptr_host = reinterpret_cast<uintptr_t>(*host);

   *host     = reinterpret_cast<void *>(uintptr_host+incr);
   *dev     += incr;
   *counter += incr;
}



// Query the on-chip temperature sensor
bool ACL_PCIE_DEVICE::get_ondie_temp_slow_call( cl_int *temp )
{
   cl_int read_data;
   
   // We assume this during read later
   ACL_PCIE_ASSERT( sizeof(cl_int) == sizeof(INT32), "sizeof(cl_int) != sizeof(INT32)" );

   if (! ACL_PCIE_HAS_TEMP_SENSOR) {
      ACL_PCIE_DEBUG_MSG(":: [%s] On-chip temperature sensor not supported by this board.\n", m_name);
      return false;
   }

   ACL_PCIE_DEBUG_MSG(":: [%s] Querying on-chip temperature sensor...\n", m_name);
   
   // read temperature sensor
   m_io->temp_sensor->read32(0, (UINT32 *)&read_data);

   ACL_PCIE_DEBUG_MSG(":: [%s] Read temp sensor data.  Value is: %i\n", m_name, read_data);
   *temp = read_data;
   return true;
}



void *ACL_PCIE_DEVICE::shared_mem_alloc ( size_t size, unsigned long long *device_ptr_out )
{
#if defined(WINDOWS)
   return NULL;
#endif   // WINDOWS
#if defined(LINUX)
   #ifdef ACL_HOST_MEMORY_SHARED
      void *host_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, m_device, 0); 
      
      if (device_ptr_out != NULL && host_ptr == (void*)-1) {
         // when mmap fails, it returns (void*)-1, not NULL
         host_ptr = NULL;
         *device_ptr_out = (unsigned long long)0;
         
      } else if (device_ptr_out != NULL) {
      
         /* map received host_ptr to FPGA-usable address. */
         void* dev_ptr = NULL;
         struct acl_cmd read_cmd = { ACLPCI_CMD_BAR, 
               ACLPCI_CMD_GET_PHYS_PTR_FROM_VIRT, 
               &dev_ptr,
               &host_ptr,   
               sizeof(dev_ptr) };
               
         bool failed_flag = (read (m_device, &read_cmd, sizeof(dev_ptr)) != 0);
         ACL_PCIE_DEBUG_MSG("  Mapped vaddr %p to phys addr %p. %s\n", 
                  host_ptr, dev_ptr, failed_flag==0 ? "OK" : "FAILED");
         if (failed_flag) {
            *device_ptr_out = (unsigned long long)NULL;
         } else {
            /* When change to 64-bit pointers on the device, update driver code
             * to deal with larger-than-void* ptrs. */
            *device_ptr_out = (unsigned long long)dev_ptr;
            
            /* Now need to add offset of the shared system. */
         } 
      }
      
      return host_ptr;
   #else
      return NULL;
   #endif
#endif   // LINUX
}

void ACL_PCIE_DEVICE::shared_mem_free ( void* vptr, size_t size )
{
#if defined(WINDOWS)
   return;
#endif   // WINDOWS
#if defined(LINUX)
   if (vptr != NULL) {
      munmap (vptr, size);
   }
#endif   // LINUX
}

// perform PR reprogram by attempting to program the board using an RBF. If this is not possible due to
// 1) Envoking the user of JTAG_PROGRAMMING via ACL_PCIE_USE_JTAG_PROGRAMMING
// 2) RBF or HASH are not present
// 3) PR Base ID does not match that with which the RBF was compiled
// 4) UniPhy fails to calibrate
// Then returns 1. Returns 0 on success. Always returns flag from arguments indicating source of failure
int ACL_PCIE_DEVICE::pr_reprogram(struct acl_pkg_file * pkg, const char * SOFNAME, int * rbf_or_hash_not_provided, int * hash_mismatch,
                  unsigned * use_jtag_programming, int * quartus_compile_version_mismatch, int part_handle) {
   // Environment variable to control when to use JTAG instead of PR (overriding the default programming method: PR)
   int reprogram_failed = 0;
   size_t core_rbf_len = 0, pr_import_version_len = 0, quartus_version_len = 0;
   *use_jtag_programming = 0;
   char *str_use_jtag_programming = getenv("ACL_PCIE_USE_JTAG_PROGRAMMING");
   if (str_use_jtag_programming) *use_jtag_programming = 1;

   // 1. Default programming method: PR
   if( !*use_jtag_programming) {
     // checking that rbf and hash sections exist in fpga.bin
     if( acl_pkg_section_exists( pkg, ACL_PKG_SECTION_CORE_RBF, &core_rbf_len ) &&
         acl_pkg_section_exists( pkg, ACL_PKG_SECTION_HASH, &pr_import_version_len ) &&
         (acl_pkg_section_exists( pkg, ACL_PKG_SECTION_QVERSION, &quartus_version_len ) || m_skip_quartus_version_check)) {
       *rbf_or_hash_not_provided = 0;
       ACL_PCIE_DEBUG_MSG(":: [%s] Programming kernel region using PR with rbf file size %i\n", m_name, (UINT32) core_rbf_len);

       // read rbf and hash from fpga.bin
       char *core_rbf = NULL;
       int read_core_rbf_ok = acl_pkg_read_section_transient( pkg, ACL_PKG_SECTION_CORE_RBF, &core_rbf );
       char *pr_import_version_str = NULL;
       int read_pr_import_version_ok = acl_pkg_read_section_transient( pkg, ACL_PKG_SECTION_HASH, &pr_import_version_str );
       char *quartus_compile_version_str = NULL;
       
       int quartus_compile_version_ok = 0;
       
       if (!m_skip_quartus_version_check) {
         quartus_compile_version_ok = acl_pkg_read_section_transient( pkg, ACL_PKG_SECTION_QVERSION, &quartus_compile_version_str );
         if (quartus_compile_version_str[quartus_version_len - 1] == '\n') {
           quartus_compile_version_str[quartus_version_len - 1] = '\0';
         } else {
           quartus_compile_version_str[quartus_version_len] = '\0';
         }
         
         quartus_version_len = ACL_QUARTUSVER_ROM_SIZE;
         *quartus_compile_version_mismatch = quartus_ver_test(quartus_compile_version_str, &quartus_version_len);
       } else {
         *quartus_compile_version_mismatch = 0;
       }

       if (*quartus_compile_version_mismatch == 0) {

         // checking that hash was successfully read from section .acl.hash within fpga.bin
         if ( read_pr_import_version_ok ) {
           pr_import_version_str[pr_import_version_len] = '\0';
           unsigned int pr_import_version = (unsigned int) strtol(pr_import_version_str, NULL, 10);
         
           // checking that base revision hash matches import revision hash and aocx and programmed sof is from same Quartus version
           if ( pr_base_id_test(pr_import_version) == 0 ) {
             *hash_mismatch = 0;
         
             // Kernel driver wants it aligned to 4 bytes.
             int aligned_to_4_bytes( 0 == ( 3 & (uintptr_t)(core_rbf) ) );
             reprogram_failed = 1;  // Default to fail before PRing
         
             // checking that rbf was successfully read from section .acl.core.rbf within fpga.bin
             if(read_core_rbf_ok && !(core_rbf_len % 4) && aligned_to_4_bytes && !version_id_test()) {
         
               ACL_PCIE_DEBUG_MSG(":: [%s] Starting PR programming of the device...\n", m_name);
               reprogram_failed = m_config->program_core_with_PR_file((char *)core_rbf, core_rbf_len, part_handle);
               ACL_PCIE_DEBUG_MSG(":: [%s] Finished PR programming of the device.\n", m_name);
         
               if ( reprogram_failed ) {
                 ACL_PCIE_DEBUG_MSG(":: [%s] PR programming failed.\n", m_name);
               }
               if ( version_id_test() ) {
                 ACL_PCIE_DEBUG_MSG(":: [%s] version_id_test() failed.\n", m_name);
                 reprogram_failed = 1;
               }
               if ( wait_for_uniphy() ) {
                 ACL_PCIE_DEBUG_MSG(":: [%s] Uniphy failed to calibrate.\n", m_name);
                 reprogram_failed = 1;
               }
               if ( !(reprogram_failed) ) {
                 ACL_PCIE_DEBUG_MSG(":: [%s] PR programming passed.\n", m_name);
               }
         
             }
           }
         }
       }
     }
   }
   return reprogram_failed;
}

// Reprogram the device with given binary file.
// There are two ways to program:
// 1. PR to replace the OpenCL kernel partition
// 2. JTAG full-chip programming (using quartus_pgm via USB-Blaster) to replace periphery + core
// Return 0 on success
int ACL_PCIE_DEVICE::reprogram(void *data, size_t data_size, int part_handle)
{
   int reprogram_failed = 1; // assume failure
   int rbf_or_hash_not_provided = 1; // assume no rbf or hash are provided in fpga.bin
   int hash_mismatch = 1; // assume base revision and import revision hashes do not match
   unsigned use_jtag_programming = 0; // assume no need for jtag programming
   int quartus_compile_version_mismatch = 1;
   size_t quartus_version_len;

   const char *SOFNAME = "reprogram_temp.sof";
   size_t sof_len = 0;

   ACL_PCIE_DEBUG_MSG(":: [%s] Starting to program device...\n", m_name);

   struct acl_pkg_file *pkg = acl_pkg_open_file_from_memory( (char*)data, data_size, ACL_PKG_SHOW_ERROR );
   ACL_PCIE_ERROR_IF(pkg == NULL, return reprogram_failed, "cannot open file from memory using pkg editor.\n");
   
   // Get Quartus Version string from aocx to write into RAM after quartus_pgm
   char *quartus_compile_version_str = NULL;
   int quartus_compile_version_ok = 0;
   if (acl_pkg_section_exists( pkg, ACL_PKG_SECTION_QVERSION, &quartus_version_len )) {
     quartus_compile_version_ok = acl_pkg_read_section_transient( pkg, ACL_PKG_SECTION_QVERSION, &quartus_compile_version_str );

     if (quartus_compile_version_ok) {
       if (quartus_compile_version_str[quartus_version_len - 1] == '\n') {
         quartus_compile_version_str[quartus_version_len - 1] = '\0';
       } else {
         quartus_compile_version_str[quartus_version_len] = '\0';
       }
     }
   }

   // set the being_programmed flag
   m_being_programmed = true;

   // 1. Default to PR reprogramming
   reprogram_failed = pr_reprogram(pkg, SOFNAME, &rbf_or_hash_not_provided, &hash_mismatch, &use_jtag_programming, &quartus_compile_version_mismatch, part_handle);

   // Autodetect JTAG cable & device index
   // Cable and Index value should't overflow
   char ad_cable[10];
   char ad_device_index[10];

   // 2. Fallback programming method: JTAG full-chip programming
   if( use_jtag_programming || rbf_or_hash_not_provided || hash_mismatch || (quartus_compile_version_mismatch && !m_skip_quartus_version_check)) {

     // checking that sof section exist in fpga.bin
     if( acl_pkg_section_exists(pkg, ACL_PKG_SECTION_SOF, &sof_len) ) {

       // check if aocx is fast-compiled or not - if so, then sof is a base revision,
       // and does not necessarily contain the desired kernel. Requires sof with
       // matching pr_base.id to be programmed (base.sof) followed by PR programming
       // with the given .rbf
       size_t fast_compile_len = 0;
       char * fast_compile_contents = NULL;
       int fast_compile = 0;
       if( acl_pkg_section_exists(pkg, ACL_PKG_SECTION_FAST_COMPILE, &fast_compile_len) &&
           acl_pkg_read_section_transient(pkg, ACL_PKG_SECTION_FAST_COMPILE, &fast_compile_contents) ) {
         fast_compile = 1;
         ACL_PCIE_DEBUG_MSG(":: [%s] Fast-compile fpga.bin detected.\n", m_name);
       }
       // Find jtag cable for the board
       // Returns 0 for both ad_cable,ad_device_index if not found
       // or if Autodetect is disabled
       this->find_jtag_cable(ad_cable,ad_device_index);

       // disable interrupt and save control registers
       this->disable_interrupts();
       m_config->save_pci_control_regs();

       // write out a SOF file
       const int wrote_sof = acl_pkg_read_section_into_file(pkg, ACL_PKG_SECTION_SOF, SOFNAME);
       ACL_PCIE_ERROR_IF( !wrote_sof, return reprogram_failed, "could not write %s.\n", SOFNAME);

       // JTAG programming the device
       ACL_PCIE_DEBUG_MSG(":: [%s] Starting JTAG programming of the device...\n", m_name);
       reprogram_failed = m_config->program_with_SOF_file(SOFNAME,ad_cable,ad_device_index);

       #if defined(LINUX)
         m_config->load_pci_control_regs();
       #endif

       if (quartus_compile_version_ok & !m_skip_quartus_version_check) {
         m_io->quartus_ver->write_block(0, quartus_version_len, quartus_compile_version_str);
       }
         
       if ( reprogram_failed ) {
         ACL_PCIE_DEBUG_MSG(":: [%s] JTAG programming failed.\n", m_name);
       }
       if ( version_id_test() ) {
         ACL_PCIE_DEBUG_MSG(":: [%s] version_id_test() failed.\n", m_name);
         reprogram_failed = 1;
       }
       if ( wait_for_uniphy() ) {
         ACL_PCIE_DEBUG_MSG(":: [%s] Uniphy failed to calibrate.\n", m_name);
         reprogram_failed = 1;
       }
       if( fast_compile ) {
         // need to rerun pr_reprogram because design should be loaded now
         hash_mismatch = 0;
         rbf_or_hash_not_provided = 0;
         reprogram_failed = pr_reprogram(pkg, SOFNAME, &rbf_or_hash_not_provided, &hash_mismatch, &use_jtag_programming, &quartus_compile_version_mismatch, part_handle);
       }
       if ( !(reprogram_failed) ) {
         ACL_PCIE_DEBUG_MSG(":: [%s] JTAG programming passed.\n", m_name);
       }

     } else {
       ACL_PCIE_DEBUG_MSG(":: [%s] Could not read SOF file from fpga.bin.\n", m_name);
       reprogram_failed = 1;
     }
   } 
   
   // Clean up
   if ( pkg ) acl_pkg_close_file(pkg);
   m_being_programmed = false;

   return reprogram_failed;
}



// Perform a simple version id read to test the basic PCIe read functionality
// Return 0 on success
int ACL_PCIE_DEVICE::version_id_test()
{    
   unsigned int version = ACL_VERSIONID ^ 1; // make sure it's not what we hope to find. 
   unsigned int iattempt;
   unsigned int max_attempts = 1;
   unsigned int usleep_per_attempt = 20;     // 20 ms per.

   ACL_PCIE_DEBUG_MSG(":: [%s] Doing PCIe-to-fabric read test ...\n", m_name);
   for( iattempt = 0; iattempt < max_attempts; iattempt ++){
      m_io->version->read32(0, &version);
      // COMPATIBLE version ID needs to be removed when everyone migrates to
      // the latest verison ID FB #410932
      if( (version == (unsigned int)ACL_VERSIONID)
              || (version == (unsigned int)ACL_VERSIONID_COMPATIBLE_161)
              || (version == (unsigned int)ACL_VERSIONID_COMPATIBLE_170)
              || (version == (unsigned int)ACL_VERSIONID_COMPATIBLE_171a)
              || (version == (unsigned int)ACL_VERSIONID_COMPATIBLE_171b)) {
         ACL_PCIE_DEBUG_MSG(":: [%s] PCIe-to-fabric read test passed\n", m_name);
         return 0;
      }
#if defined(WINDOWS)
      Sleep(  usleep_per_attempt  );
#endif   // WINDOWS
#if defined(LINUX)
      usleep( usleep_per_attempt*1000 );
#endif   // LINUX
   }

   // Kernel read command succeed, but got bad data. (version id doesn't match)
   ACL_PCIE_INFO("[%s] PCIe-to-fabric read test failed, read 0x%0x after %u attempts\n",
      m_name, version, iattempt);
   return -1;
}

// Quartus Compile Version check
// Return 0 on success
int ACL_PCIE_DEVICE::quartus_ver_test(char *version_str, size_t *version_length)
{
   unsigned int *quartus_version;
   unsigned int version;
   
   // Check version ID to ensure feature supported in HW
   m_io->version->read32(0, &version);
   if (version <= (unsigned int)ACL_VERSIONID_COMPATIBLE_170) {
      ACL_PCIE_DEBUG_MSG(":: [%s] Programming on board without Quartus Version RAM\n", m_name);
      return 1;
   }  
   
   quartus_version = (unsigned int *) malloc(*version_length + 1);
   memset(quartus_version, 0, *version_length + 1); // Make sure it's not what we hope to find

   m_io->quartus_ver->read_block(0, *version_length, quartus_version);
   
   
   char *quartus_version_str;
   quartus_version_str = (char *) quartus_version;
   
   size_t version_length_programmed = strlen(quartus_version_str);
   size_t version_length_aocx = strlen(version_str);
   
   if (version_length_programmed != version_length_aocx) {
      // Kernel read command succeed, but got bad data. (Quartus Version doesn't match)
      ACL_PCIE_INFO("[%s] Quartus versions for base and import compile do not match\n", m_name);
      ACL_PCIE_INFO("[%s] Board is currently programmed with sof from Quartus %s\n", m_name, quartus_version_str);
      ACL_PCIE_INFO("[%s] PR import was compiled with Quartus %s\n", m_name, version_str);
      ACL_PCIE_INFO("[%s] Falling back to JTAG programming instead of PR\n", m_name);
      return 1;
   }
    
   *version_length = version_length_programmed;

   if (strncmp(version_str, quartus_version_str, *version_length - 1) == 0) {
     ACL_PCIE_DEBUG_MSG(":: [%s] Quartus versions for base and import compile match\n", m_name);
     ACL_PCIE_DEBUG_MSG(":: [%s] Board is currently programmed with sof from Quartus %s\n", m_name, quartus_version_str);
     ACL_PCIE_DEBUG_MSG(":: [%s] PR import was compiled with Quartus %s\n", m_name, version_str);
     
     return 0;
   }
   
   // Kernel read command succeed, but got bad data. (Quartus Version doesn't match)
   ACL_PCIE_INFO("[%s] Quartus versions for base and import compile do not match\n", m_name);
   ACL_PCIE_INFO("[%s] Board is currently programmed with sof from Quartus %s\n", m_name, quartus_version_str);
   ACL_PCIE_INFO("[%s] PR import was compiled with Quartus %s\n", m_name, version_str);
   ACL_PCIE_INFO("[%s] Falling back to JTAG programming instead of PR\n", m_name);
   
   return 1;
} 

// Perform a simple read to the PR base ID in the static region and compare it with the given ID
// Return 0 on success
int ACL_PCIE_DEVICE::pr_base_id_test(unsigned int pr_import_version)
{    
   unsigned int pr_base_version = 0; // make sure it's not what we hope to find. 

   ACL_PCIE_DEBUG_MSG(":: [%s] Reading PR base ID from fabric ...\n", m_name);
   m_io->pr_base_id->read32(0, &pr_base_version);
   if( pr_base_version == pr_import_version ){
     ACL_PCIE_DEBUG_MSG(":: [%s] PR base and import compile IDs match\n", m_name);
     ACL_PCIE_DEBUG_MSG(":: [%s] PR base ID currently configured is 0x%0x\n", m_name, pr_base_version);
     ACL_PCIE_DEBUG_MSG(":: [%s] PR import compile ID is 0x%0x\n", m_name, pr_import_version);
     return 0;
   };
 
   // Kernel read command succeed, but got bad data. (version id doesn't match)
   ACL_PCIE_INFO("[%s] PR base and import compile IDs do not match\n", m_name);
   ACL_PCIE_INFO("[%s] PR base ID currently configured is 0x%0x\n", m_name, pr_base_version);
   ACL_PCIE_INFO("[%s] PR import compile expects ID to be 0x%0x\n", m_name, pr_import_version); 
   ACL_PCIE_INFO("[%s] Falling back to JTAG programming instead of PR\n", m_name);
   return -1;
}

// 1. Write a random value to cade_id register, do a read to confirm the write
// 2. Use the random value to find the JTAG cable for that board
// 3. Return "0" on ad_cable,ad_device_index if cable not found
void ACL_PCIE_DEVICE::find_jtag_cable(char *ad_cable, char *ad_device_index)
{ 
   bool jtag_ad_disabled = false;
   bool jtag_ad_cable_found = false;
   unsigned int version = 0;

   // Check if Autodetect is disabled
   const char *cable = getenv("ACL_PCIE_JTAG_CABLE");
   const char *device_index = getenv("ACL_PCIE_JTAG_DEVICE_INDEX");
   if (cable ||  device_index) {
      jtag_ad_disabled = true;
      ACL_PCIE_DEBUG_MSG(":: [%s] JTAG cable autodetect disabled!!!\n", m_name);
   }

   // Check version ID to ensure feature supported in HW
   m_io->version->read32(0, &version);
   if (version <= (unsigned int)ACL_VERSIONID_COMPATIBLE_161) {
      jtag_ad_disabled = true;     
      ACL_PCIE_DEBUG_MSG(":: [%s] JTAG cable autodetect disabled due to old HW version!!!\n", m_name);
   }  

   // If JTAG autodetect is enabled, program the CADEID register
   // and look for the value using in system sources and probes
   if (!jtag_ad_disabled) {
      srand(time(NULL)); 
      unsigned int cade_id_write = rand() & 0xFFFFFFFF; 
      cade_id_write = cade_id_write | 0x80000000; //Write a full 32 bit value  
      unsigned int cade_id_read = 0x0;  
      
      ACL_PCIE_DEBUG_MSG(":: [%s] Writing Cade ID to fabric ...\n", m_name);
      m_io->cade_id->write32(0, cade_id_write);
      
      ACL_PCIE_DEBUG_MSG(":: [%s] Reading Cade ID from fabric ...\n", m_name);
      m_io->cade_id->read32(0, &cade_id_read);
      
      if( cade_id_write == cade_id_read ){
         ACL_PCIE_DEBUG_MSG(":: [%s] Cade ID write/read success ...\n", m_name);
         ACL_PCIE_DEBUG_MSG(":: [%s] Cade ID  cade_id_write 0x%0x, cade_id_read 0x%0x\n", m_name, cade_id_write, cade_id_read);
      
         // Returns NULL on ad_cable,ad_device_index if no cable found
         jtag_ad_cable_found = m_config->find_cable_with_ISSP(cade_id_write,ad_cable,ad_device_index);
      
         if (!jtag_ad_cable_found) {
            ACL_PCIE_DEBUG_MSG(":: [%s] Using default cable 1 ...\n", m_name);
         } else {
            ACL_PCIE_DEBUG_MSG(":: [%s] Found Cable ...\n", m_name);
         }
      } else {
         ACL_PCIE_DEBUG_MSG(":: [%s] Cade ID write/read failed. Check BSP version or PCIE link...\n", m_name);
         ACL_PCIE_DEBUG_MSG(":: [%s] Cade ID  cade_id_write 0x%0x, cade_id_read 0x%0x\n", m_name, cade_id_write, cade_id_read);
      }
   }//if (!jtag_ad_disabled

   if (jtag_ad_disabled || !jtag_ad_cable_found) {
      sprintf(ad_cable,"%s", "0");
      sprintf(ad_device_index,"%s", "0");
   }
}


// Wait until the uniphy calibrated
// Return 0 on success
int ACL_PCIE_DEVICE::wait_for_uniphy()
{
   const unsigned int ACL_UNIPHYSTATUS = 0;
   unsigned int status = 1, retries = 0; 
   
   while( retries++ < 8){
      m_io->uniphy_status->read32(0, &status);

      if( status == ACL_UNIPHYSTATUS){
         ACL_PCIE_DEBUG_MSG(":: [%s] Uniphys are calibrated\n", m_name);
         return 0;   // success
      }

      ACL_PCIE_DEBUG_MSG(":: [%s] Uniphy status read was %x\n", m_name, status); 
      ACL_PCIE_DEBUG_MSG(":: [%s] Resetting Uniphy try %d\n", m_name, retries);
      m_io->uniphy_reset->write32( 0, 1 );

#if defined(WINDOWS)
      Sleep( 400 );
#endif   // WINDOWS
#if defined(LINUX)
      usleep(400*1000);
#endif   // LINUX
   }

   ACL_PCIE_INFO("[%s] uniphy(s) did not calibrate.  Expected 0 but read %x\n",
      m_name, status);

   // Failure! Was it communication error or actual calibration failure?
   if ( ACL_PCIE_READ_BIT( status , 3) )  // This bit is hardcoded to 0
      ACL_PCIE_INFO("                Uniphy calibration status is corrupt.  This is likely a communication error with the board and/or uniphy_status module.\n");
   else {
      // This is a 32-bit interface with the first 4 bits aggregating the
      // various calibration signals.  The remaining 28-bits would indicate
      // failure for their respective memory core.  Tell users which ones
      // failed
      for (int i = 0; i < 32-4; i++) {
         if ( ACL_PCIE_READ_BIT( status , 4+i) )
            ACL_PCIE_INFO("  Uniphy core %d failed to calibrate\n",i );
      }
      ACL_PCIE_INFO("     If there are more failures than Uniphy controllers connected, \n");
      ACL_PCIE_INFO("     ensure the uniphy_status core is correctly parameterized.\n" );
   }

   return -1;   // failure
}

