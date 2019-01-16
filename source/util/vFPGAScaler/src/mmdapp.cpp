#include "mmdapp.h"

int convert_to_cra_addr(int offset){
  return 4096 + offset;
}

MMDApp::MMDApp(TaskQueueManager& TaskQ_mgr, ServiceRegistry& srv_reg, ResourceRegistry& rsc_reg, char* name, float latency_threshold) : TaskQ_mgr(TaskQ_mgr), srv_reg(srv_reg), rsc_reg(rsc_reg), name(name), latency_threshold(latency_threshold){

//  sprintf(logFileName, "../log/app_%s.log", name);

}

MMDApp::~MMDApp(){

}

void MMDApp::run(int job_id){

  /*
  *  Variables for the log file
  */
  struct timeval start, end; 
  struct timeval start_getdev, end_getdev, start_getprg, end_getprg, start_runprg, end_runprg;
  float getDevice_overhead = 0;
  float getProgram_overhead = 0;
  float runProgram_overhead = 0;
  float total_runtime=0;
  int violation = 0;


  gettimeofday(&start, NULL);

  /*
  *  Get service information to run
  */
  service_id = srv_reg.getServiceID(name);

  /*
  *  Acquire device access
  */
  gettimeofday(&start_getdev, NULL);
  __Device __device = getDevice(service_id);
  gettimeofday(&end_getdev, NULL);
  getDevice_overhead = (end_getdev.tv_sec - start_getdev.tv_sec) + (end_getdev.tv_usec - start_getdev.tv_usec)/1000000.0;



  /*
  *  Configure the device with the service
  */
  printf("MMDApp>> request getProgram\n");
  gettimeofday(&start_getprg, NULL);
  __device = getProgram(__device, service_id);
  gettimeofday(&end_getprg, NULL);
  getProgram_overhead = (end_getprg.tv_sec - start_getprg.tv_sec) + (end_getprg.tv_usec - start_getprg.tv_usec)/1000000.0;


  // User API is not fully implemented yet
  gettimeofday(&start_runprg, NULL);
  if(strcmp(name, "VecAdd") == 0) runVecAdd(__device);
  else if(strcmp(name, "Sobel") == 0) runSobel(__device);
  else if(strcmp(name, "MatMul") == 0) runMatMul(__device);
  gettimeofday(&end_runprg, NULL);
  runProgram_overhead = (end_runprg.tv_sec - start_runprg.tv_sec) + (end_runprg.tv_usec - start_runprg.tv_usec)/1000000.0;


  /*
  *  Release the device 
  */ 
  printf("MMDApp>> request releaseDevice\n");
  releaseDevice(__device);


  gettimeofday(&end, NULL);
  gettimeofday(&end, NULL);
  total_runtime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec)/1000000.0;
  if(total_runtime > latency_threshold ) violation =1;


  /*
  *  Print out the avg. latency to the log file
  */
  char logFileName[20] ;
  sprintf(logFileName, "../log/app_%s.log", name);
  std::fstream logFile; 
  logFile.open(logFileName, std::fstream::out | std::fstream::app);
  if( logfile_first_access == 1 ) {
    logFile.setf(std::ios::left);
    logFile<<std::setw(15)<<"JobID"<<std::setw(15)<<"Runtime"<<std::setw(15)<<"getDevice"<<std::setw(15)<<"getProgram"<<std::setw(15)<<"runProgram"<<std::setw(15)<<"startTime_s"<<std::setw(15)<<"startTime_us"<<std::setw(15)<<"endTime_s"<<std::setw(15)<<"endTime_us"<<std::setw(15)<<"violation"<<violation<<std::endl;
    logfile_first_access = 0;
  }

  logFile.setf(std::ios::left);
  logFile<<std::setw(15)<<job_id<<std::setw(15)<<total_runtime<<std::setw(15)<<getDevice_overhead<<std::setw(15)<<getProgram_overhead<<std::setw(15)<<runProgram_overhead<<std::setw(15)<<start.tv_sec<<std::setw(15)<<start.tv_usec<<std::setw(15)<<end.tv_sec<<std::setw(15)<<end.tv_usec<<std::setw(15)<<violation<<std::endl;
  logFile.close();


  /*
  *  Profiling Status
  */
  if(job_id == -1){
    srv_reg.setServiceTimeWOInterference(service_id, total_runtime);
  }


}



