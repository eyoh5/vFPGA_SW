#ifndef ACL_PCIE_CONFIG_H
#define ACL_PCIE_CONFIG_H

/* (C) 1992-2017 Intel Corporation.                             */
/* Intel, the Intel logo, Intel, MegaCore, NIOS II, Quartus and TalkBack words     */
/* and logos are trademarks of Intel Corporation or its subsidiaries in the U.S.   */
/* and/or other countries. Other marks and brands may be claimed as the property   */
/* of others. See Trademarks on intel.com for full list of Intel trademarks or     */
/* the Trademarks & Brands Names Database (if Intel) or See www.Intel.com/legal (if Altera)  */
/* Your use of Intel Corporation's design tools, logic functions and other         */
/* software and tools, and its AMPP partner logic functions, and any output        */
/* files any of the foregoing (including device programming or simulation          */
/* files), and any associated documentation or information are expressly subject   */
/* to the terms and conditions of the Altera Program License Subscription          */
/* Agreement, Intel MegaCore Function License Agreement, or other applicable       */
/* license agreement, including, without limitation, that your use is for the      */
/* sole purpose of programming logic devices manufactured by Intel and sold by     */
/* Intel or its authorized distributors.  Please refer to the applicable           */
/* agreement for further details.                                                  */


/* ===- acl_pcie_config.h  -------------------------------------------- C++ -*-=== */
/*                                                                                 */
/*                         Intel(R) OpenCL MMD Driver                                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */
/*                                                                                 */
/* This file declares the class to handle functions that program the FPGA.         */
/* The actual implementation of the class lives in the acl_pcie_config.cpp,        */
/* so look there for full documentation.                                           */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */


#define PCIE_AER_CAPABILITY_ID                  ((DWORD)0x0001)
#define PCIE_AER_UNCORRECTABLE_STATUS_OFFSET    ((DWORD)0x4)
#define PCIE_AER_UNCORRECTABLE_MASK_OFFSET      ((DWORD)0x8)
#define PCIE_AER_CORRECTABLE_STATUS_OFFSET      ((DWORD)0x10)
#define PCIE_AER_SURPRISE_DOWN_BIT              ((DWORD)(1<<5))

class ACL_PCIE_CONFIG
{
   public:
      ACL_PCIE_CONFIG(WDC_DEVICE_HANDLE device);
      ~ACL_PCIE_CONFIG();
      
      // Change the core only via PCIe, using an in-memory image of the core.rbf
      // This is supported only for Stratix V and newer devices. 
      // Return 0 on success.
      int program_core_with_PR_file(char *core_bitstream, size_t core_rbf_len, int part_handle);

      // Program the FPGA using a given SOF file
      // Input filename, autodetect cable, autodetect device index
      // Return 0 on success.
      int program_with_SOF_file(const char *filename, const char *ad_cable, const char *ad_device_index);
      
      // Look up CADEID using ISSP 
      // Return TRUE with cable value in ad_cable, ad_device_index if cable found
      // Otherwise return FALSE
      bool find_cable_with_ISSP(unsigned int cade_id, char *ad_cable, char *ad_device_index );

      // Functions to save/load control registers from PCI Configuration Space
      // Return 0 on success.
      int save_pci_control_regs();
      int load_pci_control_regs();

      // Functions to query the PCI related information 
      // Use NULL as input for the info that you don't care about 
      // Return 0 on success. 
      int query_pcie_info(unsigned int *pcie_gen, unsigned int *pcie_num_lanes, char *pcie_slot_info_str);

      // Windows-specific code to control AER, and retrain the link
      int enable_AER_and_retrain_link_windows( int has_aer, DWORD location_of_aer );
      int disable_AER_windows( int *has_aer, DWORD *location_of_aer );

      // Platform agnostic sleep (in seconds)
      void wait_seconds( unsigned seconds );

   private:
#if defined(WINDOWS)
      // Retrain the PCIe link after programming FPGA with SOF file (Windows only)
      // Return 0 on success.
      int  retrain_link            (WD_PCI_SLOT &slot);

      // Helper functions for finding the PCIe related stuff. (Windows only)
      // Return 1 when target is found.
      int  find_upstream_slot      (WD_PCI_SLOT &slot, WD_PCI_SLOT *upstream);
      int  find_capabilities       (WD_PCI_SLOT &slot, DWORD *offset);      
      int  find_extended_capability(WD_PCI_SLOT &slot, UINT16 ex_cap_id_to_find, DWORD *offset);  

      static const unsigned int CONFIG_SPACE_SIZE = 0x1000;
      char m_config_space[CONFIG_SPACE_SIZE];
      WD_PCI_SLOT m_slot;
      WD_PCI_SLOT m_upstream;      
#endif   // WINDOWS
      
      WDC_DEVICE_HANDLE   m_device; 
};

#endif // ACL_PCIE_CONFIG_H
