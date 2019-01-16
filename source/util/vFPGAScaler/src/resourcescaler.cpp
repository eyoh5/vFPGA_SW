#include "resourcescaler.h"

ResourceScaler::ResourceScaler(ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, int window_size) : rsc_reg(rsc_reg), srv_reg(srv_reg), window_size(window_size) {


}

ResourceScaler::~ResourceScaler(){

}

void ResourceScaler::initWindow(int glob_num_prrs){

  for( int i=0; i<glob_num_prrs+1; i++){
    window.insert(std::pair<int, std::vector<WindowEntry>>(i, std::vector<WindowEntry>()));
  }

}

void ResourceScaler::scale(){

  printf("ResourceScaler>> periodic scaling\n");

  int N_services = srv_reg.getNumServices(); 
  int N_PRRs = rsc_reg.getNumPRRs();

  // Placeholder for resulted values
  // n_[i] : # of PRRs allocated to i-th service
  int n_[N_services]= {0};
  int tot_n_ = 0;

  // Placeholder for avg. work load of each service's 
  float avg_work_load[N_services] = {0};
  float serv_margin[N_services] = {0};
  float serv_margin_re[N_services] = {0};

  float tot_avg_work_load = 0.0;
  float tot_serv_margin =0.0;
  float tot_serv_margin_re = 0.0;


  
  for( int i=0; i<N_services; i++){

    /*
    *  Get Average Work Load of the allocated queue for the specified service
    */
    Service service = srv_reg.getEntry(i); 
    int N_allocated_PRRs = service.instances.size();
    printf(" N_allocated_PRRs of service#%d: %d\n", i, N_allocated_PRRs);
    for ( int j=0; j<N_allocated_PRRs; j++){

      WindowEntry avg_window = getAvgWindow(rsc_reg.getGlobPRRID(service.instances[j].first, service.instances[j].second));
      float avg_q_len = (float) avg_window.q_len;
      float avg_throughput = avg_window.throughput ; // blocking the divide by zero
     
      avg_work_load[i] += (avg_throughput ? avg_q_len * (1.0 / avg_throughput) : 0);
      printf("work_load[%d PRR] : %f \n", j, avg_q_len*(1.0/avg_throughput));
      printf("avg_q_len, avg_throughput : %f, %f\n", avg_q_len, avg_throughput);

    }
//    WindowEntry task_q_avg_window = getAvgWindow(N_PRRs+1);
//    float avg_task_q_work_load =  task_q_avg_window.q_len * (1.0 / task_q_avg_window.throughput);
//    avg_work_load[i] = avg_work_load[i] / (float) N_allocated_PRRs + avg_task_q_work_load;
    avg_work_load[i] = avg_work_load[i] / (float) N_allocated_PRRs ;
    serv_margin[i] = std::max(float(0), float(service.latency_threshold - avg_work_load[i] - 0.8)) +0.0000001;
    serv_margin_re[i] = 1.0/ (float)(serv_margin[i]);
    printf("serv_margin : %f, serv_margin_re : %f\n", serv_margin[i], serv_margin_re[i]);
    /*
    *  Scaling the # of instances of the service
    *  if work load of the service is larger than threshold ; increase instance
    *  else ; decrease instance
    *  we assure that each service has at least one instance
    */
    if(avg_work_load[i] > service.latency_threshold - 0.8) {
      printf("Scaler>> Increase PRR instance of service#%d from %d\n", i, N_allocated_PRRs); 
      n_[i] = N_allocated_PRRs+1; 
      printf("Scaler>> to %d \n", n_[i]);}
    else {
      printf("Scaler>> Decrease PRR instance of service#%d from %d \n", i, N_allocated_PRRs);    
      n_[i] = std::max(1, N_allocated_PRRs-1);
    }

    tot_avg_work_load += avg_work_load[i];
    tot_serv_margin += serv_margin[i];
    tot_serv_margin_re += serv_margin_re[i];
    tot_n_ += n_[i];
  }



  /*
  * Manage the contention 
  * if the total delta of the instance is larger than (total # of PRRs) - (total # of services)
  * it means that there is PRR contention,
  * in that case, allocate PRR in proportion to the work load
  */
  if( tot_n_ > N_PRRs ){
    printf("Contention is occured\n");
    for( int i=0; i<N_services; i++){
     n_[i] = 1 + floor( ((N_PRRs - N_services) * (avg_work_load[i]) / tot_avg_work_load) + 0.5) ;
       printf("---contention n_[%d] : %d\n", i, n_[i]); 
    }
  }



  /// for Debugging
  for ( int i=0; i<N_services; i++){
    printf("%d Service --------------------\n", i);
    printf("avg_work_load = %f \n", avg_work_load[i]);
    printf("n_ = %d \n", n_[i]);
  }


  /*
  *  Allocate actual PRR to service according to the resulted # of instances
  */    
  //allocPRR(n_);

  /*
  * 
  */
  for (int i=0; i<N_services; i++){
    n_[i] = srv_reg.getEntry(i).instances.size();
  }

  /*
  *  Print out the final scaling result to the log file
  */
  logFile.open(logFileName, std::fstream::out | std::fstream::app);
  if(log_file_first_access == 1) {
    for(int i=0; i<N_services; i++){
      logFile.setf(std::ios::left);
      logFile<<std::setw(15)<<"Avg.WorkLoad"<<std::setw(15)<<"NumPRRs";
    }
    logFile<<std::endl;
    log_file_first_access=0;
  }

  for(int i=0; i<N_services; i++){
    logFile.setf(std::ios::left);
    logFile<<std::setw(15)<<avg_work_load[i]<<std::setw(15)<<n_[i];
  }
  logFile<<std::endl;

  logFile.close();
}


