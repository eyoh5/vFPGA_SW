#include "mmdapp.h"
#include <thread>

int convert_to_cra_addr(int offset){
	return 4096+offset;
}

int coord(int iteration, int i){
  return iteration * 32 + i;
}; 


MMDApp::MMDApp(QueueManager& q_mgr, ServiceRegistry& srv_reg, ResourceRegistry& rsc_reg, char* name) : q_mgr(q_mgr), srv_reg(srv_reg), rsc_reg(rsc_reg), name(name){
  std::cout<<"MMDApp Init!\n";


}


MMDApp::~MMDApp(){
}

void MMDApp::run(int avg_arriv_rate, int tot_request){



  ////// Settings for request pattern
  std::default_random_engine generator;
  generator.seed(time(0));
  std::exponential_distribution<double> exp_dist(avg_arriv_rate);

	std::cout<<"Run MMDApp "<<name<<"\n";	

  ServiceInfo service_info;
	// Register the App
	if(strcmp(name, "MatMul") == 0)
			service_info = registerService(name, 1, "../bin/matrix_mult/fpga.bin", 0);
	else if(strcmp(name, "FFT2D") == 0)
			service_info = registerService(name, 1, "../bin/fft2d/fpga.bin", 0);
	else if(strcmp(name, "Sobel") == 0)
			service_info = registerService(name, 1, "../bin/sobel/fpga.bin", 0);
	else if(strcmp(name, "VecAdd") == 0)
			service_info = registerService(name, 1, "../bin/pr_bin/VecAdd/fpga.bin", 0);

  char logFileName[10];
  sprintf(logFileName, "%s.log", name); 
  std::cout<<logFileName<<"\n";
  ///// Generate log file
  std::fstream f;
  f.open(logFileName, std::fstream::out | std::fstream::app);
  f.setf(std::ios::left);
  f<<std::setw(15)<<"IDX"<<std::setw(15)<<"getHardware"<<std::setw(15)<<"getProgram"<<std::setw(15)<<"latency\n";

  float tot_latency = 0;
  float avg_latency;
		for(int i=0; i<tot_request; i++){
      ///// time values
      struct timeval start, end, start_lat, end_lat;
      float getHardware_overhead, getProgram_overhead, latency;

      double interval = exp_dist(generator);
      sleep(interval);

      gettimeofday(&start_lat, NULL);

			// get Hardware object to run the service 
			gettimeofday(&start, NULL);
			HardwareInfo hw_info = getHardware(service_info); // block
			gettimeofday(&end, NULL);
      getHardware_overhead = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec)/1000000.0;
			printf("MMDApp %d>> HardwareInfo was returned : dev_handle %d, part_id %d\n", id, hw_info.dev_handle, hw_info.part_id);

			/////// this function program the device with the accelerator of the service
			/////// and then return the newly generated device handle 
      gettimeofday(&start, NULL);
			hw_info = getProgram(hw_info, service_info);
      gettimeofday(&end, NULL);
      getProgram_overhead = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec)/1000000.0;

			// Run the kernel 
			if(strcmp(name, "MatMul") == 0)  runMatMul(hw_info);
			else if(strcmp(name, "FFT2D") == 0)  runFFT2D(hw_info); 
			else if(strcmp(name, "Sobel") == 0)  runSobel(hw_info);
			else if(strcmp(name, "VecAdd") == 0)  runVecAdd(hw_info); 

			releaseHardware(hw_info);

      gettimeofday(&end_lat,NULL);
      latency = (end_lat.tv_sec - start_lat.tv_sec) + (end_lat.tv_usec - start_lat.tv_usec)/1000000.0;
      tot_latency += latency;

      f.setf(std::ios::left);
      f<<std::setw(15)<<i<<std::setw(15)<<getHardware_overhead<<std::setw(15)<<getProgram_overhead<<std::setw(15)<<latency<<"\n";

		}

  avg_latency = tot_latency / (float) tot_request;

  f<<std::setw(15*4)<<std::setfill('=')<<"\n";
  f<<"Total "<<tot_request<<" are requested\n";
  f<<std::setw(15)<<"Average Latency : "<<avg_latency<<"\n";

  f.close();
}


