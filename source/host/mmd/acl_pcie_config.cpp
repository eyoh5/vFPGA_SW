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


/* ===- acl_pcie_config.cpp  ------------------------------------------ C++ -*-=== */
/*                                                                                 */
/*                         Intel(R) OpenCL MMD Driver                                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */
/*                                                                                 */
/* This file implements the class to handle functions that program the FPGA.       */
/* The declaration of the class lives in the acl_pcie_config.h.                    */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */


// common and its own header files
#include "acl_pcie.h"
#include "acl_pcie_config.h"

// other header files inside MMD driver
#include "acl_pcie_debug.h"

// other standard header files
#include <stdlib.h>
#include <string.h>

#if defined(LINUX)
#  include <unistd.h>
#endif   // LINUX

#include "pkg_editor.h"

// MAX size of line read from pipe-ing the output of find_jtag_cable.tcl to MMD
#define READ_SIZE 1024 
// MAX size of command passed to system for invoking find_jtag_cable.tcl from MMD
#define SYSTEM_CMD_SIZE 4*1024



// Function to install the signal handler for Ctrl-C
// Implemented inside acl_pcie.cpp
extern int install_ctrl_c_handler(int ingore_sig);



ACL_PCIE_CONFIG::ACL_PCIE_CONFIG(WDC_DEVICE_HANDLE device)
{
   m_device = device;   

#if defined(WINDOWS)
   m_slot   = ((WDC_DEVICE *)device)->slot.pciSlot;
   ACL_PCIE_ASSERT( find_upstream_slot(m_slot, &m_upstream), "cannot find the upstream slot.\n" ); 
#endif   // WINDOWS
   
   return;
}

ACL_PCIE_CONFIG::~ACL_PCIE_CONFIG() 
{
}



// Change the kernel region using PR only via PCIe, using an in-memory image of the core.rbf
// For Linux, the actual implementation of PR is inside the kernel mode driver.
// Return 0 on success.
int ACL_PCIE_CONFIG::program_core_with_PR_file(char *core_bitstream, size_t core_rbf_len, int part_handle)
{
   int pr_result = 1;   // set to default - failure 

   ACL_PCIE_ERROR_IF( core_bitstream == NULL, return 1, 
      "core_bitstream is an NULL pointer.\n" );
   ACL_PCIE_ERROR_IF( core_rbf_len < 1000000, return 1, 
      "size of core rbf file is suspiciously small.\n" );

#if defined(WINDOWS)
  int i, result;
  UINT32 to_send, status;
  UINT32 *data;

  ACL_PCIE_DEBUG_MSG(":: OK to proceed with PR!\n");

  MemoryBarrier();
  result = WDC_ReadAddr32(m_device, ACL_PRCONTROLLER_BAR, ACL_PRCONTROLLER_OFFSET+4, &status);
  ACL_PCIE_DEBUG_MSG(":: Reading 0x%08X from PR IP status register\n", (int) status);

  to_send = 0x00000001; 
  ACL_PCIE_DEBUG_MSG(":: Writing 0x%08X to PR IP status register\n", (int) to_send);
  WDC_WriteAddr32(m_device, ACL_PRCONTROLLER_BAR, ACL_PRCONTROLLER_OFFSET+4, to_send);

  MemoryBarrier();
  result = WDC_ReadAddr32(m_device, ACL_PRCONTROLLER_BAR, ACL_PRCONTROLLER_OFFSET+4, &status);
  ACL_PCIE_DEBUG_MSG(":: Reading 0x%08X from PR IP status register\n", (int) status);
  if ((status != 0x10) && (status != 0x0)) {
    ACL_PCIE_ERROR_IF(1, return 1, ":: PR IP not in an usable state.\n");
  }

  data = (UINT32 *)core_bitstream;
  ACL_PCIE_DEBUG_MSG(":: Writing %d bytes of bitstream file to PR IP at BAR %d, OFFSET 0x%08X\n", (int)core_rbf_len, (int)ACL_PRCONTROLLER_BAR, (int)ACL_PRCONTROLLER_OFFSET);
  for (i = 0; i < (int)core_rbf_len/4; i++) {
    WDC_WriteAddr32(m_device, ACL_PRCONTROLLER_BAR, ACL_PRCONTROLLER_OFFSET, data[i]);
  }

  result = WDC_ReadAddr32(m_device, ACL_PRCONTROLLER_BAR, ACL_PRCONTROLLER_OFFSET, &status);
  ACL_PCIE_DEBUG_MSG(":: Reading 0x%08X from PR IP data register\n", (int) status);

  MemoryBarrier();
  result = WDC_ReadAddr32(m_device, ACL_PRCONTROLLER_BAR, ACL_PRCONTROLLER_OFFSET+4, &status);
  ACL_PCIE_DEBUG_MSG(":: Reading 0x%08X from PR IP status register\n", (int) status);
  if (status == 0x14){
    ACL_PCIE_DEBUG_MSG(":: PR done!: 0x%08X\n", (int) status);
    pr_result = 0;
  } else {
    ACL_PCIE_DEBUG_MSG(":: PR error!: 0x%08X\n", (int) status);
    pr_result = 1;
  }

  ACL_PCIE_DEBUG_MSG(":: PR completed!\n");

#endif   // WINDOWS
#if defined(LINUX)
  struct acl_cmd cmd_pr = { ACLPCI_CMD_BAR, ACLPCI_CMD_DO_PR, NULL, NULL};

  // eyoh
  switch(part_handle){
  case 0: cmd_pr.command = ACLPCI_CMD_DO_PR; break;
  case 1: cmd_pr.command = ACLPCI_CMD_DO_PR_PARTITION_1; break;
  case 2: cmd_pr.command = ACLPCI_CMD_DO_PR_PARTITION_2; break;
  case 3: cmd_pr.command = ACLPCI_CMD_DO_PR_PARTITION_3; break;
  }
   cmd_pr.user_addr = core_bitstream;
   cmd_pr.size      = core_rbf_len;
     
   pr_result = read( m_device, &cmd_pr, sizeof(cmd_pr) );

#endif   // LINUX

   return pr_result;   
}


