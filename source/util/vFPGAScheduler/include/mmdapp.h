#ifndef MMDAPP_H
#define MMDAPP_H

#include "queuemanager.h"
#include "serviceregistry.h"
#include "resourceregistry.h"

//#include "aclutil.h"
#include "aocl_mmd.h"
#include "parse_ppm.h" // sobel filter application

#if defined(WINDOWS)
#  include "wdc_lib_wrapper.h"
#endif   // WINDOWS

#if defined(LINUX)
#  include "../../linux64/driver/hw_pcie_constants.h"
#  include "../../linux64/driver/pcie_linux_driver_exports.h"
#endif   // LINUX

#include <random>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdarg.h>
#include <sys/time.h>


class MMDApp {
  private:
	QueueManager& q_mgr;
	ServiceRegistry& srv_reg;
	ResourceRegistry& rsc_reg;
	ServiceInfo service_info;
	char* name;
  	int id;
  public:
	MMDApp(QueueManager& q_mgr, ServiceRegistry& srv_reg, ResourceRegistry& rsc_reg, char* name);
	~MMDApp();
	void run(int avg_arriv_rate, int tot_request);
	ServiceInfo registerService(char* name,int num_bins, ...);
	HardwareInfo getHardware(ServiceInfo service_info);
	void releaseHardware(HardwareInfo hw_info);
	void runFFT2D(HardwareInfo hw_info);
	void runVecAdd(HardwareInfo hw_info);
	void runSobel(HardwareInfo hw_info);
	void runMatMul(HardwareInfo hw_info);
	HardwareInfo getProgram(HardwareInfo hw_info, ServiceInfo srv_info);
};

#endif