ServiceInfo MMDApp::registerService(char* name, int num_bins, ...){

  va_list vl;
  va_start(vl, num_bins);
 
  ServiceInfo service_info;
  service_info.name = name;

  printf("MMDApp %d>> Register Service %s ...\n", id, name);

  for(int i=0; i<num_bins; i++){
   char* bin_file = va_arg(vl, char*);
    i++;
    int part_id = va_arg(vl, int);
    printf("MMDApp %d>> [%s] has bin file %s, for partition %d\n", id, name, bin_file, part_id);

   service_info.bin_partition_map.insert(std::pair<int, char*>(part_id, bin_file));
   
  }

  va_end(vl);
 
  // Add the service info to the service registry
  service_info = srv_reg.AddEntry(service_info);

  // update the application's ID 
  id = service_info.id;

  printf("MMDApp %d>> [%s] registered successfully\n", id, name);

  return service_info;
} 

HardwareInfo MMDApp::getHardware(ServiceInfo service_info){
  IQEntry request;
  request.serv_info = service_info;
  
  q_mgr.pushTaskQueue(request);

  
  // wait until the scheduler allocate proper hardware
  while(request.is_done == false) ;
 
  return request.hw_info;

} 


void MMDApp::releaseHardware(HardwareInfo hw_info){
  
  rsc_reg.release(hw_info.dev_handle, hw_info.part_id);

}

void MMDApp::runVecAdd(HardwareInfo hw_info){

  ////// Get Global Memory Interface
  int mem_if;
  int get_if_res;
  get_if_res = aocl_mmd_get_info(hw_info.dev_handle, AOCL_MMD_MEMORY_INTERFACE, sizeof(mem_if), &mem_if, NULL);
  std::cout<<"Get Memory Interface result: "<<get_if_res<<"\n";


  //////// Setting input arguments
  float* input_a = new float[100];
  float* input_b = new float[100];
  float* output = new float[100];

  for(int i=0; i<100; i++){
	input_a[i] = i;
	input_b[i] = i;
	output[i] = 0; 
 }

  /////// Copy from host
  int wr_res;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(float)*100, input_a, mem_if, 0);
  std::cout<<"copy from host input A result : " << wr_res <<"\n";
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(float)*100, input_b, mem_if, 400);
  std::cout<<"copy from hos input B result : " << wr_res <<"\n";

  // Read back the values to check
  float* rd_a = new float[100];
  wr_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float)*100, rd_a, mem_if, 0);
  for(int i=0; i<10; i++){ std::cout<<"a["<<i<<"] : "<<rd_a[i]<<"  ";}
  std::cout<<"...\n";
  for(int i=90; i<100; i++){ std::cout<<"a["<<i<<"] : "<<rd_a[i]<<"  ";} 
  std::cout<<"...\n";

  wr_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float)*100, rd_a, mem_if, 400);
  for(int i=0; i<10; i++){ std::cout<<"b["<<i<<"] : "<<rd_a[i]<<"  ";}
  std::cout<<"...\n";
  for(int i=90; i<100; i++){ std::cout<<"b["<<i<<"] : "<<rd_a[i]<<"  ";} 
  std::cout<<"...\n";

  /////// set arguments
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
  kernel_args[14] = 0; // input arg 0 addr low
  kernel_args[15] = 0; // input arg 0 addr high
  kernel_args[16] = 400; // intput arg 1 addr low
  kernel_args[17] = 0; // input arg 1 addr high
  kernel_args[18] = 800; // input arg 2 addr low
  kernel_args[19] = 0; // input arg 2 addr high

  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(kernel_args), &kernel_args, hw_info.kernel_if, convert_to_cra_addr(40), hw_info.part_id); 
  std::cout<<"Set args result : "<<wr_res<<"\n";

 
  ////// Run the kernel
  unsigned int start = 1;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(start), &start, hw_info.kernel_if, convert_to_cra_addr(0), hw_info.part_id); // block function
  std::cout<<"Run kernel result: "<<wr_res<<"\n";

  ////// Write the kernel stop
  unsigned int stop = 0;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(stop), &stop, hw_info.kernel_if, convert_to_cra_addr(0), hw_info.part_id);
  std::cout<<"Write stop result: "<<wr_res<<"\n";

  ///// read the status register
  unsigned int stat ;
  int rd_res;
  rd_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(stat), &stat, hw_info.kernel_if, convert_to_cra_addr(0), hw_info.part_id);
  std::cout<<"Read result : "<< rd_res <<"\n";
  std::cout<<"status signal is : " << std::hex << stat <<"\n";


  ///// Copy from Device
  rd_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float)*100, output, mem_if, 800);
  std::cout<<"Copy from Device result: "<< rd_res <<"\n"; 

  ///// Check the kernel output
  for (int i=0; i<10; i++){
	printf("output[%d] : %f", i, output[i]);
  }
  printf("\n");

}