// Windows specific code to disable PCIe advanced error reporting on the
// upstream port.
// No-op in Linux because save_pcie_control_regs() has already disabled
// AER on the upstream port.
// Returns 0 on success
int ACL_PCIE_CONFIG::disable_AER_windows( int *has_aer, DWORD *location_of_aer ) {
#if defined(WINDOWS)
  DWORD WD_status;

  // Find location in extended config space of AER (id 0x0001) in upstream port
  *has_aer = find_extended_capability( m_upstream, PCIE_AER_CAPABILITY_ID, location_of_aer);

  // Disable AER by setting bit 5 (surprise down) in uncorrectable error mask register (offset 0x8, 4 bytes)
  // This prevents the root complex from reporting the error further upstream
  UINT32 data;
  if (*has_aer) {
    WD_status  = WDC_PciReadCfgBySlot (&m_upstream, (*location_of_aer)+PCIE_AER_UNCORRECTABLE_MASK_OFFSET, &data, 4);
    data      |= PCIE_AER_SURPRISE_DOWN_BIT;
    WD_status |= WDC_PciWriteCfgBySlot(&m_upstream, *(location_of_aer)+PCIE_AER_UNCORRECTABLE_MASK_OFFSET, &data, 4);

    ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
        "WDC_PciReadCfgBySlot/WDC_PciWriteCfgBySlot failed when programming with SOF file.\n");
  }
#endif   // WINDOWS
  return 0;
}