void ResourceScaler::updateWindow(int prr_glob_id, float q_len, float throughput){

  WindowEntry entry;
  entry.q_len = q_len;
  entry.throughput = throughput;

  window[prr_glob_id].push_back(entry);

  if(window[prr_glob_id].size() > window_size){ // if the window is longer than the window size
    window[prr_glob_id].erase(window[prr_glob_id].begin()); // keep the window_size 
  }
  
}

WindowEntry ResourceScaler::getAvgWindow(int glob_prr_id){

  float avg_q_len = 0.0;
  float avg_throughput = 0.0;

  for(int i=0; i<window.size(); i++){
    if( i == glob_prr_id ) {
      for( int j=0; j<window[i].size(); j++ ){
        avg_q_len +=  window[i][j].q_len ;
        avg_throughput += window[i][j].throughput;
      }
    avg_q_len = avg_q_len / (float) window[i].size() ;
    avg_throughput = avg_throughput / (float) window[i].size() ;
    }
  }

  WindowEntry res ;
  res.q_len = avg_q_len;
  res.throughput = avg_throughput;

  return res;
}

void ResourceScaler::printWindow(){

  for(int i=0; i<window.size(); i++){
    printf("PRR#%d's Window  ==========================\n", i);
    for( int j=0; j<window[i].size(); j++){
      printf("Q_len : %f \n", window[i][j].q_len);
      printf("Throughput : %f \n", window[i][j].throughput);
    }
    printf("===========================================\n");
  }

}


void ResourceScaler::allocInitPRR(){ // FF manner


  for ( int service_id=0; service_id<srv_reg.getNumServices(); service_id++){

    Service service = srv_reg.getEntry(service_id);
   
    for (int dev_id=0; dev_id<rsc_reg.getNumDevices(); dev_id++){
      Device device = rsc_reg.getEntry(dev_id);
      Partition* prrs = device.prrs;
      for(int prr_id=0; prr_id<device.num_prrs; prr_id++){
         if(prrs[prr_id].allocated_to == -1 && service.is_there_bin_for(rsc_reg.getDeviceName(dev_id), prr_id) ) {
           // if the prr is empty 'and' the service has the bitstream for that prr
           prrs[prr_id].allocated_to = service_id; // update the allocation status of the prr
           srv_reg.addInstance(service_id, dev_id, prr_id);
           goto EXIT;
         }
      }
    }
   
  EXIT:;

  }

}

