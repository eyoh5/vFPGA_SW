#ifndef RESOURCESCALER
#define RESOURCESCALER

#include "resourceregistry.h"
#include "serviceregistry.h"

#include <vector>
#include <map>
#include <algorithm>
#include <math.h>
#include <fstream>
#include <stdarg.h>
#include <iomanip>

struct WindowEntry {
  float q_len;
  float throughput;
};

class ResourceScaler {
private:
  ResourceRegistry& rsc_reg;
  ServiceRegistry& srv_reg;
  int window_size;
  float threshold = 0.7;
  std::map<int, std::vector<WindowEntry>> window; // <glob_prr_id, PRR window> list
                                                  // <last_glob_prr_id + 1, PRR window> is for task queue
  // variables for log file
  char logFileName[20] = "../log/scaler.log";
  std::fstream logFile;
  int log_file_first_access = 1;

public:
  ResourceScaler(ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, int window_size);
  ~ResourceScaler();
  void initWindow(int glob_num_prrs);
  void scale();
  void updateWindow(int prr_glob_id, float q_len, float throughput);
  WindowEntry getAvgWindow(int glob_prr_id); 
  void printWindow();
  void allocInitPRR();
  void allocPRR(int* n_);

};


#endif
