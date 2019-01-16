#define _GNU_SOURC1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <thread>
#include <queue>
#include <unistd.h>
#include <assert.h>

#include "queuemanager.h"
#include "serviceregistry.h"
#include "resourceregistry.h"
#include "resourcescaler.h"
#include "aclutil.h"
#include "mmdapp.h"

#undef _GNU_SOURCE

#include "aocl_mmd.h"

#if defined(WINDOWS)
#  include "wdc_lib_wrapper.h"
#endif   // WINDOWS

#if defined(LINUX)
#  include "../../../../linux64/driver/hw_pcie_constants.h"
#endif   // LINU

/*
*  Simulation Parameters
*/
#define TOT_REQUEST 100
#define AVG_ARRIV_RATE 1.6 
#define RPT_PERIOD 1
#define SCALE_PERIOD 10
#define WINDOW_SIZE 5

/*
*  Shared global variables (or objects)
*/
static std::queue<QEntry*> TaskQ;
static std::map<int, std::queue<QEntry*>> PRQs; // <device ID, PRQ>
static std::map<int, std::queue<QEntry*>> PRRQs; // <global prr ID, PRRQ>
static ResourceRegistry rsc_reg = ResourceRegistry();
static ServiceRegistry srv_reg = ServiceRegistry();
static std::map<int, PRQueueManager*> PRQ_mgrs; // <device Id, PRQ_mgr>
static std::map<int, PRRQueueManager*> PRRQ_mgrs; // <global prr Id, PRRQ_mgr>
static ResourceScaler scaler(rsc_reg, srv_reg, WINDOW_SIZE);
static TaskQueueManager TaskQ_mgr(TaskQ, PRQ_mgrs, PRRQ_mgrs, rsc_reg, srv_reg, scaler, 0, RPT_PERIOD, SCALE_PERIOD); 
static int num_devices = 0;
static int glob_num_prrs = 0;

/*
*  Utility Functions in main
*/
void Init();
void RunDaemon();
void RunSim(int arrv_rate_0, int arrv_rate_1, int arrv_rate_2);
int* zipf_sequence(int tot_services, int tot_requests);
float* load_sequence(char* load_file_name, int tot_requests);
void irq_fn(int handle, void* user_data);
void srq_fn(int handle, void* user_data, aocl_mmd_op_t op, int status);
void request_gen(MMDApp* mmd_app, float avg_arriv_rate);

/*
*  Main Function
*/
int main (int argc, char* argv[]){

  if( argc != 4){
    printf("Syntax: ./scheduler [serv1. arriv rate] [serv2. arriv rate] [serv3. arriv rate]\n");
   exit(0);
  }

  int arrv_rate_0 = atof(argv[1]);
  int arrv_rate_1 = atof(argv[2]);
  int arrv_rate_2 = atof(argv[3]);

  Init();
  std::thread t_daemon (RunDaemon);
  RunSim(arrv_rate_0, arrv_rate_1, arrv_rate_2); 

  t_daemon.join();

  return 0;

}