__Device MMDApp::getDevice(int service_id){
 
  /*
  *  Variables for the log file
  */
  float task_q_lat, prr_q_lat;
  char logFileName[25] = "../log/getDevice.log";
  std::fstream logFile;

  /*
  *  Generate new request
  */  
  QEntry req; // QEntry includes request and response 
  req.service_id = service_id;
  req.type = GETDEV;

  TaskQ_mgr.push(&req); 

  /*
  * Wait until the GETDEV request is done
  */
  while( req.is_done == false ) ;
 
  /*
   * Generate result device object
  */
  __Device res;
  // set device id
  res.dev_id = rsc_reg.getDeviceID(req.glob_prr_id);
  // set device handle
  res.dev_handle = rsc_reg.getDeviceHandle(res.dev_id);
  // set glob_prr_id
  res.glob_prr_id = req.glob_prr_id;
  // set kernel interface
  int kernel_if ;
  aocl_mmd_get_info(rsc_reg.getDeviceHandle(rsc_reg.getDeviceID(res.glob_prr_id)), AOCL_MMD_KERNEL_INTERFACES, sizeof(kernel_if), &kernel_if, NULL);
  res.kernel_if = kernel_if;
  //set memory interface
  int mem_if;
  aocl_mmd_get_info(rsc_reg.getDeviceHandle(rsc_reg.getDeviceID(res.glob_prr_id)), AOCL_MMD_MEMORY_INTERFACE, sizeof(mem_if), &mem_if, NULL);
  res.mem_if = mem_if ;
 

  /*
  *  Print out each q latency to the log file
  */
  logFile.open(logFileName, std::fstream::out | std::fstream::app);
  logFile.setf(std::ios::left);

  if(logfile_first_access2 == 1){
    logFile<<std::setw(15)<<"TaskQLat."<<std::setw(15)<<"PRRQLat."<<std::setw(15)<<"TaskQLen"<<std::endl;
    logfile_first_access2 = 0;
  }
 
  task_q_lat = (req.deque_taskQ_point.tv_sec - req.enque_taskQ_point.tv_sec) + (req.deque_taskQ_point.tv_usec - req.enque_taskQ_point.tv_usec)/1000000.0;
  prr_q_lat = (req.deque_PRRQ_point.tv_sec - req.enque_PRRQ_point.tv_sec) + (req.deque_PRRQ_point.tv_usec - req.enque_PRRQ_point.tv_usec)/1000000.0;
  logFile<<std::setw(15)<<task_q_lat<<std::setw(15)<<prr_q_lat<<std::setw(15)<<req.task_q_len<<std::endl;

  logFile.close();

  return res;

}

__Device MMDApp::getProgram(__Device device, int service_id){

  /*
  *  Variables for log file
  */
  float task_q_lat, prr_q_lat;
  char logFileName[25] = "../log/getProgram.log";
  std::fstream logFile;


  printf("MMDApp>> Reprogram the PRR#%d with service#%d\n", device.glob_prr_id, service_id);
 
  /*
  *  Generate new request
  */
  QEntry req;
  req.service_id = service_id;
  req.glob_prr_id = device.glob_prr_id;
  req.type = GETPRG;
 
  TaskQ_mgr.push(&req);
  
  // wait until the PR is done
  while ( req.is_done == false) ;

  /*
  *  Print out each q latency to the log file
  */
  logFile.open(logFileName, std::fstream::out | std::fstream::app);
  logFile.setf(std::ios::left);

  if(logfile_first_access3 == 1){
    logFile<<std::setw(15)<<"TaskQLat."<<std::setw(15)<<"PRRQLat."<<std::setw(15)<<"TaksQlen"<<std::endl;
    logfile_first_access3 = 0;
  }
 
  task_q_lat = (req.deque_taskQ_point.tv_sec - req.enque_taskQ_point.tv_sec) + (req.deque_taskQ_point.tv_usec - req.enque_taskQ_point.tv_usec)/1000000.0;
  prr_q_lat = (req.deque_PRRQ_point.tv_sec - req.enque_PRRQ_point.tv_sec) + (req.deque_PRRQ_point.tv_usec - req.enque_PRRQ_point.tv_usec)/1000000.0;
  logFile<<std::setw(15)<<task_q_lat<<std::setw(15)<<prr_q_lat<<std::setw(15)<<req.task_q_len<<std::endl;

  logFile.close();

  return device;

}

