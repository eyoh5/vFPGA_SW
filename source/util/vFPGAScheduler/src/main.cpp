#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <sstream>   // std::ostringstream
#include <iomanip>   // std::setw
#include <thread> // std::thread
#include <queue> // std::queue

#include "queuemanager.h"
#include "daemon.h"
#include "vfpgascheduler.h"
#include "mmdapp.h"
#include "resourceregistry.h"
#include "serviceregistry.h"

#undef _GNU_SOURCE

#include "aocl_mmd.h"

#if defined(WINDOWS)
#  include "wdc_lib_wrapper.h"
#endif   // WINDOWS

#if defined(LINUX)
#  include "../../../../linux64/driver/hw_pcie_constants.h"
#endif   // LINU


#define TOT_REQUEST 1
#define AVG_ARRIV_RATE 100

void init_service_reg(ServiceRegistry& srv_reg);

int main (int argc, char *argv[]){

  std::queue<IQEntry*> IQ;
  std::queue<IQEntry*> PRQ;
  ResourceRegistry rsc_reg = ResourceRegistry();
  ServiceRegistry srv_reg = ServiceRegistry();

  vFPGAScheduler scheduler(rsc_reg, srv_reg);
  QueueManager q_mgr = QueueManager(IQ, PRQ, scheduler);
 
  Daemon daemon = Daemon(q_mgr, rsc_reg, srv_reg, scheduler);


  std::thread t_daemon(&Daemon::run, daemon);

//  MMDApp app1 = MMDApp(q_mgr, srv_reg, rsc_reg, "MatMul");
  MMDApp app2 = MMDApp(q_mgr, srv_reg, rsc_reg, "VecAdd"); 
//  MMDApp app3 = MMDApp(q_mgr, srv_reg, rsc_reg, "FFT2D");
//  MMDApp app4 = MMDApp(q_mgr, srv_reg, rsc_reg, "Sobel");

//  std::thread t_app1(&MMDApp::run, app1, AVG_ARRIV_RATE, TOT_REQUEST); // with avg. arrival rate 100
  std::thread t_app2(&MMDApp::run, app2, AVG_ARRIV_RATE, TOT_REQUEST); // with avg. arrival rate 100
//  std::thread t_app3(&MMDApp::run, app3, AVG_ARRIV_RATE, TOT_REQUEST); // with avg. arrival rate 100
//  std::thread t_app4(&MMDApp::run, app4, AVG_ARRIV_RATE, TOT_REQUEST); //with avg. arrival rate 100

  t_daemon.join();
//  t_app1.join();
  t_app2.join();
//  t_app3.join();
//  t_app4.join();

  return 0;
}

//void init_service_reg(ServiceRegistry& srv_reg){
//
//	ServiceInfo service_info;
//	service_info.name = "AES";
//	service_info.id = 1;
//	service_info.bin_partition_map.insert( std::pair<char*, int>("/temporal_aes_bin_file_dir", 1) );
//
//	
//	srv_reg.AddEntry(service_info);
//
//
//	service_info.name = "MatMul";
//	service_info.id = 2;
//	service_info.bin_partition_map.insert( std::pair<char*, int>("/temporal_mm_bin_file_dir", 1) );
//
//
//	srv_reg.AddEntry(service_info);	
//
//}

