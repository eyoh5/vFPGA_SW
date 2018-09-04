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


/* ===- acl_pcie.cpp  ------------------------------------------------- C++ -*-=== */
/*                                                                                 */
/*                         Intel(R) OpenCL MMD Driver                                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */
/*                                                                                 */
/* This file implements the functions that are defined in aocl_mmd.h               */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */


// common and its own header files
#include "acl_pcie.h" 

// other header files inside MMD driver
#include "acl_pcie_device.h"
#include "acl_pcie_debug.h"

// other standard header files
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
           
#include <map>
#include <sstream>
#include <string>

#if defined(LINUX)
#  include <unistd.h>
#  include <signal.h>
#  include <semaphore.h>
#  include <fcntl.h>
#endif   // LINUX

// MAX size of line read from pipe-ing the output of system call to MMD
#define BUF_SIZE 1024 
// MAX size of command passed to system for invoking system call from MMD
#define SYSTEM_CMD_SIZE 4*1024


// static helper functions
static bool   blob_has_elf_signature( void* data, size_t data_size );



// global variables used for handling multi-devices and its helper functions
static std::map<int, ACL_PCIE_DEVICE*> s_handle_map;
static std::map<int, const std::string>     s_device_name_map;

static inline ACL_PCIE_DEVICE *get_pcie_device(int handle)
{
   std::map<int, ACL_PCIE_DEVICE*>::iterator it = s_handle_map.find(handle);
   ACL_PCIE_ASSERT(it != s_handle_map.end(), "can't find handle %d -- aborting\n", handle);

   return it->second;
}

static void discard_pcie_device_handle(int handle)
{
   ACL_PCIE_ASSERT(s_handle_map.find(handle) != s_handle_map.end(), "can't find handle %d\n", handle);
   s_handle_map.erase(handle);
   s_device_name_map.erase(handle);
}

static inline bool is_any_device_being_programmed()  
{ 
   bool ret = false; 
   for( std::map<int, ACL_PCIE_DEVICE*>::iterator it = s_handle_map.begin(); it != s_handle_map.end(); it++) { 
      if( it->second->is_being_programmed() ) { 
         ret = true; 
         break; 
      }    
   } 
   return ret; 
}

// Functions for handling interrupts or signals for multiple devices 
// This functions are used inside the ACL_PCIE_DEVICE class
#if defined(WINDOWS)
void pcie_interrupt_handler( void* data )
{
   ACL_PCIE_DEVICE* device = static_cast<ACL_PCIE_DEVICE*>(data);
   device->service_interrupt();
}

BOOL ctrl_c_handler( DWORD fdwCtrlType ) 
{ 
   if( fdwCtrlType != CTRL_C_EVENT )      return FALSE; 

   if( is_any_device_being_programmed() ) { 
      ACL_PCIE_INFO("The device is still being programmed, cannot terminate at this point.\n"); 
      return TRUE; 
   }
  
   // On Windows, the signal handle function is executed by another thread,  
   // so we cannot simply free all the open devices.  
   // Just exit when received a ctrl-c event, the OS will take care of the clean-up. 
   exit(1); 
} 
#endif // WINDOWS
#if defined(LINUX)
// On Linux, driver will send a SIG_INT_NOTIFY *signal* to notify about an interrupt.
void pcie_linux_signal_handler (int sig, siginfo_t *info, void *unused)
{
   // the last bit indicates the DMA completion
   unsigned int irq_type_flag = info->si_int & 0x1;
   // other bits shows the handle value of the device that sent the interrupt
   unsigned int handle        = info->si_int >> 1;

   if( s_handle_map.find(handle) == s_handle_map.end() ) {
      ACL_PCIE_DEBUG_MSG(":: received an unknown handle %d in signal handler, ignore this.\n", handle);
      return;
   }

   s_handle_map[handle]->service_interrupt(irq_type_flag);
}

// Function to free all ACL_PCIE_DEVICE struct allocated for open devices
static inline void free_all_open_devices()
{
   for( std::map<int, ACL_PCIE_DEVICE*>::iterator it = s_handle_map.begin(); it != s_handle_map.end(); it++) {
      delete it->second;
   }
}

