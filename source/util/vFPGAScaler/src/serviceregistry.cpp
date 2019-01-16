#include "serviceregistry.h"

ServiceRegistry::ServiceRegistry(){

}

ServiceRegistry::~ServiceRegistry(){

}

/*
*  Register the service on service registry.
*  1) generate bins list
*  2) add to the service registry
*  RETURN : non-zero integer which refers to service ID, if there is no error
*           -1, if the name is already in use
*/
int ServiceRegistry::registerService(char* name, int num_bins, ...){
 
  va_list vl;
  va_start(vl, num_bins + 1);
  printf("ServiceRegistry>> Register Service %s with %d bins ...\n", name, num_bins);

  // Create new service entry
  Service service;
  service.name = name;

  // Gnerate bins list
  for(int i=0; i<num_bins; i++){
    char* bin_file = va_arg(vl, char*);  
    char* dev_name = va_arg(vl, char*); 
    int prr_id = va_arg(vl, int);
    binsEntry bin;
    bin.dev_name = dev_name;
    bin.prr_id = prr_id;
    bin.bin_file = bin_file;
    service.bins.push_back(bin);
  }

  service.latency_threshold = va_arg(vl, double);

  va_end(vl);

  // Add the new service entry to the service registry
  service.id = addEntry(service);

  printf("ServiceRegistry>> [%s] registered successfully, service ID %d will be returned\n", name, service.id);


  return service.id;

}

int ServiceRegistry::addEntry(Service service){

  service.id = registry.size();
  registry.insert({service.id, service});
  return service.id;

}


Service ServiceRegistry::getEntry(int service_id){

  return registry[service_id];

}

int ServiceRegistry::getNumServices(){

  return registry.size();

}

int ServiceRegistry::getServiceID(char* name){

  for(int i=0; i<registry.size(); i++){
    if(strcmp(registry[i].name, name) == 0) return registry[i].id;
  }

  printf("Error: [ServiceRegistry::getServiceID] No matched service \n");
  exit(0);
}

char* ServiceRegistry::getServiceBin(int service_id, const char* dev_name, int loc_prr_id){

  Service service = registry[service_id];

  for(int i=0; i<service.bins.size(); i++){
    binsEntry bin = service.bins[i];
    if(bin.prr_id == loc_prr_id && !strcmp(bin.dev_name, dev_name)) return bin.bin_file;
  }

//  for(int i=0; i<registry.size(); i++){
//    if(registry[i].id == service_id){
//      for(int j=0; j<registry[i].bins.size(); j++){
//        if(registry[i].bins[j].prr_id == loc_prr_id && !strcmp(registry[i].bins[j].dev_name, dev_name)) return registry[i].bins[j].bin_file;
//      }
//    }
//  }

  printf("Error: [ServiceRegistry] No matched bin file\n");
  exit(0);

}




void ServiceRegistry::print(){

  for(std::unordered_map<int, Service>::iterator it = registry.begin() ; it != registry.end(); it++){
    printf("%s Service==============\n", (it->second).name);
    printf("ID: %d\n", (it->second).id);
    printf("latency_thredhols : %f\n", (it->second).latency_threshold);
    printf("%d Bin Files [dev_name, part_id, filename]....\n", (it->second).bins.size());
    for(int i=0; i<(it->second).bins.size(); i++){
      printf("[%s, %d, %s]\n", (it->second).bins[i].dev_name, (it->second).bins[i].prr_id, (it->second).bins[i].bin_file );
    }
    printf("%d Allocated instacnes [dev_id, prr_id] ....\n", (it->second).instances.size());
    for(int j=0; j<(it->second).instances.size(); j++){ 
      printf("[%d, %d]\n", (it->second).instances[j].first, (it->second).instances[j].second);
    }
    printf("========================\n");
  }

}


void ServiceRegistry::addInstance(int service_id, int dev_id, int prr_id){

  // check the registry first
  // and if the specified PRR is allocated as instances
  // do not add that PRR as instance
  bool already_added = false;

  for ( int i=0; i<registry[service_id].instances.size(); i++){
    if(registry[service_id].instances[i].first == dev_id && registry[service_id].instances[i].second == prr_id) {
      already_added=true;
      break;
    }
  }

  if(! already_added)
    registry[service_id].instances.push_back(std::pair<int, int>(dev_id, prr_id));

}

std::pair<int, int> ServiceRegistry::removeLastInstance(int service_id){
  
  int last_idx = registry[service_id].instances.size() - 1 ;
  std::pair<int, int> res = registry[service_id].instances[last_idx];

  registry[service_id].instances.pop_back();

  
  return res;
}


void ServiceRegistry::setServiceTimeWOInterference(int service_id, float runtime){

  registry[service_id].latency_wo_interference = runtime;

}

void ServiceRegistry::incrRRFlag(int service_id){
  registry[service_id].RR_flag ++;

}

int ServiceRegistry::getRRFlag(int service_id){
  return registry[service_id].RR_flag;
}