void Init(){

  /*
  *  Search all devices on this node
  */
  char boards_name[MMD_STRING_RETURN_SIZE];
  aocl_mmd_get_offline_info(AOCL_MMD_BOARD_NAMES, sizeof(boards_name), boards_name, NULL);

  char* dev_name;
  for(dev_name = strtok(boards_name, ";"); dev_name != NULL ; dev_name = strtok(NULL, ";")){
    /*
    *   Create new device entry and Add to the resource registry
    */ 
    Device device;

    // set device name, e.g., a10_vfpga0, a10_ref0 
    device.dev_name = std::string(dev_name);

    // set device handle used by mmd library
    int dev_handle = aocl_mmd_open(dev_name);
    device.dev_handle = dev_handle;

    // set board name, e.g., Altera Arria 10 GX 
    char board_name[MMD_STRING_RETURN_SIZE];
    aocl_mmd_get_info(dev_handle, AOCL_MMD_BOARD_NAME, sizeof(board_name), board_name, NULL);
    device.board_name = std::string(board_name);

    // set number of partitions
    int num_partitions;
    aocl_mmd_get_info(dev_handle, AOCL_MMD_NUM_PARTITIONS, sizeof(num_partitions), &num_partitions, NULL);
    device.num_prrs = num_partitions;

    // set init status of PRRs
    device.prrs = new Partition[num_partitions];
    for(int i=0; i<num_partitions; i++){
      device.prrs[i].id = i;
      
      switch(i){
        case 0 : device.prrs[i].size = 1; break;
        case 1 : device.prrs[i].size = 4; break;
        case 2 : device.prrs[i].size = 4; break;
        case 3 : device.prrs[i].size = 1; break;
      }   
   
   }

    /*
    *  Set Interrupt Handler of Device
    */
    // interrupt handler
    char* usr_data = "Init Interrupt Handler";
//    aocl_mmd_set_interrupt_handler(dev_handle, irq_fn(dev_handle, usr_data),usr_data);
    aocl_mmd_set_interrupt_handler(dev_handle, irq_fn, usr_data);
    // status handler
    int status;
//    aocl_mmd_set_status_handler(dev_handle, srq_fn(dev_handle, usr_data, NULL, status ), usr_data);
    aocl_mmd_set_status_handler(dev_handle, srq_fn, usr_data);
    printf("Status done\n");
    rsc_reg.registerDevice(device);
    num_devices++;

  }

  /*
  *  Init queues and queue managers of this system
  */
  int prr_glob_id ;
  for( int i=0; i<num_devices; i++){
    PRQs.insert(std::pair<int, std::queue<QEntry*>>(i, std::queue<QEntry*>()));
    PRQ_mgrs.insert(std::pair<int, PRQueueManager*>(i, new PRQueueManager(PRQs[i], rsc_reg, srv_reg, i)));
    for( int j=0; j<rsc_reg.getEntry(i).num_prrs; j++){
      prr_glob_id = i ? i*rsc_reg.getEntry(i-1).num_prrs + j : j ;
      PRRQs.insert(std::pair<int, std::queue<QEntry*>>(prr_glob_id, std::queue<QEntry*>()));
      PRRQ_mgrs.insert(std::pair<int, PRRQueueManager*>(prr_glob_id, new PRRQueueManager(PRRQs[prr_glob_id], rsc_reg, prr_glob_id)));
      glob_num_prrs ++;
    }
  }

  printf("queue done\n");
  /*
  *  Init Scaler
  */
   scaler.initWindow(glob_num_prrs);
  printf("scaler done\n");

   rsc_reg.print();
}

void RunDaemon(){

  // Task Q daemon
  std::thread t_task_q(&TaskQueueManager::runDaemon, TaskQ_mgr);


  // PR Q and PRR Q daemon
  int prr_glob_id ;
  std::vector<std::thread> t_pr_q;
  std::vector<std::thread> t_prr_q;
  for (int i=0; i<num_devices; i++){
    t_pr_q.push_back(std::thread(&PRQueueManager::runDaemon, PRQ_mgrs[i]));
    for (int j=0; j<rsc_reg.getEntry(i).num_prrs; j++){
      prr_glob_id = i ? i*rsc_reg.getEntry(i-1).num_prrs + j : j ;
      t_prr_q.push_back(std::thread(&PRRQueueManager::runDaemon, PRRQ_mgrs[prr_glob_id]));
    }
  }

  t_task_q.join();
  for( auto &t : t_pr_q){ t.join(); }
  for( auto &t : t_prr_q) { t.join(); }
 
}