void ctrl_c_handler(int sig_num)
{
   if( is_any_device_being_programmed() ) {
      ACL_PCIE_INFO("The device is still being programmed, cannot terminate at this point.\n");
      return;
   }

   // Free all the resource allocated for open devices before exit the program.
   // It also notifies the kernel driver about the termination of the program, 
   // so that the kernel driver won't try to talk to any user-allocated memory
   // space (mainly for the DMA) after the program exit.
   free_all_open_devices();
   exit(1);
}

void abort_signal_handler(int sig_num)
{
   free_all_open_devices();
   exit(1);
}

int allocate_and_register_linux_signal_number_helper(int pid) {
   char buffer[4096], *locOfSigCgt;
   FILE *fp;
   int bytes_read, status, ret = -1;
   unsigned long long sigmask=0;
   struct sigaction sigusr, oldsig, sigabrt;
   
   sprintf(buffer, "/proc/%d/status", pid);
   fp = fopen(buffer, "rb");
   ACL_PCIE_ERROR_IF ( fp == NULL, return -1, "Unable to open file %s\n", buffer);
   bytes_read = fread(buffer, sizeof(buffer[0]), sizeof(buffer)-1, fp);
   fclose(fp);
   buffer[bytes_read] = 0;  //null terminate the string
   locOfSigCgt = strstr(buffer, "SigCgt:"); //returns null if can't find, shouldn't happen
   ACL_PCIE_ERROR_IF ( locOfSigCgt == NULL, return -1, "Did not find SigCgt: for PID %d\n", pid);
   sscanf(locOfSigCgt+7, "%llx", &sigmask);
   
   // Find an unused signal number
   for (int i = SIGRTMAX; i >= SIGRTMIN; i--) {
      if (!((sigmask>>(i-1))&1)) {
         ret = i;
         break;
      }
   }
   ACL_PCIE_ERROR_IF ( ret == -1, return -1, "Unable to find an unused signal number\n");
   
   // Enable if driver is using signals to communicate with the host.
   sigusr.sa_sigaction = pcie_linux_signal_handler;
   sigusr.sa_flags = SA_SIGINFO;
   oldsig.sa_handler = NULL;
   oldsig.sa_sigaction = NULL;
   status = sigaction(ret, &sigusr, &oldsig);
   ACL_PCIE_ERROR_IF ( status != 0, return -1, "sigaction failed with status %d, signal number %d\n", status, ret);
   ACL_PCIE_ERROR_IF ( oldsig.sa_handler != NULL, return -1, "sigaction previous sa_handler not null\n");
   ACL_PCIE_ERROR_IF ( oldsig.sa_sigaction != NULL, return -1, "sigaction previous sa_sigaction not null\n");
   
   // Install signal handler for SIGABRT from assertions in the upper layers
   sigabrt.sa_handler = abort_signal_handler;
   sigemptyset(&sigabrt.sa_mask);
   sigabrt.sa_flags = 0;
   status = sigaction(SIGABRT, &sigabrt, NULL);
   ACL_PCIE_ERROR_IF ( status != 0, return -1, "sigaction failed with status %d, signal number %d\n", status, SIGABRT);
   
   //if it makes it here, the user got an unused signal number and we installed all signal handlers
   return ret;
}

//returns an unused signal number, -1 means ran into some error
int allocate_and_register_linux_signal_number(pthread_mutex_t *mutex) {
   int pid, ret, err;
   
   pid = getpid();
   err = pthread_mutex_lock(mutex);
   ACL_PCIE_ERROR_IF ( err != 0, return -1, "pthread_mutex_lock error %d\n", err);
   
   //this has multiple return points, put in separate function so that we don't bypass releasing the mutex
   ret = allocate_and_register_linux_signal_number_helper(pid);
   
   err = pthread_mutex_unlock(mutex);
   ACL_PCIE_ERROR_IF ( err != 0, return -1, "pthread_mutex_unlock error %d\n", err);
   
   return ret;
}
#endif   // LINUX