void MMDApp::runVecAdd(__Device device){

  // set status of the device as RUNNING
//  rsc_reg.setPRRStatus(device.glob_prr_id, RUNNING);

#ifndef USE_DMA
  // set intput arguments
  float* input_a = new float[100];
  float* input_b = new float[100];
  float* output = new float[100];
  // device memory 
  unsigned int input_a_addr = 0;
  unsigned int input_b_addr = 400;
  unsigned int output_addr = 800;
#else
  // aligned_alloc for dma 
  float* input_a = (float*) aligned_alloc(64, 4*100);
  float* input_b = (float*) aligned_alloc(64, 4*100);
  float* output = (float*) aligned_alloc(64, 4*100);
  // aligned device memory for dma
  unsigned int input_a_addr = 0;
  unsigned int input_b_addr = 448;
  unsigned int output_addr = 896;
#endif

  for(int i=0; i<100; i++){
    input_a[i] = i;
    input_b[i] = i;
    output[i] = 0;
  }

  // copy from host 
  int wr_res;
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(float)*100, input_a, device.mem_if, input_a_addr);
  printf("copy from host input A result : %d\n", wr_res);
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(float)*100, input_b, device.mem_if, input_b_addr);
 printf("copy from host input B result : %d\n",  wr_res);
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(float)*100, output, device.mem_if, output_addr);
  printf("copy from host output result : %d\n", wr_res);

  // set kernel arguments
  unsigned int kernel_args[20];
  kernel_args[0] = 1; //work dimension
  kernel_args[1] = 1000000; // workgroup size
  kernel_args[2] = 1; // global size 0
  kernel_args[3] = 1; // global size 1
  kernel_args[4] = 1; // global size 2
  kernel_args[5] = 1; // num group 0
  kernel_args[6] = 1; // num group 1
  kernel_args[7] = 1; // num group 2
  kernel_args[8] = 1000000; // local size 0
  kernel_args[9] = 1; // local size 1
  kernel_args[10] = 1; // local size 2
  kernel_args[11] = 0; // global offset 0
  kernel_args[12] = 0; // global offset 1
  kernel_args[13] = 0; // global offset 2
  kernel_args[14] = input_a_addr; // input arg 0 addr low
  kernel_args[15] = 0; // input arg 0 addr high
  kernel_args[16] = input_b_addr; // intput arg 1 addr low
  kernel_args[17] = 0; // input arg 1 addr high
  kernel_args[18] = output_addr; // input arg 2 addr low
  kernel_args[19] = 0; // input arg 2 addr high

  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(kernel_args), &kernel_args, device.kernel_if, convert_to_cra_addr(40), rsc_reg.getLocPRRID(device.glob_prr_id));
  printf("Set args result : %d\n", wr_res);


  // run the kernel
  unsigned int start = 1;
  for(int i=0; i<100; i++){
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(start), &start, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id)); // block function
  }

  // write the kernel stop
//  unsigned int stop = 0;
//  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(stop), &stop, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//  printf("Write stop result: %d\n", wr_res);

  // read the status register
//  unsigned int stat ;
//  while(1) {
//    int rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(stat), &stat, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//    if( (stat & 0x1000) == 0) break;
//
// }
//  printf("status signal is : %x\n", stat);


  // copy from Device
  int rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(float)*100, output, device.mem_if, output_addr);
  printf("Copy from Device result: %d\n", rd_res);

  // check the kernel output
  for (int i=0; i<10; i++){
        printf("VecAddoutput[%d] : %f", i, output[i]);
  }
  printf("\n");


//  // set the status of the partition as READY
//  rsc_reg.setPRRStatus(device.glob_prr_id, READY);

  free(input_a);
  free(input_b);
  free(output);

}