void RunSim(int arrv_rate_0, int arrv_rate_1, int arrv_rate_2){

  /*
  *  Simulation Init
  *  1) registerServices
  *  2) create application objects
  *  3) initialize servie rank 
  *  4) generate service request sequences
  *  5) generate request according to request sequences
  */

  // registerService  
//  int VecAdd_id = srv_reg.registerService("VecAdd", 8, "../bin/pr_bin/VecAdd/fpga.bin", "acla10_vfpga0", 0,
//                                                       "../bin/pr_bin/VecAdd/fpga_1.bin", "acla10_vfpga0", 1,
//                                                       "../bin/pr_bin/VecAdd/fpga_2.bin", "acla10_vfpga0", 2,
//                                                       "../bin/pr_bin/VecAdd/fpga_3.bin", "acla10_vfpga0", 3,
//                                                       "../bin/pr_bin/VecAdd/fpga.bin", "acla10_vfpga1", 0,
//                                                       "../bin/pr_bin/VecAdd/fpga_1.bin", "acla10_vfpga1", 1,
//                                                       "../bin/pr_bin/VecAdd/fpga_2.bin", "acla10_vfpga1", 2,
//                                                       "../bin/pr_bin/VecAdd/fpga_3.bin", "acla10_vfpga1", 3, 1.0);
//  assert(VecAdd_id >=0 && "Error: registerService failure\n");
//  int MatMul_id = srv_reg.registerService("MatMul", 4, "../bin/pr_bin/MatMul/fpga_1.bin", "acla10_vfpga0", 1,
//                                                       "../bin/pr_bin/MatMul/fpga_1.bin", "acla10_vfpga1", 1,
//                                                       "../bin/pr_bin/MatMul/fpga_2.bin", "acla10_vfpga0", 2,
//                                                       "../bin/pr_bin/MatMul/fpga_2.bin", "acla10_vfpga1", 2, 2.0);
//  assert(MatMul_id >=0 && "Error: registerService failure\n");
//  int Sobel_id = srv_reg.registerService("Sobel", 4, "../bin/pr_bin/Sobel/fpga_1.bin", "acla10_vfpga0", 1,
//                                                      "../bin/pr_bin/Sobel/fpga_1.bin", "acla10_vfpga1", 1,
//                                                      "../bin/pr_bin/Sobel/fpga_2.bin", "acla10_vfpga0", 2,
//                                                      "../bin/pr_bin/Sobel/fpga_2.bin", "acla10_vfpga1", 2,  1.0);
//  assert(Sobel_id >=0 && "Error: registerService failure\n");

  int VecAdd_id = srv_reg.registerService("VecAdd", 8, "../bin/pr_bin/VecAdd/fpga.bin", "acla10_vfpga0", 0,
                                                       "../bin/pr_bin/VecAdd/fpga_1.bin", "acla10_vfpga0", 1,
                                                       "../bin/pr_bin/VecAdd/fpga_2.bin", "acla10_vfpga0", 2,
                                                       "../bin/pr_bin/VecAdd/fpga_3.bin", "acla10_vfpga0", 3,
                                                       "../bin/pr_bin/VecAdd/fpga.bin", "acla10_vfpga1", 0,
                                                       "../bin/pr_bin/VecAdd/fpga_1.bin", "acla10_vfpga1", 1,
                                                       "../bin/pr_bin/VecAdd/fpga_2.bin", "acla10_vfpga1", 2,
                                                       "../bin/pr_bin/VecAdd/fpga_3.bin", "acla10_vfpga1", 3, 1.0);
  assert(VecAdd_id >=0 && "Error: registerService failure\n");
  int MatMul_id = srv_reg.registerService("MatMul", 8, "../bin/pr_bin/MatMul/fpga.bin", "acla10_vfpga0", 0,
                                                       "../bin/pr_bin/MatMul/fpga_1.bin", "acla10_vfpga0", 1,
                                                       "../bin/pr_bin/MatMul/fpga_2.bin", "acla10_vfpga0", 2,
                                                       "../bin/pr_bin/MatMul/fpga_3.bin", "acla10_vfpga0", 3,
                                                       "../bin/pr_bin/MatMul/fpga.bin", "acla10_vfpga1", 0,
                                                       "../bin/pr_bin/MatMul/fpga_1.bin", "acla10_vfpga1", 1,
                                                       "../bin/pr_bin/MatMul/fpga_2.bin", "acla10_vfpga1", 2,
                                                       "../bin/pr_bin/MatMul/fpga_3.bin", "acla10_vfpga1", 3, 1.0);
  assert(MatMul_id >=0 && "Error: registerService failure\n");
  int Sobel_id = srv_reg.registerService("Sobel", 8, "../bin/pr_bin/Sobel/fpga.bin", "acla10_vfpga0", 0,
                                                       "../bin/pr_bin/Sobel/fpga_1.bin", "acla10_vfpga0", 1,
                                                       "../bin/pr_bin/Sobel/fpga_2.bin", "acla10_vfpga0", 2,
                                                       "../bin/pr_bin/Sobel/fpga_3.bin", "acla10_vfpga0", 3,
                                                       "../bin/pr_bin/Sobel/fpga.bin", "acla10_vfpga1", 0,
                                                       "../bin/pr_bin/Sobel/fpga_1.bin", "acla10_vfpga1", 1,
                                                       "../bin/pr_bin/Sobel/fpga_2.bin", "acla10_vfpga1", 2,
                                                       "../bin/pr_bin/Sobel/fpga_3.bin", "acla10_vfpga1", 3, 1.0);
  assert(Sobel_id >=0 && "Error: registerService failure\n");




  // create application objects
//  MMDApp app_MatMul = MMDApp(TaskQ_mgr, srv_reg, rsc_reg, "MatMul");
//  MMDApp app_VecAdd = MMDApp(TaskQ_mgr, srv_reg, rsc_reg, "VecAdd");
//  MMDApp app_Sobel = MMDApp(TaskQ_mgr, srv_reg, rsc_reg, "Sobel");
  MMDApp app_MatMul(TaskQ_mgr, srv_reg, rsc_reg, "MatMul", 2.0);
  MMDApp app_VecAdd(TaskQ_mgr, srv_reg, rsc_reg, "VecAdd", 1.0);
  MMDApp app_Sobel(TaskQ_mgr, srv_reg, rsc_reg, "Sobel", 1.0);
  int tot_services = 3;

  // service rank init
 // std::map<int, MMDApp*> rank_app_map;
 // rank_app_map.insert(std::pair<int, MMDApp*>(0, &app_Sobel));
 // rank_app_map.insert(std::pair<int, MMDApp*>(1, &app_VecAdd));
 // rank_app_map.insert(std::pair<int, MMDApp*>(2, &app_MatMul));

  // generate service request sequence
  // zipf_sequence return sequence of service's rank 
 // int* req_seq = zipf_sequence(tot_services, TOT_REQUEST);
 

  // We assume that one PRR per service is pre-configured
  // and the number of registered services is not changed during the simulation
  scaler.allocInitPRR(); // gurantee at least one PRR per service
  srv_reg.print();

  // Profiling
  // run each service for 5 times
  // the first application-run will pre-configure the region
  // during 5 time of application running, we profiled the avg. service throughput
  for( int i=0; i<5; i++){
    app_MatMul.run(-1);
    app_VecAdd.run(-1);
    app_Sobel.run(-1);
  }
  sleep(10); 

  // generate synthesized request patter for each servcie
  std::vector<std::thread> t_services;
  t_services.reserve(tot_services);

   struct timeval start, end;
   gettimeofday(& start, NULL);

  t_services.push_back(std::thread(&request_gen, &app_Sobel, arrv_rate_2));
  t_services.push_back(std::thread(&request_gen, &app_VecAdd, arrv_rate_0));
  t_services.push_back(std::thread(&request_gen, &app_MatMul, arrv_rate_1));

  for(auto &t: t_services){
    t.join();
  }

  gettimeofday(&end, NULL);
  float makespan= (end.tv_sec - start.tv_sec) + (end.tv_usec- start.tv_usec)/1000000.0;
  printf("END  %f\n", makespan);
  exit(0);
//  // generate synthesized request pattern
//  std::vector<std::thread> t_requests;
//  t_requests.reserve(TOT_REQUEST);

//  // for the poisson arrival 
//  std::default_random_engine generator;
//  generator.seed(time(0));
//  std::exponential_distribution<double> exp_dist(float(AVG_ARRIV_RATE));


//  for (int i=0; i<TOT_REQUEST; i++){
//    double interval = 1.0 / (float) AVG_ARRIV_RATE;
////    double interval = exp_dist(generator);
//    sleep(interval);
// 
//    t_requests.push_back(std::thread(&MMDApp::run, rank_app_map[req_seq[i]], i)); 
//  }  
//
//  for( auto &t : t_requests ){
//    t.join();
//  }


}