// Function to install the signal handler for Ctrl-C
// If ignore_sig != 0, the ctrl-c signal will be ignored by the program
// If ignore_sig  = 0, the custom signal handler (ctrl_c_handler) will be used
int install_ctrl_c_handler(int ingore_sig)
{
#if defined(WINDOWS)
   SetConsoleCtrlHandler( (ingore_sig ? NULL : (PHANDLER_ROUTINE) ctrl_c_handler), TRUE );
#endif   // WINDOWS
#if defined(LINUX)
   struct sigaction sig;
   sig.sa_handler = (ingore_sig ? SIG_IGN : ctrl_c_handler);
   sigemptyset(&sig.sa_mask);
   sig.sa_flags = 0;
   sigaction(SIGINT, &sig, NULL);
#endif   // LINUX

   return 0;
}

// Function to return the number of boards installed in the system
unsigned int get_offline_num_boards() 
{
// Windows MMD will try to open all the devices
#if defined(WINDOWS)
   return ACL_MAX_DEVICE; 
#endif   // WINDOWS

// Linux MMD will look into the number of devices
#if defined(LINUX)
   FILE *fp;
   char str_line_in[BUF_SIZE];
   char str_board_pkg_name[BUF_SIZE];
   char str_cmd[SYSTEM_CMD_SIZE];
   unsigned int num_boards = 0;

   sprintf(str_board_pkg_name,"acl%s",ACL_BOARD_PKG_NAME);
   sprintf(str_cmd, "ls /sys/class/aclpci_%s 2>/dev/null",ACL_BOARD_PKG_NAME);
   
   fp = popen(str_cmd, "r");

   // Read every line from output
   while (fgets(str_line_in, BUF_SIZE, fp) != NULL) {
      
      if (strncmp(str_board_pkg_name, str_line_in, strlen(str_board_pkg_name)) == 0) {
         num_boards++;
      }
   }
   
   pclose(fp);
   
   // Fall back to the legacy behavior of opening all devices when
   // no boards are found (which implies something else is broken)
   if (num_boards == 0) {
      num_boards = ACL_MAX_DEVICE;
   }
   return num_boards;
#endif   // LINUX
}




// Get information about the board using the enum aocl_mmd_offline_info_t for
// offline info (called without a handle), and the enum aocl_mmd_info_t for
// info specific to a certain board. 
#define RESULT_INT(X) {*((int*)param_value) = X; if (param_size_ret) *param_size_ret=sizeof(int);}
#define RESULT_STR(X) do { \
    size_t Xlen = strlen(X) + 1; \
    memcpy((void*)param_value,X,(param_value_size <= Xlen) ? param_value_size : Xlen); \
    if (param_size_ret) *param_size_ret=Xlen; \
  } while(0)

int aocl_mmd_get_offline_info(
   aocl_mmd_offline_info_t requested_info_id,
   size_t param_value_size,
   void* param_value,
   size_t* param_size_ret 
)
{
   unsigned int num_boards;
   switch(requested_info_id)
   {
      case AOCL_MMD_VERSION:     RESULT_STR(MMD_VERSION); break;
      case AOCL_MMD_NUM_BOARDS:  
      {
         num_boards = get_offline_num_boards();
         RESULT_INT(num_boards);
         break;
      }
      case AOCL_MMD_BOARD_NAMES: 
      {
         // Construct a list of all possible devices supported by this MMD layer
         std::ostringstream boards;
         num_boards = get_offline_num_boards();
         for (unsigned i=0; i<num_boards; i++) {
            boards << "acl" << ACL_BOARD_PKG_NAME << i;
            if (i<num_boards-1) boards << ";";
         }
         RESULT_STR(boards.str().c_str()); 
         break;
      }
      case AOCL_MMD_VENDOR_NAME: 
      {
         RESULT_STR(ACL_VENDOR_NAME); 
         break;
      }
      case AOCL_MMD_VENDOR_ID:  RESULT_INT(ACL_PCI_INTELFPGA_VENDOR_ID); break;
      case AOCL_MMD_USES_YIELD:  RESULT_INT(0); break;
      case AOCL_MMD_MEM_TYPES_SUPPORTED: RESULT_INT(AOCL_MMD_PHYSICAL_MEMORY); break;
   }
   return 0;
}