// Windows specific code to enable PCIe advanced error reporting on the
// upstream port.
// No-op in Linux because load_pcie_control_regs() has already enabled
// AER on the upstream port.
// Returns 0 on success
int ACL_PCIE_CONFIG::enable_AER_and_retrain_link_windows( int has_aer, DWORD location_of_aer ) {
#if defined(WINDOWS)
  DWORD WD_status;
  int status;

  status = load_pci_control_regs();
  ACL_PCIE_ERROR_IF(status, return -1, 
      "load_pci_control_regs failed programming with SOF file.\n");

  // Restore AER by resetting bit 5 (surprise down) in uncorrectable error mask register (offset 0x8)
  // But first, need to clear the surprise down error (bit 5) in the status register (offsets 0x4 and 0x10)
  // Errors are cleared by writing a 1 to that bit, so to clear all errors, write back the value just read
  UINT32 data;
  if (has_aer) {
    WD_status  = WDC_PciReadCfgBySlot ( &m_upstream, location_of_aer+PCIE_AER_UNCORRECTABLE_STATUS_OFFSET, &data, 4 );
    WD_status |= WDC_PciWriteCfgBySlot( &m_upstream, location_of_aer+PCIE_AER_UNCORRECTABLE_STATUS_OFFSET, &data, 4 );

    WD_status |= WDC_PciReadCfgBySlot ( &m_upstream, location_of_aer+PCIE_AER_CORRECTABLE_STATUS_OFFSET, &data, 4 );
    WD_status |= WDC_PciWriteCfgBySlot( &m_upstream, location_of_aer+PCIE_AER_CORRECTABLE_STATUS_OFFSET, &data, 4 );

    WD_status |= WDC_PciReadCfgBySlot ( &m_upstream, location_of_aer+PCIE_AER_UNCORRECTABLE_MASK_OFFSET, &data, 4 );
    data      &= ~PCIE_AER_SURPRISE_DOWN_BIT;
    WD_status |= WDC_PciWriteCfgBySlot( &m_upstream, location_of_aer+PCIE_AER_UNCORRECTABLE_MASK_OFFSET, &data, 4 );

    ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
        "WDC_PciReadCfgBySlot/WDC_PciWriteCfgBySlot failed when programming SOF file.\n");
  }

  // Retrain the link after reprogramming the FPGA
  status = retrain_link(m_slot);
  ACL_PCIE_ERROR_IF(status, return -1, 
      "Failed link retraining after programming SOF file.\n");
#endif   // WINDOWS

  return 0;
}


// Program the FPGA using a given SOF file
// Quartus is needed for this, because, 
//   quartus_pgm is used to program the board through USB blaster
// For Linux, when the kernel driver is asked to save/load_pcie_control_regs(),
//   it will also disable/enable the aer on the upstream, so no need to 
//   implement those here.
// NOTE: This function only works with single device machines - if there
// are multiple cards (and multiple USB-blasters) in the system, it doesn't
// properly determine which card is which.  Only the first device will be
// programmed.
// Return 0 on success.
int ACL_PCIE_CONFIG::program_with_SOF_file(const char *filename, const char *ad_cable, const char *ad_device_index) 
{
   const int MAX_ATTEMPTS   = 3;
   int       program_failed = 1;
   int   status;
   bool use_cable_autodetect = true;

   // If ad_cable value is "0", either JTAG cable autodetect failed or not
   // supported, then use the default value
   if (strcmp(ad_cable, "0") == 0)
     use_cable_autodetect = false;
 
   const char *cable = getenv("ACL_PCIE_JTAG_CABLE");
   if ( !cable ) { 
      if (use_cable_autodetect) {
         cable = ad_cable;
         ACL_PCIE_INFO("setting Cable to autodetect value %s\n",cable);
      } else {
         cable = "1";
         ACL_PCIE_INFO("setting Cable to default value %s\n",cable);
      }
   }

   const char *device_index = getenv("ACL_PCIE_JTAG_DEVICE_INDEX");
   if ( !device_index ) {
      if (use_cable_autodetect) { 
         device_index = ad_device_index;
         ACL_PCIE_INFO("setting Device Index to autodetect value %s\n",device_index);
      } else {
         device_index = "1";
         ACL_PCIE_INFO("setting Device Index to default value %s\n",device_index);
      }
   }

   char cmd[4*1024];
   sprintf(cmd, "quartus_pgm -c %s -m jtag -o \"P;%s@%s\"", cable, filename, device_index);
   ACL_PCIE_INFO("executing \"%s\"\n", cmd);

   // Disable AER
   int has_aer;
   DWORD location_of_aer;
   status = disable_AER_windows( &has_aer, &location_of_aer );
   ACL_PCIE_ERROR_IF(status, return -1, 
      "Failed to disable AER on Windows before programming SOF.\n");

   // Set the program to ignore the ctrl-c signal
   // This setting will be inherited by the systme() function call below,
   // so that the quartus_pgm call won't be interrupt by the ctrl-c signal.
   install_ctrl_c_handler(1 /* ignore the signal */);

   // Program FPGA by executing the command
   for( int attempts = 0; attempts < MAX_ATTEMPTS && program_failed; attempts++ ){
      program_failed = system(cmd);
#if defined(WINDOWS)
      WDC_Sleep( 2000000, WDC_SLEEP_NON_BUSY );
#endif   // WINDOWS
#if defined(LINUX)
      sleep(2);
#endif   // LINUX
   }

   // Restore the original custom ctrl-c signal handler
   install_ctrl_c_handler(0 /* use the custom signal handler */);

   // Enable AER
   status = enable_AER_and_retrain_link_windows( has_aer, location_of_aer );
   ACL_PCIE_ERROR_IF(status, return -1, 
      "Failed to enable AER and retrain link on Windows after programming SOF.\n");

   return program_failed;
}