void request_gen(MMDApp* mmd_app, float avg_arriv_rate){

  char workload_file[40];
  sprintf(workload_file, "../input/%d_arrv.txt", mmd_app->getServiceID());

  float* arrv_seq = load_sequence(workload_file, TOT_REQUEST);

  std::vector<std::thread> t_req;
  t_req.reserve(TOT_REQUEST);
  

  //for the poisson arrival
  std::default_random_engine generator;
  generator.seed(time(0));
  std::exponential_distribution<double> exp_dist(avg_arriv_rate);

  for (int i=0; i<TOT_REQUEST; i++){

//  std::default_random_engine generator;
//  generator.seed(time(0));
//  std::exponential_distribution<double> exp_dist(arrv_seq[i]);


//   double interval = 1.0 / avg_arriv_rate;
   double interval = exp_dist(generator);
   usleep(interval*1000000);
   
//   usleep(inter_seq[i]*1000000*0.05);
   t_req.push_back(std::thread(&MMDApp::run, mmd_app, i));

  }
  
  for (auto &t : t_req){
    t.join();
  }

  return; 
}


int* zipf_sequence(int tot_services, int tot_requests){

  int* seq = new int[tot_requests];
  float* srv_popularity = new float[tot_services]; 
  
  // calculate number of requests of each services
  // accroding to zipf's distribution
  float denom = 0;
  for (int i=1; i<=tot_services; i++){
    denom += 1.0 / i;
  }
  for (int i=tot_services; i>0; i--){
    srv_popularity[tot_services-i] = (1.0 / (float) i) / denom; // rank N, rank N-1, ..., rank 1's # of requests
  }

  // generate requests sequence 
  for (int i=0; i<tot_requests; i++){
    float r = rand() / (float) RAND_MAX ; // {0~1}
    float cum =0;
    for (int j=0; j<tot_services; j++){
      cum += srv_popularity[j];
      if( r <= cum ){
        seq[i] =  (tot_services - j -1);
	break;
      }
    }  
  }

  return seq;
}


float* load_sequence(char* load_file_name, int tot_requests){


  std::fstream load_file;
  load_file.open(load_file_name, std::fstream::in);

  std::string line ;
  float service_id;
  float* seq_res = new float[tot_requests];

  for (int i=0; i<tot_requests; i++){
    getline(load_file, line);
    sscanf(line.c_str(), "%f", &service_id);
    seq_res[i] = service_id;
  }

  return seq_res;

}


 void irq_fn(int handle, void* user_data){
     printf("This is interrupt handler [handle: %d , user_data: %s]\n", handle, user_data);

 }
 
 void srq_fn(int handle, void* user_data, aocl_mmd_op_t op, int status){
   printf("This is status handler [handle: %d, user_data: %s]\n", handle, user_data);
 }


// aocl_mmd_interrupt_handler_fn irq_fn(int handle, void* user_data){
//     printf("This is interrupt handler %d , %s\n", handle, user_data);
// }
// 
// aocl_mmd_status_handler_fn srq_fn(int handle, void* user_data, aocl_mmd_op_t op, int status){
//   printf("This is status handler\n");
// }