//eyoh
int aocl_mmd_get_info(
   int handle,
   aocl_mmd_info_t requested_info_id,
   size_t param_value_size,
   void* param_value,
   size_t* param_size_ret 
)
{
   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1, 
      "aocl_mmd_get_info failed due to the target device (handle %d) is not properly initialized.\n", handle);

   switch(requested_info_id)
   {
     case AOCL_MMD_BOARD_NAME:
       {
         std::ostringstream board_name;
         board_name << ACL_BOARD_NAME << " (" << s_device_name_map[ handle ] << ")";
         RESULT_STR(board_name.str().c_str()); 
         break;
       }

     // eyoh
     case AOCL_MMD_NUM_KERNEL_INTERFACES:
     {
	RESULT_INT(AOCL_MMD_KERNEL); break;
	//RESULT_INT(pcie_dev->get_num_partitions());
	break;
     }

     // eyoh
     case AOCL_MMD_KERNEL_INTERFACES:
     {
	RESULT_INT(AOCL_MMD_KERNEL); break;

	// create list of kernel handles 
	// and return it back
	//int num_partitions = pcie_dev->get_num_partitions();
	//int* r;
	//r = (int*) param_value;
	//for (int i=0; i<num_partitions; i++){
	//	r[i] =i;
	//}

	//if(param_size_ret){
	//	*param_size_ret = num_partitions;
	//}
	//break;
     }
     case AOCL_MMD_NUM_PARTITIONS: 	  RESULT_INT(pcie_dev->get_num_partitions()); break;
     case AOCL_MMD_PLL_INTERFACES:        RESULT_INT(AOCL_MMD_PLL); break;
     case AOCL_MMD_MEMORY_INTERFACE:      RESULT_INT(AOCL_MMD_MEMORY); break;
     case AOCL_MMD_PCIE_INFO:             RESULT_STR(pcie_dev->get_dev_pcie_info()); break;

     case AOCL_MMD_TEMPERATURE:
     {
         float *r;
         int temp;
         pcie_dev->get_ondie_temp_slow_call( &temp );
         r = (float*)param_value;
         *r = ((708.0f * (float)temp) / 1024.0f) - 273.0f;
         if (param_size_ret)
           *param_size_ret = sizeof(float);
         break;
     }
     
     // currently not supported
     case AOCL_MMD_BOARD_UNIQUE_ID:       return -1;
   }
   return 0;
}

#undef RESULT_INT
#undef RESULT_STR 



// Open and initialize the named device.
int AOCL_MMD_CALL aocl_mmd_open(const char *name)
{
   static int signal_handler_installed = 0;
   static int unique_id = 0;
   int dev_num = -1;
   static int user_signal_number = -1;
#if defined(LINUX)
   static pthread_mutex_t linux_signal_arb_mutex = PTHREAD_MUTEX_INITIALIZER;   //initializes as unlocked, static = no cleanup needed
#endif   // LINUX

   if (sscanf(name, "acl" ACL_BOARD_PKG_NAME "%d", &dev_num) != 1)     { return -1; }
   if (dev_num < 0 || dev_num >= ACL_MAX_DEVICE) { return -1; }
   if (++unique_id <= 0)                         { unique_id = 1; }

   ACL_PCIE_ASSERT(s_handle_map.find(unique_id) == s_handle_map.end(), 
      "unique_id %d is used before.\n", unique_id);

   if(signal_handler_installed == 0) {
#if defined(LINUX)
      user_signal_number = allocate_and_register_linux_signal_number(&linux_signal_arb_mutex);
      if (user_signal_number == -1) return -1;
#endif   // LINUX

      install_ctrl_c_handler(0 /* use the custom signal handler */);
      signal_handler_installed = 1;
   }

   ACL_PCIE_DEVICE *pcie_dev = new ACL_PCIE_DEVICE( dev_num, name, unique_id, user_signal_number );
   if ( !pcie_dev->is_valid() ){
      delete pcie_dev;
      return -1;
   }

   s_handle_map[ unique_id ] = pcie_dev;
   s_device_name_map.insert( std::pair<int, const std::string>(unique_id, name));
   if (pcie_dev->is_initialized()) {
      return unique_id;
   } else {
      // Perform a bitwise-not operation to the unique_id if the device
      // do not pass the initial test. This negative unique_id indicates
      // a fail to open the device, but still provide actual the unique_id 
      // to allow reprogram executable to get access to the device and 
      // reprogram the board when the board is not usable. 
      return ~unique_id; 
   }
}

