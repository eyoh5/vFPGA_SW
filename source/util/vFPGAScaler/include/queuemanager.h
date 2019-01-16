#ifndef QUEUEMANAGER_H
#define QUEUEMANAGER_H

#include "resourceregistry.h"
#include "serviceregistry.h"
#include "resourcescaler.h"

#include <queue>
#include <map>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <fstream>
#include <stdarg.h>

#include "aocl_mmd.h"

#if defined(WINDOWS)
#  include "wdc_lib_wrapper.h"
#endif   // WINDOWS

#if defined(LINUX)
#  include "../../linux64/driver/hw_pcie_constants.h"
#  include "../../linux64/driver/pcie_linux_driver_exports.h"
#endif   // LINUX


class TaskQueueManager;
class PRQueueManager;
class PRRQueueManager;
unsigned char* loadFileIntoMemory (const char *in_file, size_t *file_size_out); 

enum RequestType{

  GETDEV,
  GETPRG

} typedef RequestType;
  

struct QEntry{

  RequestType type;
  int service_id;
  int glob_prr_id; // returned by getDevice
  bool is_done=false;
  struct timeval enque_taskQ_point, 
                 deque_taskQ_point,
                 enque_PRRQ_point,
                 deque_PRRQ_point;
  int task_q_len;  

} typedef QEntry;

enum TaskQSchedulingType {

  LocRR, // Round Robin
  GlobRR

} typedef TaskQSchedulingType;


class TaskQueueManager {
private:
  std::queue<QEntry*>& Q;
  std::map<int, PRQueueManager* >& PRQ_mgrs; // <dev_id, prq_mgr> list
  std::map<int, PRRQueueManager* >& PRRQ_mgrs; // <glob_prr_id, prrq_mgr> list
  ResourceRegistry& rsc_reg;
  ServiceRegistry& srv_reg;
  ResourceScaler& scaler;
  int time_in_task;
  int t_rpt_period;  
  int t_scale_period;
  int logfile_first_access = 1;
  float throughput=0.0;
public:
  TaskQueueManager(std::queue<QEntry*>& Q, std::map<int, PRQueueManager*>& PRQ_mgrs, std::map<int, PRRQueueManager*>& PRRQ_mgrs, ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, ResourceScaler& scaler, int time_in_task, int t_rpt_period, int t_scale_period);
  ~TaskQueueManager();
  void push(QEntry* entry);
  QEntry* pop();
  void runDaemon();
  void report();
  int determinPRR(QEntry* entry, TaskQSchedulingType s_type);
};

class PRQueueManager {
private:
  std::queue<QEntry*>& Q;
  ResourceRegistry& rsc_reg;
  ServiceRegistry& srv_reg;
  int dev_id;

public:
  PRQueueManager(std::queue<QEntry*>& Q, ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, int dev_id);
  ~PRQueueManager();
  void push(QEntry* entry);
  QEntry* pop();
  void runDaemon();

};

class PRRQueueManager {
private:
  std::queue<QEntry*>& Q;
  ResourceRegistry& rsc_reg;
  int glob_prr_id;
  float throughput=0.0;

public:
  PRRQueueManager(std::queue<QEntry*>& Q, ResourceRegistry& rsc_reg, int glob_prr_id);
  ~PRRQueueManager();
  void push(QEntry* entry);
  QEntry* pop();
  float getQlen();
  float getThroughput();
  void runDaemon();

};
#endif
