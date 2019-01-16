#include "queuemanager.h"

// Task Q is critical section
// so we have to manage the mutex for it
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*
*  TaskQueueManager Member Functions
*/
TaskQueueManager::TaskQueueManager(std::queue<QEntry*>& Q, std::map<int, PRQueueManager*>& PRQ_mgrs, std::map<int, PRRQueueManager*>& PRRQ_mgrs, ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, ResourceScaler& scaler, int time_in_task, int t_rpt_period, int t_scale_period) : 

Q (Q) , PRQ_mgrs (PRQ_mgrs), PRRQ_mgrs (PRRQ_mgrs), rsc_reg (rsc_reg), srv_reg (srv_reg), scaler(scaler), time_in_task (time_in_task), t_rpt_period (t_rpt_period), t_scale_period (t_scale_period) {


}

TaskQueueManager::~TaskQueueManager(){

}

void TaskQueueManager::push(QEntry* entry){

  pthread_mutex_lock(&mutex);
  Q.push(entry);
  gettimeofday(&(entry->enque_taskQ_point), NULL);
  entry->task_q_len = Q.size();
  pthread_mutex_unlock(&mutex);

}

QEntry* TaskQueueManager::pop(){

  QEntry* entry = Q.front();
  Q.pop();

  return entry;

} 

void TaskQueueManager::runDaemon(){
  
  while(1){
    if( !Q.empty() ) {
        /*
        *  Variables for the log file
        */
        struct timeval start_t, end_t, start, end;
        float tot_q_lat = 0;
        float scaler_lat = 0;
        float getdev_lat = 0;
        float getprg_lat = 0;
        gettimeofday(&start_t, NULL);
        char logFileName[20] = "../log/taskQmgr.log";
        std::fstream logFile; 


        /*
        *  Scale the number of allocated resources per service
        */
        gettimeofday(&start, NULL);

        if( time_in_task % t_scale_period == 0 ) { printf("time_in_task %d\n", time_in_task);scaler.scale(); /*srv_reg.print();*/}


        gettimeofday(&end, NULL);
        scaler_lat = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec)/1000000.0; 

        // pop from the queue
	QEntry* entry = pop();
        gettimeofday(&(entry->deque_taskQ_point), NULL);

        /*
        *  Process the request according to the request type
        *  getDevice or getProgram
        */
        switch ( entry->type ){
          case GETDEV: { 

            static int rr_flag_idx=0;

            gettimeofday(&start, NULL);

            // determine the proper PRR to process the request
            int glob_prr_id = determinPRR(entry, LocRR);

  
            // transfer to the PRRQ_mgr
            printf("TaskQueueManager>> push the request of service#%d to PRR#%d\n", entry->service_id, glob_prr_id); 
            PRRQ_mgrs[glob_prr_id]->push(entry);
            gettimeofday(&(entry->enque_PRRQ_point), NULL); 
 
            // report queue length of each PRRs to scaler at every task processing
            if(time_in_task % t_rpt_period == 0) report();
  	  
            time_in_task++;

            gettimeofday(&end, NULL);
            getdev_lat = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec)/1000000.0;

            break;
          }
          case GETPRG: {
 
            gettimeofday(&start, NULL);

            if(!rsc_reg.isPrgWith(entry->glob_prr_id, entry->service_id)) { // the PRR is not configured with the service
              // transfer to the PRQ_mgr
              int dev_id = rsc_reg.getDeviceID(entry->glob_prr_id);
              printf("TaskQueueManager>> push the request of service#%d to Device#%d\n", entry->service_id, dev_id);
              PRQ_mgrs[dev_id]->push(entry);
              gettimeofday(&(entry->enque_PRRQ_point), NULL);

            }
            else{ // the PRR is alreday configured with the service ; done ;
              entry->is_done = true;
              gettimeofday(&(entry->enque_PRRQ_point), NULL);
              gettimeofday(&(entry->deque_PRRQ_point), NULL);
            }

            gettimeofday(&end, NULL);
            getprg_lat = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec)/1000000.0;
            
            break;
          }
          default: break;
        }
        

        gettimeofday(&end_t, NULL);  
       
        /*
        *  Print out the processing time to the log file
        */
        tot_q_lat = (end_t.tv_sec - start_t.tv_sec) + (end_t.tv_usec - start_t.tv_usec)/1000000.0;
        throughput = 1.0/tot_q_lat;


        logFile.open(logFileName, std::fstream::out | std::fstream::app);
        if( logfile_first_access == 1){
          logFile.setf(std::ios::left);
          logFile<<std::setw(15)<<"TotQLat."<<std::setw(15)<<"ScalerLat"<<std::setw(15)<<"getDevLat"<<std::setw(15)<<"getPrgLat"<<std::setw(15)<<"Req.Type"<<std::endl;
          logfile_first_access = 0;
        }

        logFile.setf(std::ios::left);
        logFile<<std::setw(15)<<tot_q_lat<<std::setw(15)<<scaler_lat<<std::setw(15)<<getdev_lat<<std::setw(15)<<getprg_lat<<std::setw(15)<<entry->type<<std::endl;

        logFile.close();
    }
  }

}

