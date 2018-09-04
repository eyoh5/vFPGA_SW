#ifndef RESOURCEREGISTRY_H
#define RESOURCEREGISTRY_H

#include <stdlib.h>
#include <unordered_map>

#include "serviceregistry.h"

enum PartitionStatus {
	READY = 1, 
	RUN = 2,
};

struct PartitionInfo{
  int id;
  int programmed_with;
 // int rus;
 // int num_ffs;
 // int num_luts;
 // int num_brams;
  PartitionStatus status;
} typedef PartitionInfo;


struct DeviceInfo{
  char* name;
  int handle;
  int num_partitions;
  PartitionInfo* partitions;
} typedef DeviceInfo;


class ResourceRegistry{
  private:
	std::unordered_map <int, DeviceInfo> registry; // <ID, dev_info>
  public:
	ResourceRegistry();
	~ResourceRegistry();
	DeviceInfo GetEntry(int device_id);
	int push(DeviceInfo dev_info); 
	void print();
	std::pair<int, int> determine_partition(ServiceInfo serv_info);
	void release(int dev_handle, int part_id);
	void reprogram(int prev_handle, int new_handle, int part_id, int service_id);
	int is_programmed_with(int dev_handle, int part_id, int service_id);
};

#endif