void MMDApp::runFFT2D(HardwareInfo hw_info){

  ////// Get Global Memory Interface
  int mem_if;
  int get_if_res;
  get_if_res = aocl_mmd_get_info(hw_info.dev_handle, AOCL_MMD_MEMORY_INTERFACE, sizeof(mem_if), &mem_if, NULL);
  std::cout<<"Get Memory Interface result: "<<get_if_res<<"\n";

  ////// Helper function
   typedef struct {
    float x;
    float y;
  } float2;

  int LOGN = 10;
  int N = (1 << LOGN);

  /////// Setting input argumnents
  float2 *h_inData = new float2[N * N];
  float2* h_outData = new float2[N * N];
//  float2* h_tmp = new float2[N*N];

  for(int i=0; i<N; i++){
    for(int j=0; j<N; j++){
      int where = coord(i, j);
      h_inData[where].x = (float)(i);
      h_inData[where].y = (float)(j);
    }
  }
    
  ////// Copy from host
  int wr_res;
  int data_offset = 0;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(float2)*N*N, h_inData, mem_if, data_offset);
  std::cout<<"copy from host h_in result: "<<wr_res<<"\n";

  ////// Read back the value to check
  float2* rd_in = new float2[N* N];
  int rd_res;
  rd_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float2)*N*N, rd_in, mem_if, 0);
  std::cout<<"read back result : "<<rd_res<<"\n";
  for(int i=0; i<10; i++){
    std::cout<<"rd_in["<<i<<"] : "<<rd_in[i].x <<", "<<rd_in[i].y<<" ";
  }
  std::cout<<"...\n";
  for (int i=N*N - 10; i<N*N; i++){
    std::cout<<"rd_in["<<i<<"] : "<<rd_in[i].x<<", "<<rd_in[i].y<<" ";
  }
  std::cout<<"\n";

  ////// Define device pointer for tmp data