void MMDApp::runSobel(__Device device){

  // set status of the device as RUNNING
//  rsc_reg.setPRRStatus(device.glob_prr_id, RUNNING);

  // set input arguments
  int ROWS = 432;
  int COLS = 768;

#ifndef USE_DMA
  unsigned int* input = new unsigned int[ROWS * COLS];
  unsigned int* output = new unsigned int [ROWS * COLS];
  // device memory
  unsigned int input_addr = 1200; 
  unsigned int output_addr = 1200 + ROWS*COLS;
#else
  unsigned int* input = (unsigned int*) aligned_alloc(64, 4*ROWS*COLS);
  unsigned int* output = (unsigned int*) aligned_alloc(64, 4*ROWS*COLS);
  // aligned device memory
  unsigned int input_addr = 1216;
  unsigned int output_addr = 1328320; 
#endif 

  for(int i=0; i<ROWS*COLS; i++){
    output[i] = 0;
  }

  char* imageFileName = "../bin/sample_image.ppm";
  if(!parse_ppm(imageFileName, COLS, ROWS, (unsigned char*)input)){
    printf("Cannot load input image !!\n");
    return;
  }

  // copy from host
  int wr_res;
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(unsigned int)*ROWS*COLS, input, device.mem_if, input_addr);
  printf("copy from host result : %d\n", wr_res);

  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(unsigned int)*ROWS*COLS, output, device.mem_if, output_addr);
  printf("copy from host result : %d\n", wr_res);

  // set kernel argument
  unsigned int kernel_arg[20];
  kernel_arg[0] = 1;
  kernel_arg[1] = 1;
  kernel_arg[2] = 1;
  kernel_arg[3] = 1;
  kernel_arg[4] = 1;
  kernel_arg[5] = 1;
  kernel_arg[6] = 1;
  kernel_arg[7] = 1;
  kernel_arg[8] = 1;
  kernel_arg[9] = 1;
  kernel_arg[10] = 1;
  kernel_arg[11] = 0;
  kernel_arg[12] = 0;
  kernel_arg[13] = 0;
  kernel_arg[14] = input_addr; //intput ptr low
  kernel_arg[15] = 0; //input ptr high
  kernel_arg[16] = output_addr; // output ptr low
  kernel_arg[17] = 0; //output ptr high;
  kernel_arg[18] = 331776; // ROWS*COLS
  kernel_arg[19] = 128; // threshold

  /// write kernel arguments
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(kernel_arg), kernel_arg, device.kernel_if, convert_to_cra_addr(40), rsc_reg.getLocPRRID(device.glob_prr_id));
  printf("write kernel arg result : %d\n", wr_res);

  /// Run kernel
  unsigned int start = 1;
  for(int i=0; i<100; i++){
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(start), &start, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
  }

//  ////// Write the kernel stop
//  unsigned int stop = 0;
//  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(stop), &stop, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//  printf("Write stop result: %d\n", wr_res);

  // read the status register
//  unsigned int stat ;
//  while(1) {
//    int rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(stat), &stat, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//    if( (stat & 0x1000) == 0) break;
//
// }
//  printf("status signal is : %x\n", stat);


//  unsigned int stat ;
//  while(1) {
//    int rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(stat), &stat, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//    if(stat == 0x3b00) break;
//  }
//  printf("status signal is : %x\n", stat);


  //// copy from device
  int rd_res;
  rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(output), output, device.mem_if, output_addr);
  printf("copy from device result : %d\n", rd_res);

  for(int i=0; i<10; i++){
    printf("Sobeloutput[%d]: %f",i, output[i]);
  }
  printf("\n");


  free(input);
  free(output);


}