// Close an opened device, by its handle.
int AOCL_MMD_CALL aocl_mmd_close(int handle)
{
   delete get_pcie_device(handle);  
   discard_pcie_device_handle(handle);
   
   return 0;
}



// Set the interrupt handler for the opened device.
int AOCL_MMD_CALL aocl_mmd_set_interrupt_handler( int handle, aocl_mmd_interrupt_handler_fn fn, void* user_data )
{
   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1, 
      "aocl_mmd_set_interrupt_handler failed due to the target device (handle %d) is not properly initialized.\n", handle);

   return pcie_dev->set_kernel_interrupt(fn, user_data);
}

// Set the operation status handler for the opened device.
int AOCL_MMD_CALL aocl_mmd_set_status_handler( int handle, aocl_mmd_status_handler_fn fn, void* user_data )
{
   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1, 
      "aocl_mmd_set_status_handler failed due to the target device (handle %d) is not properly initialized.\n", handle);

   return pcie_dev->set_status_handler(fn, user_data);
}



// Called when the host is idle and hence possibly waiting for events to be
// processed by the device  
int AOCL_MMD_CALL aocl_mmd_yield(int handle)
{
   return get_pcie_device(handle)->yield();
}



// Read, write and copy operations on a single interface.
int AOCL_MMD_CALL aocl_mmd_read(
   int dev_handle,
   aocl_mmd_op_t op,
   size_t len,
   void* dst,
   int mmd_interface,
   size_t offset,
   // eyoh
   // if the mmd_interface is AOCL_MMD_KERNEL
   // the handle of the specific partition which corresponds to the destination kernel interface sould be transffered 
   // other wise handle of partition is set to -1
   int part_handle )
{
   void * host_addr = dst;
   size_t dev_addr  = offset;

   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(dev_handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1, 
      "aocl_mmd_read failed due to the target device (handle %d) is not properly initialized.\n", dev_handle);

   return pcie_dev->read_block( op, (aocl_mmd_interface_t)mmd_interface, host_addr, dev_addr, len, part_handle );
}

int AOCL_MMD_CALL aocl_mmd_write(
   int dev_handle,
   aocl_mmd_op_t op,
   size_t len,
   const void* src,
   int mmd_interface,
   size_t offset,
   // eyoh
   // if the mmd_interface is AOCL_MMD_KERNEL
   // the handle of the specific partition which corresponds to the destination kernel interface should be transffered 
   // other wise handle of partition is set to -1
   int part_handle ) 
{
   void * host_addr = const_cast<void *>(src);
   size_t dev_addr  = offset;

   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(dev_handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1, 
      "aocl_mmd_write failed due to the target device (handle %d) is not properly initialized.\n", dev_handle);

   return pcie_dev->write_block( op, (aocl_mmd_interface_t)mmd_interface, host_addr, dev_addr, len, part_handle );
}

int AOCL_MMD_CALL aocl_mmd_copy(
    int dev_handle,
    aocl_mmd_op_t op,
    size_t len,
    int mmd_interface,
    size_t src_offset,
    size_t dst_offset,
    // eyoh
   // if the mmd_interface is AOCL_MMD_KERNEL

   // the handle of the specific partition which corresponds to the destination kernel interface sould be transffered 
   // other wise handle of partition is set to -1
   int part_handle )
{
   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(dev_handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1, 
      "aocl_mmd_copy failed due to the target device (handle %d) is not properly initialized.\n", dev_handle);

   return pcie_dev->copy_block( op, (aocl_mmd_interface_t)mmd_interface, src_offset, dst_offset, len, part_handle );
}