bool ACL_PCIE_CONFIG::find_cable_with_ISSP(unsigned int cade_id, char *ad_cable, char *ad_device_index ) 
{
   FILE *fp;
   int status;
   char line_in[READ_SIZE];
   bool found_cable = false; 

   char cmd[SYSTEM_CMD_SIZE];
   const char *aocl_boardpkg_root = getenv("AOCL_BOARD_PACKAGE_ROOT");
   if (!aocl_boardpkg_root) {
      ACL_PCIE_INFO("AOCL_BOARD_PACKAGE_ROOT not set!!!");
      return false;   
   }
   
   sprintf(cmd, "quartus_stp -t %s/scripts/find_jtag_cable.tcl %X", aocl_boardpkg_root, cade_id);
   ACL_PCIE_DEBUG_MSG("executing \"%s\"\n", cmd);
   
   // Open PIPE to tcl script
#if defined(WINDOWS)
   fp = _popen(cmd, "r");
#endif   // WINDOWS
#if defined(LINUX)
   fp = popen(cmd, "r");
#endif   // LINUX

   if (fp == NULL) {
      ACL_PCIE_INFO("Couldn't open fp file\n");
   }
  
   // Read everyline and look for matching string from tcl script
   while (fgets(line_in, READ_SIZE, fp) != NULL) {
      ACL_PCIE_DEBUG_MSG("%s", line_in);
      const char* str_match_cable = "Matched Cable:";
      const char* str_match_dev_name = "Device Name:@";
      const char* str_match_end = ":";
      // parsing the string and extracting the cable/index value
      // from the output of find_jtag_cable.tcl script
      char* pos_cable = strstr(line_in, str_match_cable);
      if (pos_cable) {
         found_cable = true;
         // find the sub-string locations in the line
         char* pos_dev_name = strstr(line_in, str_match_dev_name);
         char* pos_end = strstr(pos_dev_name + strlen(str_match_dev_name), str_match_end); //Find the last ":" 
         // calculate the cable/index string size
         int i_cable_str_len = pos_dev_name - pos_cable - strlen(str_match_cable);
         int i_dev_index_str_len = pos_end - pos_dev_name - strlen(str_match_dev_name);
         // extract the cable/index value from the line
         sprintf(ad_cable,"%.*s", i_cable_str_len, pos_cable + strlen(str_match_cable));
         sprintf(ad_device_index,"%.*s", i_dev_index_str_len, pos_dev_name + strlen(str_match_dev_name));
         ACL_PCIE_INFO("JTAG Autodetect device found Cable:%s, Device Index:%s\n", ad_cable, ad_device_index); 
         break;
      } 
   }
   
#if defined(WINDOWS)
   status = _pclose(fp);
#endif   // WINDOWS
#if defined(LINUX)
   status = pclose(fp);
#endif   // LINUX
   if (status == -1) {
      /* Error reported by pclose() */
      ACL_PCIE_INFO("Couldn't close find_cable_with_ISSP file\n");
   } else {
      /* Use macros described under wait() to inspect `status' in order
      *        to determine success/failure of command executed by popen()
      *        */
   }
   
   if (!found_cable) {
      ACL_PCIE_INFO("Autodetect Cable not found!!\n");
   }
   return found_cable;
}



