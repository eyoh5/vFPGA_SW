#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

#include <vector>
#include <stdarg.h>
#include <unordered_map>
#include <string.h>
#include <string>

struct binsEntry{

  char* dev_name;
  int prr_id;
  char* bin_file;

} ;

struct Service{

  char* name;
  int id;
  float latency_threshold;
  float latency_wo_interference;
  int RR_flag=0;
  std::vector<binsEntry> bins; // bin file list
  std::vector<std::pair<int, int>> instances; // <allocated dev. ID, allocated prr ID> list

  int is_there_bin_for(std::string dev_name, int prr_id) {
    const char* c_dev_name = dev_name.c_str();
    for(int i=0; i<bins.size(); i++){
      if(strcmp(bins[i].dev_name, c_dev_name) == 0){
        if(bins[i].prr_id == prr_id) return 1;
      }
    }

    return 0;
  }

} typedef Service;


class ServiceRegistry {
private:
  std::unordered_map <int, Service> registry; // <service ID, ServiceInfo>
public:
  ServiceRegistry();
  ~ServiceRegistry();
  int registerService(char* name, int num_bins, ...);
  int addEntry(Service service);
  Service getEntry(int service_id);
  int getNumServices();
  int getServiceID(char* name);
  char* getServiceBin(int service_id, const char* dev_name, int loc_prr_id);
  void print();
  void addInstance(int service_id, int dev_id, int prr_id);
  std::pair<int, int> removeLastInstance(int service_id);
  void setServiceTimeWOInterference(int service_id, float runtime);
  void incrRRFlag(int service_id);
  int getRRFlag(int service_id);
};


#endif