//  unsigned int dev_ptr_tmp_low = 16842752;
  unsigned int dev_ptr_tmp_low = 8*N*N*2;
  unsigned int dev_ptr_tmp_high = 0;

  for(int i=0; i<2; i++){

  /////// FETCH KERNEL
  /////// set arguments
  unsigned int kernel_arg_fetch[17];
  kernel_arg_fetch[0] = 1;
  kernel_arg_fetch[1] = N;
  kernel_arg_fetch[2] = N*N / 8;
  kernel_arg_fetch[3] = 1;
  kernel_arg_fetch[4] = 1;
  kernel_arg_fetch[5] = N/8;
  kernel_arg_fetch[6] = 1;
  kernel_arg_fetch[7] = 1;
  kernel_arg_fetch[8] = N;
  kernel_arg_fetch[9] = 1;
  kernel_arg_fetch[10] = 1;
  kernel_arg_fetch[11] = 0;
  kernel_arg_fetch[12] = 0;
  kernel_arg_fetch[13] = 0;
  kernel_arg_fetch[14] = (i==0 ? 0 : dev_ptr_tmp_low); // h_in low or tmp low 
  kernel_arg_fetch[15] = (i==0 ? 0 : dev_ptr_tmp_high); // h_in high or tmp high
  kernel_arg_fetch[16] = 0; // mangle int 

  ////// write kernel arguments 
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(kernel_arg_fetch), kernel_arg_fetch, hw_info.kernel_if, convert_to_cra_addr(40));
  std::cout<<"kernel arg write result : "<<wr_res<<"\n";

  ///// Run Kernel 
  unsigned int start = 1;   
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(start), &start, hw_info.kernel_if, convert_to_cra_addr(0));
 std::cout<<"run kernel result : "<< wr_res <<"\n";

   ////// Write the kernel stop
  unsigned int stop = 0;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(stop), &stop, hw_info.kernel_if, convert_to_cra_addr(0));
  std::cout<<"Write stop result: "<<wr_res<<"\n";

 
  //// FFT2D KERNEL
   /////// set arguments
  unsigned int kernel_arg[15];
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
  kernel_arg[14] = 0; // inverse_int 

  ////// write kernel arguments 
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(kernel_arg), kernel_arg, hw_info.kernel_if, convert_to_cra_addr(40 + 128)); // each kernel interace cra interval is 128(dec)
  std::cout<<"kernel arg write result : "<<wr_res<<"\n";

  ///// Run Kernel 
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(start), &start, hw_info.kernel_if, convert_to_cra_addr(0 + 128));
 std::cout<<"run kernel result : "<< wr_res <<"\n";

   ////// Write the kernel stop
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(stop), &stop, hw_info.kernel_if, convert_to_cra_addr(0));
  std::cout<<"Write stop result: "<<wr_res<<"\n";


 //////// TRANSPOSE KERNEL
  /////// set arguments
   unsigned int kernel_arg_tp[17];
   kernel_arg_tp[0] = 1;
   kernel_arg_tp[1] = N;
   kernel_arg_tp[2] = N*N / 8 ;
   kernel_arg_tp[3] = 1;
   kernel_arg_tp[4] = 1;
   kernel_arg_tp[5] = N/8;
   kernel_arg_tp[6] = 1;
   kernel_arg_tp[7] = 1;
   kernel_arg_tp[8] = N;
   kernel_arg_tp[9] = 1;
   kernel_arg_tp[10] = 1;
   kernel_arg_tp[11] = 0;
   kernel_arg_tp[12] = 0;
   kernel_arg_tp[13] = 0;
   kernel_arg_tp[14] = (i==0 ? dev_ptr_tmp_low : 8*N*N); 
   kernel_arg_tp[15] = (i==0 ? dev_ptr_tmp_high : 0);
   kernel_arg_tp[16] = 0; //mangle int

   std::cout<<"** output ptr :"<< kernel_arg_tp[14] <<","<<kernel_arg_tp[15]<<"\n";

   ////// write kernel arguments 
   wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(kernel_arg_tp), kernel_arg_tp, hw_info.kernel_if, convert_to_cra_addr(40 + 128*2));
   std::cout<<"kernel arg write result : "<<wr_res<<"\n";

//   ///// Run Kernel 
//   wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(start), &start, hw_info.kernel_if, convert_to_cra_addr(0 + 128*2));
//  std::cout<<"run kernel result : "<< wr_res <<"\n";

  }

  /////// copy from device
  rd_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float2)*N*N, h_outData, mem_if, 8*N*N);
  std::cout<<"copy from device result : "<<rd_res<<"\n";


  ////// check output 
  for(int i=0; i<10; i++){
    std::cout<<"h_out["<<i<<"] : "<<h_outData[i].x <<", " <<h_outData[i].y;
  }
  std::cout<<"...\n";
  for(int i=N*N -1; i<N*N; i++){
    std::cout<<"h_out[" << i << "] : " <<h_outData[i].x <<", " <<h_outData[i].y;
  }
  std::cout<<"\n";

}

