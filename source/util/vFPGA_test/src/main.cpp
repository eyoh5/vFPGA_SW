#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <fstream>
#include <sstream>   // std::ostringstream
#include <fcntl.h>
#include <unistd.h>
#include <iomanip>   // std::setw
#include <thread> // std::thread
#include <queue> // std::queue

#undef _GNU_SOURCE

#include "aocl_mmd.h"

#if defined(WINDOWS)
#  include "wdc_lib_wrapper.h"
#endif   // WINDOWS

#if defined(LINUX)
#  include "../../../../linux64/driver/hw_pcie_constants.h"
#  include "../../../../linux64/driver/pcie_linux_driver_exports.h"
#endif   // LINU


#define TOT_REQUEST 1
#define AVG_ARRIV_RATE 100

 unsigned char *loadFileIntoMemory (const char *in_file, size_t *file_size_out) {
 
   FILE *f = NULL;
   unsigned char *buf;
   size_t file_size;
 
   // When reading as binary file, no new-line translation is done.
   f = fopen (in_file, "rb");
   if (f == NULL) {
     fprintf (stderr, "Couldn't open file %s for reading\n", in_file);
     return NULL;
   }
 
   // get file size
   fseek (f, 0, SEEK_END);
   file_size = (size_t)ftell (f);
   rewind (f);
 
   // slurp the whole file into allocated buf
   buf = (unsigned char*) malloc (sizeof(char) * file_size);
   *file_size_out = fread (buf, sizeof(char), file_size, f);
   fclose (f);
 
   if (*file_size_out != file_size) {
     fprintf (stderr, "Error reading %s. Read only %lu out of %lu bytes\n",
                      in_file, *file_size_out, file_size);
     return NULL;
   }
   return buf;
}




int main (int argc, char *argv[]){


  // get all supported board names from MMD
  char boards_name[1024];
  aocl_mmd_get_offline_info(AOCL_MMD_BOARD_NAMES, sizeof(boards_name), boards_name, NULL);
  std::cout<<"[aocl_mmd_get_offline_info] board names :"<<boards_name<<"\n";

  char* dev_name;
  dev_name = strtok(boards_name, ";");
  std::cout<<"[strtok] the first board name : "<< dev_name <<"\n";

  int handle;
  handle = aocl_mmd_open(dev_name);
  std::cout<<"[aocl_mmd_open] handle : "<<handle<<"\n";

  int num_partitions;
  aocl_mmd_get_info(handle, AOCL_MMD_NUM_PARTITIONS, sizeof(num_partitions), &num_partitions, NULL);
  std::cout<<"[aocl_mmd_get_info] num_partitions : "<<num_partitions<<"\n";

  char* bin_file_dir = "../bin/VecAdd/fpga.bin";
  unsigned char* bin_file = NULL;
  size_t bin_file_size;
  
  bin_file = loadFileIntoMemory(bin_file_dir, &bin_file_size);

  handle = aocl_mmd_reprogram(handle, bin_file, bin_file_size, 0);
  std::cout<<"[aocl_mmd_reprogram] PR result : "<<handle<<"\n";

  int kernel_if;
  int mmd_res;
  mmd_res = aocl_mmd_get_info(handle, AOCL_MMD_KERNEL_INTERFACES, sizeof(kernel_if), &kernel_if, NULL);
  std::cout<<"[aocl_mmd_get_info] kernel interface : "<< kernel_if<<"\n";
  std::cout<<"[aocl_mmd_get_info] result : "<<mmd_res<<"\n";

  int mem_if;
  mmd_res = aocl_mmd_get_info(handle, AOCL_MMD_MEMORY_INTERFACE, sizeof(mem_if), &mem_if, NULL);
  std::cout<<"[aocl_mmd_get_info] memory interface : " <<mem_if <<"\n";
  std::cout<<"[aocl_mmd_get_info] result : "<<mmd_res<<"\n";

// ////// Setting input arguments
//  float* input_a = new float[100];
//  float* input_b = new float[100];
//  float* output = new float[100];
//
//  for(int i=0; i<100; i++){
//        input_a[i] = i;
//        input_b[i] = i;
//        output[i] = 0;
//  }
//
//  mmd_res = aocl_mmd_write(handle, NULL, sizeof(float)*100, input_a, mem_if, 0);
//  std::cout<<"[aocl_mmd_write] result : "<<mmd_res<<"\n";
//  mmd_res = aocl_mmd_write(handle, NULL, sizeof(float)*100, input_b, mem_if, 400);
//  std::cout<<"[aocl_mmd_write] result : "<<mmd_res<<"\n";
// 
//
//  // Read back the values to check
//  float* rd_a = new float[100];
//  mmd_res = aocl_mmd_read(handle, NULL, sizeof(float)*100, rd_a, mem_if, 0);
//  std::cout<<"[aocl_mmd_read] result : "<<mmd_res<<"\n";
//
//  for(int i=0; i<10; i++){ std::cout<<"a["<<i<<"] : "<<rd_a[i]<<"  ";}
//  std::cout<<"...\n";
//  for(int i=90; i<100; i++){ std::cout<<"a["<<i<<"] : "<<rd_a[i]<<"  ";}
//  std::cout<<"...\n";
//
//  mmd_res = aocl_mmd_read(handle, NULL, sizeof(float)*100, rd_a, mem_if, 400);
//  std::cout<<"[aocl_mmd_read] result : "<<mmd_res<<"\n";
//
//  for(int i=0; i<10; i++){ std::cout<<"b["<<i<<"] : "<<rd_a[i]<<"  ";}
//  std::cout<<"...\n";
//  for(int i=90; i<100; i++){ std::cout<<"b["<<i<<"] : "<<rd_a[i]<<"  ";}
//  std::cout<<"...\n";

  
  

  /// DD test
  //open 
//  ssize_t m_device = open("/dev/acla10_vfpga0", O_RDWR);
//  std::cout<<"[DD] open result : "<<m_device<<"\n";
//  if(m_device == -1){
//    printf("Worng device\n");    
//  }
//
//  char* bin_file_dir = "../bin/VecAdd/top.kernel.rbf";
//  unsigned char* bin_file = NULL;
//  size_t bin_file_size;
//  
//  bin_file = loadFileIntoMemory(bin_file_dir, &bin_file_size);
//
//  struct acl_cmd driver_cmd = {ACLPCI_CMD_BAR, ACLPCI_CMD_DO_PR, NULL, NULL};
//  driver_cmd.user_addr = bin_file;
//  driver_cmd.size = bin_file_size;
//  int pr_result = read(m_device, &driver_cmd, sizeof(driver_cmd)) ;
//  std::cout<<"[DD] PR result : "<<pr_result<<"\n";

 return 0;

}

