#ifndef ACL_PCIE_MM_IO_H
#define ACL_PCIE_MM_IO_H

#include <map>
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


/* ===- acl_pcie_mm_io.h  --------------------------------------------- C++ -*-=== */
/*                                                                                 */
/*                         Intel(R) OpenCL MMD Driver                                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */
/*                                                                                 */
/* This file declares the class to handle memory mapped IO over PCIe.              */
/* The actual implementation of the class lives in the acl_pcie_mm_io.cpp,         */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */


/*
 *
 */


//eyoh
//struct KERNEL_INFO{
//	int kernel_id;
//} typedef KERNEL_INFO;


class ACL_PCIE_MM_IO_DEVICE
{
   public: 
      ACL_PCIE_MM_IO_DEVICE (WDC_DEVICE_HANDLE device, DWORD bar, KPTR device_offset, const char* name , bool diff_endian = false, int is_kernel = false);

      ~ACL_PCIE_MM_IO_DEVICE();


      DWORD bar_id()                           { return m_bar; };
      KPTR  convert_to_bar_addr( size_t addr ) { return addr + m_offset; };

      // read/write functions to the memory-mapped io device
      // return 0 on success, negative on error
      int read8  ( size_t addr, UINT8  *data );
      int write8 ( size_t addr, UINT8   data );
      int read16 ( size_t addr, UINT16 *data );
      int write16( size_t addr, UINT16  data );
      int read32 ( size_t addr, UINT32 *data );
      int write32( size_t addr, UINT32  data );
      int read64 ( size_t addr, UINT64 *data );
      int write64( size_t addr, UINT64  data );

      int read_block ( size_t addr, size_t size, void *dst );
      int write_block( size_t addr, size_t size, void *src );

   private:
      static const int MAX_NAME_LENGTH = 32;
      
      // Helper functions
      inline void   *compute_address( void* base, uintptr_t offset );

      char              m_name[MAX_NAME_LENGTH];
      WDC_DEVICE_HANDLE m_device;
      DWORD             m_bar;
      KPTR              m_offset;
      bool              m_diff_endian;  //indicates if the host and this device have different endianess
	
	// eyoh
	// if the MM_IO_DEVICE is kernel interface, it represents a partition
	// in that case, the is_kernel is a partition ID number.
 	// int the case of non-kernel interface, the is_kernel value will be -1 
	int is_kernel = -1;
	//KERNEL_INFO kernel_info;
	//int set_kernel_info();
};

/*
 * Utility functions to clean up the various address translations for reads/writes
 */
class ACL_PCIE_MM_IO_MGR
{
   public:
      ACL_PCIE_MM_IO_MGR( WDC_DEVICE_HANDLE device, int num_partitions );
      ~ACL_PCIE_MM_IO_MGR();

      ACL_PCIE_MM_IO_DEVICE *mem;
      ACL_PCIE_MM_IO_DEVICE *pcie_cra;
      ACL_PCIE_MM_IO_DEVICE *dma;
      ACL_PCIE_MM_IO_DEVICE *window;
      ACL_PCIE_MM_IO_DEVICE *version;
      ACL_PCIE_MM_IO_DEVICE *pr_base_id;
      ACL_PCIE_MM_IO_DEVICE *quartus_ver;
      ACL_PCIE_MM_IO_DEVICE *cade_id;
      ACL_PCIE_MM_IO_DEVICE *uniphy_status;
      ACL_PCIE_MM_IO_DEVICE *uniphy_reset;
	//eyoh
      //ACL_PCIE_MM_IO_DEVICE *kernel_if;
      std::map<int, ACL_PCIE_MM_IO_DEVICE*> *kernel_if_map;
      ACL_PCIE_MM_IO_DEVICE *pll;
      ACL_PCIE_MM_IO_DEVICE *temp_sensor;
      ACL_PCIE_MM_IO_DEVICE *hostch_ver;
};

#endif // ACL_PCIE_MM_IO_H