void MMDApp::runSobel(HardwareInfo hw_info){

  int ROWS = 432;
  int COLS = 768;

  ////// Get global memory interface
  int mem_if;
  int get_if_res;
  get_if_res = aocl_mmd_get_info(hw_info.dev_handle, AOCL_MMD_MEMORY_INTERFACE, sizeof(mem_if), &mem_if, NULL);
  std::cout<<"Get Memory Interface result: "<<get_if_res<<"\n";
  
  ////// set input argument
  unsigned int* input = new unsigned int[ROWS * COLS];
  unsigned int* output = new unsigned int[ROWS * COLS];
 
  char* imageFileName = "../bin/sample_image.ppm";
  if(!parse_ppm(imageFileName, COLS, ROWS, (unsigned char*)input)){
    std::cout<<"Cannot load input image !!\n";
    return;
  }

  ///// copy from host 
  int wr_res;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(unsigned int)*ROWS*COLS, input, mem_if, 0);
  std::cout<<"copy from host result : "<<wr_res<<"\n"; 

  ///// set kernel argument
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
  kernel_arg[14] = 0; //intput ptr low 
  kernel_arg[15] = 0; //input ptr high
  kernel_arg[16] = sizeof(unsigned int)*ROWS*COLS; // output ptr low
  kernel_arg[17] = 0; //output ptr high;
  kernel_arg[18] = ROWS*COLS;
  kernel_arg[19] = 128;

  /// write kernel arguments
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(kernel_arg), kernel_arg, hw_info.kernel_if, convert_to_cra_addr(40));
  std::cout<<"write kernel arg result : "<< wr_res <<"\n";

  /// Run kernel
  unsigned int start = 1;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(start), &start, hw_info.kernel_if, convert_to_cra_addr(0));
  std::cout<<"run kernel result :"<<wr_res<<"\n";

  ////// Write the kernel stop
  unsigned int stop = 0;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(stop), &stop, hw_info.kernel_if, convert_to_cra_addr(0));
  std::cout<<"Write stop result: "<<wr_res<<"\n";


  //// copy from device
  int rd_res;
  rd_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(output), output, mem_if, sizeof(unsigned int)*ROWS*COLS);
  std::cout<<"copy from device result : "<<rd_res<<"\n";

  for(int i=0; i<10; i++){
    std::cout<<"output[" <<i<< "]: "<<output[i]<<" ";
  }
  std::cout<<"\n";

}

