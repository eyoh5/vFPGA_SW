#include "serviceregistry.h"

ServiceRegistry::ServiceRegistry(){

}

ServiceRegistry::~ServiceRegistry(){

}

ServiceInfo ServiceRegistry::GetEntry(int service_id){

}

ServiceInfo ServiceRegistry::AddEntry(ServiceInfo entry){
  int registry_len = registry.size();
  entry.id = registry_len + 1;
  registry.insert({entry.id, entry});

  return entry;
}

void ServiceRegistry::print(){

  for(std::unordered_map<int, ServiceInfo>::iterator it = registry.begin() ; it != registry.end(); it++){
    printf("%s Service==============\n", (it->second).name);
    printf("ID: %d\n", (it->second).id);
    printf("Bin Files [part_id, filename]....\n");
    for(int i=0; i<((it->second).bin_partition_map).size(); i++){
      printf("[%d, %s]\n", i, ((it->second).bin_partition_map)[i]);
    }
    printf("========================\n");
  }

}
