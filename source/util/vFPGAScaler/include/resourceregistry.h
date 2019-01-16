#ifndef RESOURCEREGISTRY_H
#define RESOURCEREGISTRY_H

#include <unordered_map>
#include <string>
#include <sys/time.h>

enum PRRStatus{
  READY,
  RUNNING
} typedef PRRStatus;

struct Partition{
  int id;
  int allocated_to =-1;
  int programmed_with =-1;
  int size =0;
  PRRStatus status =READY;
  struct timeval start_run;
  struct timeval end_run;
};

struct Device{
  std::string dev_name;
  std::string board_name;
  int id;
  int dev_handle;
  int num_prrs;
  Partition* prrs;

} typedef Device;


class ResourceRegistry{
private:
  std::unordered_map <int, Device> registry; // <device ID, Device>

public:
  ResourceRegistry();
  ~ResourceRegistry();
  int registerDevice(Device device);
  int addEntry(Device device);
  Device getEntry(int id);
  Device getEntryByGlobPRRID(int glob_prr_id);
  std::string getDeviceName(int dev_id);
  int getDeviceHandle(int dev_id);
  int getDeviceID(int glob_prr_id);
  int getNumDevices();
  int getNumPRRs();
  int getGlobPRRID(int dev_id, int prr_id);
  int getLocPRRID(int glob_prr_id);
  PRRStatus getPRRStatus(int glob_prr_id);
  void setPRRStatus(int glob_prr_id, PRRStatus status);
  void print();
  bool isPrgWith(int glob_prr_id, int service_id);
  void setPRRPrgStatus(int glob_prr_id, int service_id);
  void setPRRStartRun(int dev_id, int loc_prr_id, struct timeval start);
  void setPRREndRun(int dev_id, int loc_prr_id, struct timeval end);
  float calcPRRThroughput(int glob_prr_id);
};

#endif
