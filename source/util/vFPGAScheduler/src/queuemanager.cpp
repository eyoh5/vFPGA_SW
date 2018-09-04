#include "queuemanager.h"

QueueManager::QueueManager(std::queue<IQEntry*>& IQ, std::queue<IQEntry*>& PRQ, vFPGAScheduler& scheduler) : IQ(IQ), PRQ(PRQ), scheduler(scheduler) {
	printf("QueueManager>> QueueManager Constructed!\n");

}

QueueManager::~QueueManager(){
}

void QueueManager::runTaskQueue(){

	printf("QueueManager>> Q mgr Run!\n");

	while(1){
		if(!IQ.empty()){

			printf("QueueManager>> pop from the IQ\n");		
			IQEntry* request = popTaskQueue();
			printf("QueueManager>> service %d is requested\n", (request->serv_info).id);

			
		}
	}
}

void QueueManager::runPRQueue(){

  printf("QueueManager >> PRQ mgr Run!\n");

  while(1){
     if(!PRQ.empty()){
     
       printf("QueueManager>> pop from the PRQ\n");
       IQEntry* request = popPRQueue();
       printf("QueueManager>> service %d's reprogram is requested\n", (request->serv_info).id);
    }
  }

}

IQEntry* QueueManager::popTaskQueue(){

  // Pop from the Queue
	IQEntry* front = IQ.front();
	IQ.pop();

	// Deiliver the poped entry to scheduler
	scheduler.schedule(front);

  return front;
}

IQEntry* QueueManager::popPRQueue(){

  // Pop from the PRQ
  IQEntry* front = PRQ.front();
  PRQ.pop();

  // PR process
  unsigned char* aocx_file = NULL;
  size_t aocx_filesize;
  int pr_res;

  char* bin_file = ((front->serv_info).bin_partition_map)[(front->hw_info).part_id];

  aocx_file = acl_loadFileIntoMemory(bin_file, &aocx_filesize);
  if(aocx_file == NULL){
    printf("Error: Unable to load aocx file into memory\n");
  }

  pr_res = aocl_mmd_reprogram((front->hw_info).dev_handle, aocx_file, aocx_filesize, (front->hw_info).part_id);
  printf("Reprogram result : %d\n", pr_res);

  (front->hw_info).dev_handle = pr_res;
  (front->is_done) = true;

	return front; 
}
 
void QueueManager::pushTaskQueue(IQEntry& entry){

	// Push the entry to the queue
	IQ.push(&entry);

}
	


void QueueManager::pushPRQueue(IQEntry& entry){

  // Push the entry to the queue
  PRQ.push(&entry);

}