// Functions to save/load control registers form PCI Configuration Space
// This saved registers are used to restore the PCIe link after reprogramming
// through methods other than PR
// For Windows, the register values are stored in this class, and do 
//   nothing else
// For Linux, the register values are stored inside the kernel driver, 
//   And, it will disable the interrupt and the aer on the upstream, 
//   when the save_pci_control_regs() function is called. They will 
//   be enable when load_pci_control_regs() is called.  
// Return 0 on success
int ACL_PCIE_CONFIG::save_pci_control_regs() 
{
   int save_failed = 1;
   
#if defined(WINDOWS)
   save_failed = (int)WDC_PciReadCfg(m_device, 0, m_config_space, CONFIG_SPACE_SIZE);
#endif   // WINDOWS
#if defined(LINUX)
   struct acl_cmd cmd_save = { ACLPCI_CMD_BAR, ACLPCI_CMD_SAVE_PCI_CONTROL_REGS, NULL, NULL };
   save_failed = read(m_device, &cmd_save, 0);
#endif   // LINUX

   return save_failed;
}

int ACL_PCIE_CONFIG::load_pci_control_regs() 
{
   int load_failed = 1;
   
#if defined(WINDOWS)
   load_failed = (int)WDC_PciWriteCfg(m_device, 0, m_config_space, CONFIG_SPACE_SIZE);
#endif   // WINDOWS
#if defined(LINUX)
   struct acl_cmd cmd_load = { ACLPCI_CMD_BAR, ACLPCI_CMD_LOAD_PCI_CONTROL_REGS, NULL, NULL };
   load_failed = read(m_device, &cmd_load, 0);
#endif   // LINUX

   return load_failed;
}



// Functions to query the PCI related information 
// Use NULL as input for the info that you don't care about 
// Return 0 on success. 
int ACL_PCIE_CONFIG::query_pcie_info(unsigned int *pcie_gen, unsigned int *pcie_num_lanes, char *pcie_slot_info_str) 
{ 
   int            status = 0; 
#if defined(WINDOWS) 
   const DWORD  LINK_STATUS_OFFSET = 0x12; 
   DWORD        board_caps_offset; 
   unsigned int read_value = 0; 

   // Find the PCIe Capabilities Structure offset 
   ACL_PCIE_ERROR_IF( find_capabilities(m_slot, &board_caps_offset) != 1, return -1,  
      "cannot find capabilities when querying PCIE info.\n"); 

   DWORD board_status = board_caps_offset + LINK_STATUS_OFFSET; 
   DWORD WD_status = WDC_PciReadCfgBySlot( &m_slot, board_status, &read_value, 2 ); 
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1,  
      "WDC_PciReadCfgBySlot failed when querying PCIE info.\n"); 

   if( pcie_gen != NULL) { 
      *pcie_gen = read_value & 0xf; 
   } 

   if( pcie_num_lanes != NULL ) { 
      *pcie_num_lanes = (read_value & 0xf0) >> 4; 
   } 

   if( pcie_slot_info_str != NULL ) { 
      sprintf(pcie_slot_info_str, "%02x:%02x.%02x",  
         m_slot.dwBus, m_slot.dwSlot, m_slot.dwFunction); 
   } 
#endif   // WINDOWS 
#if defined(LINUX) 
   struct acl_cmd driver_cmd; 

   if( pcie_gen != NULL ) { 
      driver_cmd.bar_id      = ACLPCI_CMD_BAR; 
      driver_cmd.command     = ACLPCI_CMD_GET_PCI_GEN; 
      driver_cmd.device_addr = NULL; 
      driver_cmd.user_addr   = pcie_gen; 
      driver_cmd.size        = sizeof(*pcie_gen); 
      status |= read (m_device, &driver_cmd, sizeof(driver_cmd)); 
   }

   if( pcie_num_lanes != NULL ) { 
      driver_cmd.bar_id      = ACLPCI_CMD_BAR; 
      driver_cmd.command     = ACLPCI_CMD_GET_PCI_NUM_LANES; 
      driver_cmd.device_addr = NULL; 
      driver_cmd.user_addr   = pcie_num_lanes; 
      driver_cmd.size        = sizeof(*pcie_num_lanes); 
      status |= read (m_device, &driver_cmd, sizeof(driver_cmd)); 
   } 

   if( pcie_slot_info_str != NULL ) { 
      driver_cmd.bar_id      = ACLPCI_CMD_BAR; 
      driver_cmd.command     = ACLPCI_CMD_GET_PCI_SLOT_INFO; 
      driver_cmd.device_addr = NULL; 
      driver_cmd.user_addr   = pcie_slot_info_str; 
      driver_cmd.size        = sizeof(pcie_slot_info_str); 
      status |= read (m_device, &driver_cmd, sizeof(driver_cmd)); 
   } 
