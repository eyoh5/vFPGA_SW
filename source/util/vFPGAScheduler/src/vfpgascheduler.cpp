#include "vfpgascheduler.h"
#include <iostream>

vFPGAScheduler::vFPGAScheduler(ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg) : rsc_reg(rsc_reg), srv_reg(srv_reg){
  std::cout<<"vFPGAScheduler>> vFPGAScheduler Init\n";
}

vFPGAScheduler::~vFPGAScheduler(){

}


void vFPGAScheduler::run(){

  while(1){
	std::cout<<"vFPGAScheduler>> vFPGASceduler is running\n";
	sleep(3);
  }

}


int vFPGAScheduler::schedule(IQEntry* task){
	
	std::pair<int, int> det_hw;
	det_hw = get_partition(task->serv_info);
	if(det_hw == std::make_pair(-1,-1)){
		printf("vFPGAScheduler >> no partition to host the service %d\n", (task->serv_info).id );
		task->is_done = true;
	}

	printf("vFPGAShceduler>> schedule task of service%d to partition%d of device%d\n", (task->serv_info).id, det_hw.second, det_hw.first);
  
  
	(task->hw_info).dev_handle = det_hw.first;
	(task->hw_info).part_id = det_hw.second;
	task->is_done = true;

	return 0;
  
	
}



std::pair<int, int> vFPGAScheduler::get_partition(ServiceInfo serv_info){

  // it should choose proper partition
  return rsc_reg.determine_partition(serv_info);


}

