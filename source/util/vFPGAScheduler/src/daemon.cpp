#include "daemon.h"


Daemon::Daemon(QueueManager& q_mgr, ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, vFPGAScheduler& scheduler) : q_mgr(q_mgr), rsc_reg(rsc_reg), srv_reg(srv_reg), scheduler(scheduler){

  printf("Daemon>> Daemon is Constructed!\n");

  // Initialize Resource Registry 
  rsc_reg_init();  

}


Daemon::~Daemon(){
}


void Daemon::run(){

  // run sub modules
  std::thread t_iq_mgr(&QueueManager::runTaskQueue, &q_mgr);
  std::thread t_prq_mgr(&QueueManager::runPRQueue, &q_mgr);
//  std::thread t_scheduler(&vFPGAScheduler::run, &scheduler);


  while(1){
		printf("Daemon>> Daemon is running\n");
		sleep(5);
	}

  t_iq_mgr.join();
  t_prq_mgr.join();
//  t_scheduler.join();	


}


void Daemon::rsc_reg_init(){


  // get all supported board names from MMD
	char boards_name[MMD_STRING_RETURN_SIZE];
  aocl_mmd_get_offline_info(AOCL_MMD_BOARD_NAMES, sizeof(boards_name), boards_name, NULL);

  //query through all possible devices 
  char *dev_name;
  
  for(dev_name = strtok(boards_name, ";"); dev_name != NULL ; dev_name = strtok(NULL, ";")){

		DeviceInfo dev_info;				

		int handle = aocl_mmd_open(dev_name);
    printf("device handle is %d\n", handle);

		dev_info.handle = handle;

		char board_name[MMD_STRING_RETURN_SIZE];
		aocl_mmd_get_info(handle, AOCL_MMD_BOARD_NAME, sizeof(board_name), board_name, NULL);
 		dev_info.name = board_name ;

		//int num_partitions;
		//aocl_mmd_get_device_info(handle, AOCL_MMD_BOARD_NUM_PARTITIONS, sizeof(num_partitions), &num_partitions);
	

    dev_info.num_partitions = 4;// temporal num_partitions

    // Init the partition info of the device
		dev_info.partitions = new PartitionInfo[dev_info.num_partitions];
		for(int i=0; i<dev_info.num_partitions ; i++){
			dev_info.partitions[i].id = i;	
	    dev_info.partitions[i].programmed_with = 0;
			dev_info.partitions[i].status = READY;
		}
		//(dev_info.partitions)->rus = 1975// temporal alus 

		rsc_reg.push(dev_info);

	 }

}