void MMDApp::runMatMul(__Device device){

  // set status of the device as RUNNING
//  rsc_reg.setPRRStatus(device.glob_prr_id, RUNNING);

#ifndef USE_DMA
  // set input arguments
  float* input_a = new float[4096];
  float* input_b = new float[4096];
  float* output = new float[4096];
  float* rd_a = new float[4096]; // read back value
  // device memory
  unsigned int input_a_addr = 2655408;
  unsigned int input_b_addr = 2671792;
  unsigned int output_addr = 2688176;
#else
  float* input_a = (float*) aligned_alloc(64, 4*4096);
  float* input_b = (float*) aligned_alloc(64, 4*4096);
  float* output = (float*) aligned_alloc(64, 4*4096);
  float* rd_a = (float*) aligned_alloc(64, 4*4096);
  // aligned device memory
  unsigned int input_a_addr = 2655424;
  unsigned int input_b_addr = 2671808;
  unsigned int output_addr = 2688192;
#endif

  float j=0.0;
  for(int i=0; i<4096; i++){
  if(i%100 == 0) j=0.0;
  input_a[i] = j;
  input_b[i] = j;
  output[i] = 0;
  j+=1.0;
  }

  int input_a_width = 64; // makes 64 x 64 matrix
  int input_b_width = 64; // makes 64 x 64 matrix

  // copy from host
  int wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(float)*4096, input_a, device.mem_if, input_a_addr);
  printf("copy from host input A result : %d\n", wr_res);

  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(float)*4096, input_b, device.mem_if, input_b_addr);
  printf("copy from hos input B result : %d\n", wr_res);

  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(float)*4096, output, device.mem_if, output_addr);
  printf("copy from host output result : %d\n", wr_res);


  // read back the values to check
  wr_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(float)*4096, rd_a, device.mem_if, input_a_addr);
  for(int i=0; i<10; i++){ printf("a[%d] : %f  ", i, rd_a[i]);}
  printf("...\n");
  for(int i=4086; i<4096; i++){ printf("a[%d] : %f   ", i, rd_a[i]);}
  printf("...\n");

  wr_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(float)*4096, rd_a, device.mem_if, input_b_addr);
  for(int i=0; i<10; i++){ printf("b[%d] : %f  ", i, rd_a[i]);}
  printf("...\n");
  for(int i=4086; i<4096; i++){ printf("b[%d] : %f  ", i, rd_a[i]);}
  printf("...\n");
 
  // set arguments
  unsigned int kernel_args[22];
  kernel_args[0] = 2; //work dimension
  kernel_args[1] = 32; // workgroup size
  kernel_args[2] = 32; // global size 0
  kernel_args[3] = 64; // global size 1
  kernel_args[4] = 1; // global size 2
  kernel_args[5] = 8; // num group 0
  kernel_args[6] = 8; // num group 1
  kernel_args[7] = 1; // num group 2
  kernel_args[8] = 4; // local size 0
  kernel_args[9] = 8; // local size 1
  kernel_args[10] = 1; // local size 2
  kernel_args[11] = 0; // global offset 0
  kernel_args[12] = 0; // global offset 1
  kernel_args[13] = 0; // global offset 2
  kernel_args[14] = output_addr; // input arg 0 addr low
  kernel_args[15] = 0; // input arg 0 addr high
  kernel_args[16] = input_a_addr; // intput arg 1 addr low
  kernel_args[17] = 0; // input arg 1 addr high
  kernel_args[18] = input_b_addr; // input arg 2 addr low
  kernel_args[19] = 0; // input arg 2 addr high
  kernel_args[20] = 64;
  kernel_args[21] = 64;

  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(kernel_args), &kernel_args, device.kernel_if, convert_to_cra_addr(40), rsc_reg.getLocPRRID(device.glob_prr_id));
  printf("Set args result : %d\n", wr_res);

  // run the kernel
  unsigned int start = 1;
  for (int i=0; i<100;i++){
  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(start), &start, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id)); // block function
  }

//  // write the kernel stop
//  unsigned int stop = 0;
//  wr_res = aocl_mmd_write(device.dev_handle, NULL, sizeof(stop), &stop, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//  printf("Write stop result: %d\n", wr_res);

  // read the status register
//  unsigned int stat ;
//  while(1) {
//    int rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(stat), &stat, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//    if( (stat & 0x1000) == 0) break;
//
// }
//  printf("status signal is : %x\n", stat);

//  // read the status siganl
//  unsigned int stat ;
////  while(1) {
//    int rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(stat), &stat, device.kernel_if, convert_to_cra_addr(0), rsc_reg.getLocPRRID(device.glob_prr_id));
//    //if(stat == 0x3b00) break;
//    printf(".");
////  }
//  printf("status signal is : %x\n", stat);


  // copy from Device
  int rd_res = aocl_mmd_read(device.dev_handle, NULL, sizeof(float)*4096, output, device.mem_if, output_addr);
  printf("Copy from Device result: %d\n", rd_res);

  // check the kernel output
  for (int i=0; i<10; i++){
  printf("MatMuloutput[%d] : %f", i, output[i]);
  }
  printf("\n");


  free(input_a);
  free(input_b);
  free(output);
  free(rd_a);


}

void MMDApp::releaseDevice(__Device device){

  // set status of the device as READY
  rsc_reg.setPRRStatus(device.glob_prr_id, READY);


}

int MMDApp::getServiceID(){
  return service_id;
}
