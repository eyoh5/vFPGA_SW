#include "resourceregistry.h"
#include <string.h>

ResourceRegistry::ResourceRegistry(){
}

ResourceRegistry::~ResourceRegistry(){

}

int ResourceRegistry::registerDevice(Device device){

  device.id = addEntry(device);

  return device.id;
}

int ResourceRegistry::addEntry(Device device){

  device.id = registry.size();
  registry.insert({device.id, device});

  return device.id;

}

Device ResourceRegistry::getEntry(int id){

  return registry[id];

}

Device ResourceRegistry::getEntryByGlobPRRID(int glob_prr_id){

  int _glob_prr_id = 0;

  for(int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if( _glob_prr_id == glob_prr_id) return registry[i];
      _glob_prr_id ++;
    }
  }

  printf("Error: [ResourceRegistry::getEntryByGlobPRRID] No matched device\n");
  exit(0);

}



std::string ResourceRegistry::getDeviceName(int dev_id){

  for(int i=0; i<registry.size(); i++){
    if(dev_id == i) return registry[i].dev_name;
  }
  printf("Error: [ResourceRegistry::getDeviceName] No matched device\n");
  exit(0);
  
}

int ResourceRegistry::getDeviceHandle(int dev_id){
  
  for(int i=0; i<registry.size(); i++){
    if( dev_id == i ) return registry[i].dev_handle;
  }
 
  printf("Error: [ResourceRegistry::getDeviceHandle] No matched device\n");
  exit(0); 
}

int ResourceRegistry::getNumDevices(){

  return registry.size();

}

int ResourceRegistry::getNumPRRs(){

  int res = 0;

  for(int i=0; i<registry.size(); i++){
    res += registry[i].num_prrs;
  }

  return res;
}


int ResourceRegistry::getDeviceID(int glob_prr_id){

  for(int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if(getGlobPRRID(i, j) == glob_prr_id) return i;
     }
  }

  printf("Error: [ResourceRegistry::getDeviceID] No matched device\n");
  exit(0);

}

int ResourceRegistry::getGlobPRRID(int dev_id, int prr_id){

  int glob_prr_id=0;

  for(int i=0; i<registry.size(); i++){
    if ( i == dev_id){
      for(int j=0; j<registry[i].num_prrs; j++){
        if( j == prr_id ){
          return glob_prr_id;
        }
        // else
        glob_prr_id ++;
      }
    }
    //else 
    glob_prr_id += registry[i].num_prrs;
  }

  printf("Error: [ResourceRegistry::getGlobPRRID] no matched local PRR ID\n");
  exit(0);

}


int ResourceRegistry::getLocPRRID(int glob_prr_id){

  for(int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if(getGlobPRRID(i, j) == glob_prr_id) return j;
    }
  }
  
  printf("Error: [ResourceRegistry::getLocPRRID] no matched PRR\n");
  exit(0);

}

PRRStatus ResourceRegistry::getPRRStatus(int glob_prr_id){

  for(int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if( getGlobPRRID(i, j) == glob_prr_id){
        return registry[i].prrs[j].status;
      }
    }
  }
 
  printf("Error: [ResourceRegistry::getPRRStatus] No matched PRR\n");
  exit(0);
 
}

void ResourceRegistry::setPRRStatus(int glob_prr_id, PRRStatus status){

  struct timeval time;
  int _glob_prr_id=0;
 
  for(int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if(/*getGlobPRRID(i, j) == glob_prr_id*/ _glob_prr_id == glob_prr_id) {
        registry[i].prrs[j].status = status;
        gettimeofday(&time, NULL);
        printf("setPRRStatus: (%f, %f)\n", (float) time.tv_sec, (float)time.tv_usec);
        if (status == RUNNING) setPRRStartRun(i, j, time);
        else setPRREndRun(i, j, time); // status == READY
        return;
      }
      _glob_prr_id++;
    }
  }

  printf("Error: [ResourceRegistry::setPRRStauts] No matched PRR\n");
  exit(0);
  
}

void ResourceRegistry::print(){

  for( std::unordered_map<int, Device>::iterator it = registry.begin() ; it != registry.end() ; it++){
    printf("%s Device =============\n", (it->second).dev_name.c_str());
    printf("dev_name : %s \n", (it->second).dev_name.c_str());
    printf("board_name : %s \n", (it->second).board_name.c_str());
    printf("id : %d \n", (it->first));
    printf("dev_handle : %d\n", (it->second).dev_handle);
    printf("num_prrs : %d\n", (it->second).num_prrs);

    Partition* prrs = (it->second).prrs;
      for(int part=0 ; part<(it->second).num_prrs ; part++){
        printf("Partition %d ===========\n", prrs[part].id);
        printf("id : %d \n", prrs[part].id);
        printf("allocated_to : %d\n", prrs[part].allocated_to);
        printf("programmed_with : %d\n", prrs[part].programmed_with);
        printf("status : %d\n", prrs[part].status);
        printf("start_run : (%ld,%ld)\n", prrs[part].start_run.tv_sec, prrs[part].start_run.tv_usec);
        printf("end_run : (%ld,%ld)\n", prrs[part].end_run.tv_sec, prrs[part].end_run.tv_usec);
        printf("========================\n");
      }
      printf("===========================================================\n");

  }

}


bool ResourceRegistry::isPrgWith(int glob_prr_id, int service_id){

  int _glob_prr_id=0;

  for( int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if( _glob_prr_id == glob_prr_id && registry[i].prrs[j].programmed_with == service_id) return true;
      _glob_prr_id++;
   }
  }

  return false;

}

void ResourceRegistry::setPRRPrgStatus(int glob_prr_id, int service_id){

  for( int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if( getGlobPRRID(i, j) == glob_prr_id) {
        registry[i].prrs[j].programmed_with = service_id;
        return;
      }
   }
  }

  printf("Error: [ResourceRegistry::setPRRPrgStatus] No matched PRR\n");
  exit(0);

}

void ResourceRegistry::setPRRStartRun(int dev_id, int loc_prr_id, struct timeval start){

  registry[dev_id].prrs[loc_prr_id].start_run.tv_sec = start.tv_sec;
  registry[dev_id].prrs[loc_prr_id].start_run.tv_usec = start.tv_usec;

}

void ResourceRegistry::setPRREndRun(int dev_id, int loc_prr_id, struct timeval end){

  registry[dev_id].prrs[loc_prr_id].end_run.tv_sec = end.tv_sec;
  registry[dev_id].prrs[loc_prr_id].end_run.tv_usec = end.tv_usec;
}

float ResourceRegistry::calcPRRThroughput(int glob_prr_id){

  float res = 0.0;

  for(int i=0; i<registry.size(); i++){
    for(int j=0; j<registry[i].num_prrs; j++){
      if( getGlobPRRID(i, j) == glob_prr_id){
        struct timeval s = registry[i].prrs[j].start_run;
        struct timeval e = registry[i].prrs[j].end_run;
        res = (e.tv_sec - s.tv_sec) + (e.tv_usec - s.tv_usec)/1000000.0;
        return 1.0/res;
      }
    }
  }

  printf("Error: [ResourceRegistry::calcPRRThroughput] No matched PRR\n");
  exit(0);

}
