#ifndef _VFPGASCHEDULER_H
#define _VFPGASCHEDULER_H 

#include "resourceregistry.h"
#include "serviceregistry.h"
#include "queuemanager.h"

#include <unistd.h>

struct IQEntry;

class vFPGAScheduler{
  private: 
	ResourceRegistry& rsc_reg;
	ServiceRegistry& srv_reg;
  public:
	vFPGAScheduler(ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg);
	~vFPGAScheduler();
	void run();
	int schedule(IQEntry* task);
	std::pair<int, int> get_partition(ServiceInfo serv_info);
};

#endif
