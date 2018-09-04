#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

#include <unordered_map>
#include <map>

struct ServiceInfo{
  char* name;
  int id;
  std::map<int, char*> bin_partition_map; //<partition ID, bin file directory>

  int is_there_bin_for(int partition_id){

		for(std::map<int, char*>::iterator it = bin_partition_map.begin() ; it != bin_partition_map.end(); it++){
			if(it->first == partition_id) return 1;
		}	
		return 0;
	}

} typedef ServiceInfo;


class ServiceRegistry {
  private:
	std::unordered_map <int, ServiceInfo> registry; // <service ID, ServiceInfo>
  public: 
	ServiceRegistry();
	~ServiceRegistry();
	ServiceInfo GetEntry(int service_id);
	ServiceInfo AddEntry(ServiceInfo entry);
	void print();
};

#endif