#endif   // LINUX 
   return status; 
} 



void ACL_PCIE_CONFIG::wait_seconds( unsigned seconds ) {
 #if defined(WINDOWS)
    WDC_Sleep( seconds * 1000000, WDC_SLEEP_NON_BUSY );
  #endif   // WINDOWS
  #if defined(LINUX)
    sleep( seconds );
  #endif   // LINUX
}


#if defined(WINDOWS)
// Retrain the PCIe link after programming FPGA with SOF file (Windows only)
// Return 0 on success
int ACL_PCIE_CONFIG::retrain_link(WD_PCI_SLOT &slot) 
{
   const int DESIRED_WIDTH = 8;
   const int GEN3_SPEED    = 3, GEN2_SPEED    = 2, GEN1_SPEED    = 1;

   const DWORD LINK_STATUS_OFFSET       = 0x12;
   const DWORD LINK_CONTROL_OFFSET      = 0x10;
   const DWORD LINK_CAPABILITIES_OFFSET = 0x0c;
   const DWORD RETRAIN_LINK_BIT         = (1 << 5);
   const DWORD TRAINING_IN_PROGRESS_BIT = (1 << 11);

   DWORD        WD_status;
   DWORD        board_caps_offset, usa_caps_offset;

   // Find the PCIe Capabilities Structure offset
   ACL_PCIE_ERROR_IF( find_capabilities(slot, &board_caps_offset) != 1, return -1, 
      "cannot find capabilities when retraining the link.\n");

   // Find the PCIe Capabilities Structure offset for the upstream port
   ACL_PCIE_ERROR_IF( find_capabilities(m_upstream, &usa_caps_offset)!= 1, return -1, 
      "cannot find PCIe capabilities structure for upstream port.\n" );

   // variables for checking status and link caps
   unsigned int status;
   UINT32       link_caps;

   DWORD board_status    = board_caps_offset + LINK_STATUS_OFFSET; 
   DWORD board_link_caps = board_caps_offset + LINK_CAPABILITIES_OFFSET;
   DWORD usa_status      = usa_caps_offset   + LINK_STATUS_OFFSET;
   DWORD usa_control     = usa_caps_offset   + LINK_CONTROL_OFFSET;
   DWORD usa_link_caps   = usa_caps_offset   + LINK_CAPABILITIES_OFFSET;

   // Check the current status for the board
   WD_status = WDC_PciReadCfgBySlot( &slot, board_status, &status, 2);
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
      "WDC_PciReadCfgBySlot failed when retraining the link.\n");

   if((status & 0xf) == GEN3_SPEED){ 
      ACL_PCIE_INFO("Link currently operating at 8 GT/s.\n");
   } else if((status & 0xf) == GEN2_SPEED){
      ACL_PCIE_INFO("Link currently operating at 5 GT/s.\n");
   } else if((status & 0xf) == GEN1_SPEED){
      ACL_PCIE_INFO("Link currently operating at 2.5 GT/s\n");
   } else{
      ACL_PCIE_INFO("Link currently operating at an unknown speed. Status register is %x\n", status);
   }

   // check the current link caps for the board
   WD_status = WDC_PciReadCfgBySlot( &slot, board_link_caps, &link_caps, 4 );
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
      "WDC_PciReadCfgBySlot failed when retraining the link.\n");

   unsigned int board_max_width       = (link_caps >> 4) & 0x3f;   // Link width: bits 9:4
   unsigned int supported_speed_bits  = (link_caps & 0x0f);        // Link speed: bits 3:0
   unsigned int desired_trained_speed = supported_speed_bits;

   float board_max_speed = (supported_speed_bits == GEN1_SPEED) ? 2.5f :
                           (supported_speed_bits == GEN2_SPEED) ? 5.0f :
                           (supported_speed_bits == GEN3_SPEED) ? 8.0f : //gen 3
                                                                  0.0f ;
   ACL_PCIE_DEBUG_MSG("Board max upstream width: x%d\n",       board_max_width);
   ACL_PCIE_DEBUG_MSG("Board max upstream speed: %.1f GT/s\n", board_max_speed);

   // check the current link caps for the upstream port
   WD_status = WDC_PciReadCfgBySlot( &m_upstream, usa_link_caps, &link_caps, 4 );
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
      "WDC_PciReadCfgBySlot failed when retraining the link.\n");

   unsigned int max_width = (link_caps >> 4) & 0x3f;   // Link width: bits 9:4
   supported_speed_bits   = (link_caps & 0x0f);        // Link speed: bits 3:0
   desired_trained_speed  = supported_speed_bits < desired_trained_speed ? 
      supported_speed_bits : desired_trained_speed;

   float max_speed = (supported_speed_bits == GEN1_SPEED) ? 2.5f :
                     (supported_speed_bits == GEN2_SPEED) ? 5.0f :
                     (supported_speed_bits == GEN3_SPEED) ? 8.0f : //gen 3
                                                            0.0f ;
   ACL_PCIE_DEBUG_MSG("Max upstream width: x%d\n",       max_width);
   ACL_PCIE_DEBUG_MSG("Max upstream speed: %.1f GT/s\n", max_speed);

   if(max_width < DESIRED_WIDTH) 
      ACL_PCIE_WARN_MSG("PCIe slot does not support more than %d lanes, PCIe throughput will be adversely affected.\n", DESIRED_WIDTH);
   if(supported_speed_bits < 2)
      ACL_PCIE_WARN_MSG("PCIe slot does not support gen2 operation.  PCIe throughput will be adversely affected.\n");

   // Retrain the link
   UINT16       control;
   WD_status  = WDC_PciReadCfgBySlot ( &m_upstream, usa_status,  &status,  2 );
   WD_status |= WDC_PciReadCfgBySlot ( &m_upstream, usa_control, &control, 2 );
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
      "WDC_PciReadCfgBySlot failed when retraining the link.\n");

   ACL_PCIE_DEBUG_MSG("Upstream status before retrain: 0x%x\n",  status);
   ACL_PCIE_DEBUG_MSG("Upstream control before retrain: 0x%x\n", control);

   DWORD wdata = control | RETRAIN_LINK_BIT;
   WD_status = WDC_PciWriteCfgBySlot( &m_upstream, usa_control, &wdata, 2 );
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
      "WDC_PciWriteCfgBySlot failed when retraining the link.\n");

   // Wait for training to complete
   bool training = true;
   UINT32 timeout = 0;
   while(training && timeout < 50) {
      WD_status |= WDC_PciReadCfgBySlot( &m_upstream, usa_status, &status, 2 );
      training = (status & (TRAINING_IN_PROGRESS_BIT));
      WDC_Sleep( 1000, WDC_SLEEP_BUSY ); // 1 ms
      ++timeout;
   }

   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
      "WDC_PciReadCfgBySlot failed when retraining the link.\n");

   ACL_PCIE_ERROR_IF(training, return -1, 
      "Link training timed out, PCIe link not established. \n");

   ACL_PCIE_DEBUG_MSG("Link training completed in %d ms.\n", timeout);

   // Check the new link speed
   WD_status = WDC_PciReadCfgBySlot( &slot, board_status, &status, 2 );
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return -1, 
      "WDC_PciReadCfgBySlot failed when retraining the link.\n");

   unsigned int lanes         = (status & 0xf0) >> 4;
   unsigned int trained_speed = status & 0xf;
   unsigned int real_trained_speed_per_lane = (trained_speed==GEN1_SPEED) ? 250U : 
                                              (trained_speed==GEN2_SPEED) ? 500U : 
                                              (trained_speed==GEN3_SPEED) ? 1000U : 
                                                                            0U;
   ACL_PCIE_INFO("Link operating at Gen %u with %u lanes.\n", trained_speed, lanes);
   ACL_PCIE_INFO("Expected peak bandwidth = %u MB/s\n",lanes*real_trained_speed_per_lane);

   unsigned int achievable_max_width = (board_max_width < max_width) ? 
      board_max_width : max_width;   //max_width is that of upstream

   if (lanes < achievable_max_width)
      ACL_PCIE_WARN_MSG("Link trained %d lanes whereas %d were desired.\n", lanes, achievable_max_width);

   ACL_PCIE_ERROR_IF(trained_speed < desired_trained_speed, return -1, 
      "Link trained at gen %d whereas gen %d was desired.\n", trained_speed, desired_trained_speed);

   return 0;  // success
}