void ResourceScaler::allocPRR(int* n_){ // FF manner

  int N_services = srv_reg.getNumServices();
  int N_PRRs = rsc_reg.getNumPRRs();
 
  /*
  *  Deallocate PRR 
  *  if n_[service_id] < N_allocated 
  *  you should do deallocation first,
  *  if not, the total # of allocated PRR can exceed the total # of PRRs
  */
  for(int service_id=0; service_id<N_services ; service_id++){
    int N_allocated = srv_reg.getEntry(service_id).instances.size();

    if( n_[service_id] - N_allocated < 0 ) {
      printf(" Deallocate instances form %d to %d , service %d\n", N_allocated, n_[service_id], service_id); 
      for( int i=0; i< N_allocated - n_[service_id]; i++){
        std::pair<int, int> removed_inst = srv_reg.removeLastInstance(service_id);
        Device device = rsc_reg.getEntry(removed_inst.first);
        Partition* prrs = device.prrs;
        prrs[removed_inst.second].allocated_to = -1;
      }
    }
  }

  /*
  * Allocate more PRRs
  */
  for(int service_id =0; service_id<N_services; service_id++){
    
    int N_allocated = srv_reg.getEntry(service_id).instances.size();
    if( n_[service_id] - N_allocated > 0 ) {
//      printf(" Allocate more instances from %d to %d, service %d\n", N_allocated, n_[service_id], service_id);

      /*
      * First Fit
      */
//      for(int device_id=0; device_id<rsc_reg.getNumDevices(); device_id++){
//        Device device = rsc_reg.getEntry(device_id);
//        for(int prr_id=0; prr_id<device.num_prrs; prr_id++){
//          Service service = srv_reg.getEntry(service_id);
//          if( (n_[service_id] > service.instances.size())  && (device.prrs[prr_id].allocated_to == -1) && service.is_there_bin_for(device.dev_name, prr_id)){
////            printf(" add PRR %d to service %d\n", prr_id, service_id);
//            device.prrs[prr_id].allocated_to = service_id;
//            srv_reg.addInstance(service_id, device_id, prr_id); 
//          }
//        }
//      }


      /*
      * First Fit Increasing order of bins
      */
      //arrange PRRs in increasing siez
      std::vector<std::pair<int, int>> prr_id_incr_order; // <size, glob_prr_id>
      // trans. from prrs to vector
      for( int device_id =0; device_id< rsc_reg.getNumDevices(); device_id++){
        Device device = rsc_reg.getEntry(device_id);
        for (int prr_id =0; prr_id<device.num_prrs; prr_id++){
          printf("push to prr_id_incr_order Dev#%d, PRR#%d\n", device_id, prr_id);
          prr_id_incr_order.push_back(std::pair<int, int>(device.prrs[prr_id].size, rsc_reg.getGlobPRRID(device_id, prr_id))); 
        }
      }
  
      // sorting to increasing order
      printf("sorting..\n");
      std::sort(prr_id_incr_order.begin(), prr_id_incr_order.end());

      // FF increasing start
      for(int k=0; k<prr_id_incr_order.size(); k++){
        int glob_prr_id = prr_id_incr_order[k].second;
        int loc_prr_id = rsc_reg.getLocPRRID(glob_prr_id);
        Device device = rsc_reg.getEntry(rsc_reg.getDeviceID(glob_prr_id));
        Service service = srv_reg.getEntry(service_id);
        if( (n_[service_id] > service.instances.size()) && device.prrs[loc_prr_id].allocated_to == -1 && service.is_there_bin_for(device.dev_name, loc_prr_id)){
          device.prrs[loc_prr_id].allocated_to = service_id;
          srv_reg.addInstance(service_id, device.id, loc_prr_id);
        }
     }


    } // end if n_ - N_allocated >0

  } // end for service lop

}