// Initialize host channel specified in channel_name
int AOCL_MMD_CALL aocl_mmd_hostchannel_create(
    int handle,
    char * channel_name,
    size_t queue_depth,
    int direction)
{
    ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(handle);
    ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1,
       "aocl_mmd_create_hostchannel failed due to the target device (handle %d) is not properly initialized.\n", handle);
       
    return pcie_dev->create_hostchannel( channel_name, queue_depth, direction );
}

// reset the host channel specified with channel handle
int AOCL_MMD_CALL aocl_mmd_hostchannel_destroy(
      int handle,
      int channel)
{
    ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(handle);
    ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1,
       "aocl_mmd_create_hostchannel failed due to the target device (handle %d) is not properly initialized.\n", handle);
       
    return pcie_dev->destroy_channel(channel);
}

// Get the pointer to buffer the user can write/read from the kernel with
AOCL_MMD_CALL void* aocl_mmd_hostchannel_get_buffer(
   int handle,
   int channel,
   size_t *buffer_size,
   int *status)
{

   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return NULL, 
      "aocl_mmd_read failed due to the target device (handle %d) is not properly initialized.\n", handle);

   return pcie_dev->hostchannel_get_buffer(buffer_size , channel, status);
}

// Acknolwedge from the user that they have written/read send_size amount of buffer obtained from get_buffer
size_t AOCL_MMD_CALL aocl_mmd_hostchannel_ack_buffer(
   int handle,
   int channel,
   size_t send_size,
   int *status)
{

   ACL_PCIE_DEVICE *pcie_dev = get_pcie_device(handle);
   ACL_PCIE_ERROR_IF(!pcie_dev->is_initialized(), return -1, 
      "aocl_mmd_read failed due to the target device (handle %d) is not properly initialized.\n", handle);

   return pcie_dev->hostchannel_ack_buffer(send_size , channel, status);
}

// Reprogram the device
//eyoh
int AOCL_MMD_CALL aocl_mmd_reprogram(int dev_handle, void *data, size_t data_size, int part_handle)
{
   // assuming the an ELF-formatted blob.
   if ( !blob_has_elf_signature( data, data_size ) ) {
      ACL_PCIE_DEBUG_MSG("ad hoc fpga bin\n");
      return -1;
   }

   if( get_pcie_device(dev_handle)->reprogram( data, data_size, part_handle) ) {
      return -1;
   }

   // Delete and re-open the device to reinitialize hardware
   const std::string device_name = s_device_name_map[dev_handle];
   delete get_pcie_device(dev_handle);  
   discard_pcie_device_handle(dev_handle);

   return aocl_mmd_open(device_name.c_str());
}



// Shared memory allocator
AOCL_MMD_CALL void* aocl_mmd_shared_mem_alloc( int handle, size_t size, unsigned long long *device_ptr_out )
{
   return get_pcie_device(handle)->shared_mem_alloc (size, device_ptr_out);
}

// Shared memory de-allocator
AOCL_MMD_CALL void aocl_mmd_shared_mem_free ( int handle, void* host_ptr, size_t size )
{
   get_pcie_device(handle)->shared_mem_free (host_ptr, size);
}

// This function checks if the input data has an ELF-formatted blob.
// Return true when it does.
static bool blob_has_elf_signature( void* data, size_t data_size )
{
   bool result = false;
   if ( data && data_size > 4 ) {
      unsigned char* cdata = (unsigned char*)data;
      const unsigned char elf_signature[4] = { 0177, 'E', 'L', 'F' }; // Little endian
      result = (cdata[0] == elf_signature[0])
            && (cdata[1] == elf_signature[1])
            && (cdata[2] == elf_signature[2])
            && (cdata[3] == elf_signature[3]);
   }
   return result;
}


// Return a positive number when single device open. Otherwise, return -1
AOCL_MMD_CALL int get_open_handle()
{
   int open_devices = 0;
   int handle = 0;
   for (int i=0; i<30; i++)
   {
      if (s_handle_map.count(i) > 0)
      {
         open_devices++;
         handle = i;
      }
   }
   if (open_devices == 1)
      return handle;
   else
      return -1;
}