int TaskQueueManager::determinPRR(QEntry* entry, TaskQSchedulingType s_type){

  switch (s_type) {
    case LocRR: {// Round Robin only in allocated instances
      int instance_idx = (srv_reg.getRRFlag(entry->service_id)) % (srv_reg.getEntry(entry->service_id).instances.size()) ;
      int dev_id = srv_reg.getEntry(entry->service_id).instances[instance_idx].first;
      int prr_id = srv_reg.getEntry(entry->service_id).instances[instance_idx].second;
      int glob_prr_id = rsc_reg.getGlobPRRID(dev_id, prr_id);
      srv_reg.incrRRFlag(entry->service_id);
      return glob_prr_id;
      break;
    }
    case GlobRR: { // Round Robin over all PRRs
      static int RR_flag = 0;
      int RR_idx = RR_flag % rsc_reg.getNumPRRs();
      RR_flag++;
      return RR_idx;
    }
    default: {
      printf("Error: [TaskQueueManager] No matched task Q scheduling type \n");
      exit(0);
      break;
    }
  }

}



void TaskQueueManager::report(){

  // PRR Q 
  for( int i=0; i<PRRQ_mgrs.size(); i++){
    scaler.updateWindow(i, PRRQ_mgrs[i]->getQlen(), PRRQ_mgrs[i]->getThroughput());
  }

  // Task Q
  scaler.updateWindow(PRRQ_mgrs.size()+1, Q.size(), throughput);
}

/*
*  PRQueueManager Member Functions
*/
PRQueueManager::PRQueueManager(std::queue<QEntry*>& Q, ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, int dev_id) : Q (Q), rsc_reg(rsc_reg), srv_reg(srv_reg), dev_id(dev_id) {

}

PRQueueManager::~PRQueueManager(){

}


void PRQueueManager::push(QEntry* entry){

  Q.push(entry);

}

QEntry* PRQueueManager::pop(){
  
  QEntry* entry = Q.front();
  Q.pop();

  return entry;

}


void PRQueueManager::runDaemon(){

  while(1){
    if( !Q.empty() ) {
   
      QEntry* entry = pop();
      gettimeofday(&(entry->deque_PRRQ_point), NULL);      
      Device device = rsc_reg.getEntryByGlobPRRID(entry->glob_prr_id);

      /*
      *  Process the GETPRG request
      *  reprogram the PRR when the PRR is not alreday configured with the service
      */
       if(! rsc_reg.isPrgWith(entry->glob_prr_id, entry->service_id)){
         // load file into memory
         unsigned char* aocx_file = NULL;
         size_t aocx_filesize;
         char* bin_file_dir = srv_reg.getServiceBin(entry->service_id, device.dev_name.c_str(), rsc_reg.getLocPRRID(entry->glob_prr_id));
//         char* bin_file_dir = srv_reg.getServiceBin(entry->service_id, rsc_reg.getDeviceName(rsc_reg.getDeviceID(entry->glob_prr_id)).c_str(), rsc_reg.getLocPRRID(entry->glob_prr_id)); 
       aocx_file = loadFileIntoMemory(bin_file_dir, &aocx_filesize);
         if(aocx_file == NULL){
           printf("Error: Unable to load aocx file into memory\n");
           exit(0);
          }
  
         printf("PRQueueManager>> Reprogram the PRR#%d with %s\n", entry->glob_prr_id, bin_file_dir);
         // reprogram the PRR
         aocl_mmd_reprogram(rsc_reg.getDeviceHandle(rsc_reg.getDeviceID(entry->glob_prr_id)), aocx_file, aocx_filesize, rsc_reg.getLocPRRID(entry->glob_prr_id));
  
         // update the PRR's programed status
         rsc_reg.setPRRPrgStatus(entry->glob_prr_id, entry->service_id);
         

      } // end if isPrgWith

    entry->is_done = true;

    } // end if Q.empty
    
  } // end while

}


/*
*  PRRQueueManger Memeber Functions
*/
PRRQueueManager::PRRQueueManager(std::queue<QEntry*>& Q, ResourceRegistry& rsc_reg, int glob_prr_id) : Q (Q), rsc_reg(rsc_reg),  glob_prr_id(glob_prr_id) {

}

PRRQueueManager::~PRRQueueManager(){

}

void PRRQueueManager::push(QEntry* entry){

  Q.push(entry);

}

QEntry* PRRQueueManager::pop(){
  
  QEntry* entry = Q.front();
  Q.pop();

  return entry;
}

float PRRQueueManager::getQlen(){

  return (float) Q.size(); 

}

float PRRQueueManager::getThroughput(){

  return throughput;

}

void PRRQueueManager::runDaemon(){

  while(1){
    if( ! Q.empty() ){

      QEntry* entry = pop();      
     
      while(rsc_reg.getPRRStatus(glob_prr_id) != READY) ; // wait until the current running program release the prr
      printf("PRQueueManager>> launch the request\n");

      // calculate the current throughput
      throughput = rsc_reg.calcPRRThroughput(glob_prr_id); 
      printf("prr#%d calculated throughput, exetime :%f,  %f \n",glob_prr_id, throughput, 1.0/throughput);

      // set the device status as RUNNING
      // unset the device status as READY will be done by MMDApp::releaseDevice
      rsc_reg.setPRRStatus(glob_prr_id, RUNNING);
 
 
      entry->glob_prr_id = glob_prr_id;
      entry->is_done = true;
 

    }

  }

}
  
unsigned char* loadFileIntoMemory (const char *in_file, size_t *file_size_out) {

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