// Try to find the upstream port for the current board slot
// Return 1 when the target is found
int ACL_PCIE_CONFIG::find_upstream_slot(WD_PCI_SLOT &slot, WD_PCI_SLOT *upstream) 
{
   int    found = 0;
   UINT32 data;
   DWORD  WD_status;

   // Enumerate all devices
   WDC_PCI_SCAN_RESULT scan_result;
   WD_status = WDC_PciScanDevices(0, 0, &scan_result);
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return 0, 
      "WDC_PciScanDevices failed when finding upstream slot.\n");

   for(unsigned int i = 0; i < scan_result.dwNumDevices; i++) {
      // Looking for a type 1 header
      // The top bit indicates multi-function support, ignore it (mask with 0x7f, not 0xff)
      WD_status = WDC_PciReadCfgBySlot( &(scan_result.deviceSlot[i]), 0x0C, &data, 4 );
      if ( WD_status != WD_STATUS_SUCCESS || ((data >> 16) & 0x7f) != 1 ) {
         continue;
      }

      // Check the secondary bus number - looking for the source of the bus with ID [bus]
      WD_status = WDC_PciReadCfgBySlot( &(scan_result.deviceSlot[i]), 0x18, &data, 4 );
      if ( WD_status != WD_STATUS_SUCCESS || ((data >> 8) & 0xff) != slot.dwBus ) {
         continue;
      }

      // Found the source of this bus - verify there is only one
      ACL_PCIE_ERROR_IF(found, return 0, 
         "Found multiple sources(%d and %d) for bus %d!\n", 
         upstream->dwBus, scan_result.deviceSlot[i], slot.dwBus);

      found = 1;
      *upstream = scan_result.deviceSlot[i];
   }

   return found;
}