void MMDApp::runMatMul(HardwareInfo hw_info){


  ////// Get Global Memory Interface
  int mem_if;
  int get_if_res;
  get_if_res = aocl_mmd_get_info(hw_info.dev_handle, AOCL_MMD_MEMORY_INTERFACE, sizeof(mem_if), &mem_if, NULL);
  std::cout<<"Get Memory Interface result: "<<get_if_res<<"\n";


  //////// Setting input arguments
  float* input_a = new float[100];
  float* input_b = new float[100];
  float* output = new float[100];

  for(int i=0; i<100; i++){
	input_a[i] = i;
	input_b[i] = i;
	output[i] = 0; 
 }

  /////// Copy from host
  int wr_res;
  int data_offset = 0;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(float)*100, input_a, mem_if, data_offset);
  std::cout<<"copy from host input A result : " << wr_res <<"\n";
  data_offset += 400;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(float)*100, input_b, mem_if, data_offset);
  std::cout<<"copy from hos input B result : " << wr_res <<"\n";

  // Read back the values to check
  float* rd_a = new float[100];
  wr_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float)*100, rd_a, mem_if, 0);
  for(int i=0; i<10; i++){ std::cout<<"a["<<i<<"] : "<<rd_a[i]<<"  ";}
  std::cout<<"...\n";
  for(int i=90; i<100; i++){ std::cout<<"a["<<i<<"] : "<<rd_a[i]<<"  ";} 
  std::cout<<"...\n";

  wr_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float)*100, rd_a, mem_if, 400);
  for(int i=0; i<10; i++){ std::cout<<"b["<<i<<"] : "<<rd_a[i]<<"  ";}
  std::cout<<"...\n";
  for(int i=90; i<100; i++){ std::cout<<"b["<<i<<"] : "<<rd_a[i]<<"  ";} 
  std::cout<<"...\n";

  /////// set arguments
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
  kernel_args[14] = 0; // input arg 0 addr low
  kernel_args[15] = 0; // input arg 0 addr high
  kernel_args[16] = 400; // intput arg 1 addr low
  kernel_args[17] = 0; // input arg 1 addr high
  kernel_args[18] = 800; // input arg 2 addr low
  kernel_args[19] = 0; // input arg 2 addr high

  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(kernel_args), &kernel_args, hw_info.kernel_if, convert_to_cra_addr(40)); 
  std::cout<<"Set args result : "<<wr_res<<"\n";

  ////// Run the kernel
  unsigned int start = 1;
  wr_res = aocl_mmd_write(hw_info.dev_handle, NULL, sizeof(start), &start, hw_info.kernel_if, convert_to_cra_addr(0)); // block function
  std::cout<<"Run kernel result: "<<wr_res<<"\n";



  ///// read the status register
  unsigned int stat ;
  int rd_res;
  rd_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(stat), &stat, hw_info.kernel_if, convert_to_cra_addr(0));
  std::cout<<"Read result : "<< rd_res <<"\n";
  std::cout<<"status signal is : " << std::hex << stat <<"\n";


  ///// Copy from Device
  rd_res = aocl_mmd_read(hw_info.dev_handle, NULL, sizeof(float)*100, output, mem_if, 800);
  std::cout<<"Copy from Device result: "<< rd_res <<"\n"; 

  ///// Check the kernel output
  for (int i=0; i<10; i++){
	printf("output[%d] : %f", i, output[i]);
  }
  printf("\n");


}

 HardwareInfo  MMDApp::getProgram(HardwareInfo hw_info, ServiceInfo srv_info){

  HardwareInfo res;  

  ////// Check whether the partition already programmed with the service or not
  if(!rsc_reg.is_programmed_with(hw_info.dev_handle, hw_info.part_id, srv_info.id)){

    ///// Create New Entry
    IQEntry pr_request;
    pr_request.hw_info = hw_info;
    pr_request.serv_info = srv_info;
  
    q_mgr.pushPRQueue(pr_request);

    // wait until the PR is done 
    while(pr_request.is_done == false) ;

   ////// Update the new device handle and the prgorammed status to the resource registry
   rsc_reg.reprogram((pr_request.hw_info).dev_handle, (pr_request.hw_info).part_id, hw_info.part_id, id); 

   /////// Set the interrupt handler 
   int irq_res;
   char* usr_data = "hello world";
   irq_res = aocl_mmd_set_interrupt_handler((pr_request.hw_info).dev_handle, irq_fn((pr_request.hw_info).dev_handle, usr_data), usr_data );
   std::cout<<"Set Interrupt Handler result : "<< irq_res<<"\n";
 
 
   /////// Set the status handler
   int srq_res;
   int status;
   srq_res = aocl_mmd_set_status_handler((pr_request.hw_info).dev_handle, srq_fn((pr_request.hw_info).dev_handle, usr_data, NULL,status ), usr_data);
   std::cout <<"Set status Handler result : "<<srq_res<<"\n";

   ///// set the result 
   res.dev_handle = (pr_request.hw_info).dev_handle;
   res.part_id = (pr_request.hw_info).part_id;

  }

  /////// Get Kernel Interface
  int kernel_if;
  int get_if_res;
  get_if_res = aocl_mmd_get_info(res.dev_handle, AOCL_MMD_KERNEL_INTERFACES, sizeof(kernel_if), &kernel_if, NULL);
  std::cout<<"Get Kernel Interface result : "<<get_if_res<<"\n";


  if(get_if_res < 0){
	std::cout <<"Error getting kernel interface info" << std::endl;
  }

  res.kernel_if = kernel_if;

  return res;
}


