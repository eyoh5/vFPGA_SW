#ifndef MMDAPP_H
#define MMDAPP_H

//#define USE_DMA 1

#include "serviceregistry.h"
#include "resourceregistry.h"
#include "queuemanager.h"
#include "aocl_mmd.h"
#include "parse_ppm.h"

#if defined(WINDOWS)
#  include "wdc_lib_wrapper.h"
#endif   // WINDOWS

#if defined(LINUX)
#  include "../../linux64/driver/hw_pcie_constants.h"
#  include "../../linux64/driver/pcie_linux_driver_exports.h"
#endif   // LINUX

#include <fstream>
#include <iomanip>
#include <stdarg.h>
#include <sys/time.h>

int convert_to_cra_addr(int offset);

struct __Device{
  int dev_id;
  int dev_handle;
  int glob_prr_id;
  int kernel_if;
  int mem_if;
} typedef __Device;


class MMDApp{
private:

  TaskQueueManager& TaskQ_mgr;
  ServiceRegistry& srv_reg;
  ResourceRegistry& rsc_reg;
  char* name;
  int service_id;
  float latency_threshold;
//  char logFileName[20] = "tmp";
//  std::fstream logFile;
  int logfile_first_access =1;
  int logfile_first_access2 =1;
  int logfile_first_access3 =1;
//  int num_requests = 0;  

public:

  MMDApp(TaskQueueManager& TaskQ_mgr, ServiceRegistry& srv_reg, ResourceRegistry& rsc_reg, char* name, float latency_threshold);
  ~MMDApp();
  void run(int job_id);
  __Device getDevice(int service_id);
  __Device getProgram(__Device device, int service_id);
  void runVecAdd(__Device device);
  void runSobel(__Device device);
  void runMatMul(__Device device);
  void releaseDevice(__Device device);
  int getServiceID();

};

#endif