// Try to find the PCIe capabilities structure offset
// Return 1 when the target is found
int ACL_PCIE_CONFIG::find_capabilities(WD_PCI_SLOT &slot, DWORD *offset)
{
   int    found = 0;
   UINT32 data;
   DWORD  WD_status;

   // Read the head of the caps list
   WD_status = WDC_PciReadCfgBySlot(&slot, 0x34, &data, 4);
   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return 0, 
      "WDC_PciReadCfgBySlot failed when finding capabilities.\n");
   ACL_PCIE_ERROR_IF(data == 0xffffffff, return 0,
      "get 0xffffffff when finding capabilities.\n");

   DWORD cap_ptr = (data & 0xff);

   // Walk the list of caps to find the PCIe caps structure
   while(cap_ptr != 0 && !found) {
      WD_status |= WDC_PciReadCfgBySlot(&slot, cap_ptr, &data, 4);

      // Grab the current caps struct and link to the next one
      *offset = cap_ptr;
      cap_ptr = ((data >> 8) & 0xff);

      // Looking for the PCIe caps ID
      if( (data & 0xff) != 0x10 )
         continue;

      // Found it :)
      found = 1;
   }

   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return 0, 
      "WDC_PciReadCfgBySlot failed when finding capabilities.\n");

   return found;
}   

// Try to find the location in extended config space of the given ID
// Return 1 when the target is found
int ACL_PCIE_CONFIG::find_extended_capability(WD_PCI_SLOT &slot, UINT16 ex_cap_id_to_find, DWORD *offset) 
{
   int found = 0;
   UINT32 data;
   DWORD WD_status = WD_STATUS_SUCCESS;

   DWORD ex_cap_ptr = 0x100;

   // Walk the link list of capabilities
   while(ex_cap_ptr != 0 && !found) {
      WD_status |= WDC_PciReadCfgBySlot( &slot, ex_cap_ptr, &data, 4 );

      // Grab the current caps struct and link to the next one
      *offset = ex_cap_ptr;
      ex_cap_ptr = ((data >> 20) & 0xfff);	//next extended capability on bits 31:20

      // extended capability id is on bottom 16 bits
      if( (data & 0xffff) != ex_cap_id_to_find )
         continue;

      // Found it :)
      found = 1;
   }

   ACL_PCIE_ERROR_IF(WD_status != WD_STATUS_SUCCESS, return 0, 
      "WDC_PciReadCfgBySlot failed when finding extended capability.\n");

   return found;
}
#endif   // WINDOWS

