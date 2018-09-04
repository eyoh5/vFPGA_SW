#ifndef ACL_PCIE_H
#define ACL_PCIE_H

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


/* ===- acl_pcie.h  --------------------------------------------------- C++ -*-=== */
/*                                                                                 */
/*                         Intel(R) OpenCL MMD Driver                                */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */
/*                                                                                 */
/* This file defines macros and types that are used inside the MMD driver          */
/*                                                                                 */
/* ===-------------------------------------------------------------------------=== */


#ifndef ACL_PCIE_EXPORT
#  define ACL_PCIE_EXPORT __declspec(dllimport)
#endif

#define MMD_VERSION AOCL_MMD_VERSION_STRING
#define KERNEL_DRIVER_VERSION_EXPECTED ACL_DRIVER_VERSION

#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include "cl_platform.h"
#include "hw_pcie_constants.h" // a10_ref/linux64/driver
#include "aocl_mmd.h"

#if defined(LINUX)
#  include <version.h>
#endif   // LINUX


#if defined(WINDOWS)
// Need DWORD, UINT32, etc.  
// But windows.h spits out a lot of spurious warnings.
#pragma warning( push )
#pragma warning( disable : 4668 )
#  include <windows.h>
#pragma warning( pop )

// WinDriver header files
#  include "wdc_lib_wrapper.h"

#  define INVALID_DEVICE (NULL)

// define for the format string for size_t type
#  define SIZE_FMT_U "%Iu"
#  define SIZE_FMT_X "%Ix"

#endif   // WINDOWS
#if defined(LINUX)
typedef uintptr_t            KPTR;
typedef ssize_t WDC_DEVICE_HANDLE;

typedef unsigned int        DWORD;
typedef unsigned long long  QWORD;
typedef char                 INT8;
typedef unsigned char       UINT8;
typedef int16_t             INT16;
typedef uint16_t           UINT16;
typedef int                 INT32;
typedef unsigned int       UINT32;
typedef long long           INT64;
typedef unsigned long long UINT64;

// Linux driver-specific exports
#  include "pcie_linux_driver_exports.h"

#  define INVALID_DEVICE (-1)
#  define WD_STATUS_SUCCESS 0

// define for the format string for size_t type
#  define SIZE_FMT_U "%zu"
#  define SIZE_FMT_X "%zx"

#endif   // LINUX

typedef enum {
  AOCL_MMD_KERNEL = 0,      // Control interface into kernel interface
  AOCL_MMD_MEMORY = 1,      // Data interface to device memory
  AOCL_MMD_PLL = 2,         // Interface for reconfigurable PLL
  AOCL_MMD_HOSTCH = 3
} aocl_mmd_interface_t;

// Describes the properties of key components in a standard ACL device
struct ACL_PCIE_DEVICE_DESCRIPTION
{
   DWORD vendor_id;
   DWORD device_id;
   char  pcie_info_str[1024];
};


#define ACL_PCIE_ASSERT(COND,...) \
   do { if ( !(COND) ) { \
      printf("\nMMD FATAL: %s:%d: ",__FILE__,__LINE__); printf(__VA_ARGS__); fflush(stdout); assert(0); } \
   } while(0)

#define ACL_PCIE_ERROR_IF(COND,NEXT,...) \
   do { if ( COND )  { \
      printf("\nMMD ERROR: " __VA_ARGS__); fflush(stdout); NEXT; } \
   } while(0)

#define ACL_PCIE_INFO(...) \
   do { \
      printf("MMD INFO : " __VA_ARGS__); fflush(stdout); \
   } while(0)

#endif   // ACL_PCIE_H
