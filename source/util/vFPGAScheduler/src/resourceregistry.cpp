#include "resourceregistry.h"

ResourceRegistry::ResourceRegistry(){
}

ResourceRegistry::~ResourceRegistry(){

}

struct DeviceInfo ResourceRegistry::GetEntry(int device_id){
}

int ResourceRegistry::push(DeviceInfo dev_info){

	registry.insert({registry.size(), dev_info});
	
	return 0;

}

void ResourceRegistry::print(){

  for( std::unordered_map<int, DeviceInfo>::iterator it = registry.begin() ; it != registry.end() ; it++){

		printf("%s Device =============\n", (it->second).name);
		printf("handle : %d\n", (it->second).handle);

		printf("num_partitions : %d\n", (it->second).num_partitions);

		PartitionInfo* partitions = (it->second).partitions;
		for(int part=0 ; part<(it->second).num_partitions ; part++){
			printf("Partition %d ===========\n", partitions[part].id);
			printf("programmed_with : %d\n", partitions[part].programmed_with);
			printf("status : %d\n", partitions[part].status);
			printf("========================\n");
		}
		printf("===========================================================\n");
			
	}
}

std::pair<int, int> ResourceRegistry::determine_partition(ServiceInfo serv_info){
 
	int res_dev_handle;
	int res_part_id;
	
	printf("ResourceRegistry>> Before partition allocation\n");
	print();
	
	for( std::unordered_map<int, DeviceInfo>::iterator  it = registry.begin(); it != registry.end() ; it++){
		PartitionInfo* partitions = (it->second).partitions;
		for(int part = 0 ; part < (it->second).num_partitions; part++){
			if(partitions[part].programmed_with == serv_info.id){
				while(partitions[part].status != READY) ; // wait until the partition be ready
				partitions[part].status = RUN;
				return std::make_pair((it->second).handle, partitions[part].id);
			}
			else{// the partition is not programmed with the service 
				 if(serv_info.is_there_bin_for(partitions[part].id)){
					while(partitions[part].status != READY) ; // wait until the partition be ready
					partitions[part].status = RUN;
					return std::make_pair((it->second).handle, partitions[part].id);
				}
			}
		}
	}
	return std::make_pair(-1,-1); // if there is no partition to host the service
}

void ResourceRegistry::release(int dev_handle, int part_id){
  printf("ResourceRegistry>> release %d device %d partition\n", dev_handle, part_id);
  for(std::unordered_map<int, DeviceInfo>::iterator it = registry.begin() ; it != registry.end(); it++){
		if((it->second).handle == dev_handle){
			((it->second).partitions)[part_id].status = READY;
		}
	} 
}

void ResourceRegistry::reprogram(int prev_handle, int new_handle, int part_id, int service_id){

  for(std::unordered_map<int, DeviceInfo>::iterator it = registry.begin() ; it != registry.end(); it++){ 
    if( (it->second).handle == prev_handle) {
			((it->second).partitions)[part_id].programmed_with = service_id;
      (it->second).handle = new_handle;
		}
	}

  printf("ResourceReg>> %d reprogrammed status is updated with new handle %d\n", prev_handle, new_handle);

}

int ResourceRegistry::is_programmed_with(int dev_handle, int part_id, int service_id){

  for(std::unordered_map<int, DeviceInfo>::iterator it = registry.begin() ; it != registry.end(); it++){
    if((it->second).handle == dev_handle){
			PartitionInfo* partitions = (it->second).partitions;
      if(partitions[part_id].programmed_with == service_id) return 1;
			else break;
		}
	}
  return 0;
}
