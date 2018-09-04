#ifndef DAEMON_H
#define DAEMON_H

#include "queuemanager.h"
#include "resourceregistry.h"
#include "serviceregistry.h"
#include "vfpgascheduler.h"

#include "aclutil.h"
#include "aocl_mmd.h"

#if defined(WINDOWS)
#  include "wdc_lib_wrapper.h"
#endif // WINDOWS

#if defined(LINUX)
#  include "../../linux64/driver/hw_pcie_constants.h"
#endif // LINUX


#include <unistd.h>
#include <thread>
#include <cstring>

class Daemon{
private:
	QueueManager& q_mgr;
	ResourceRegistry& rsc_reg;
	ServiceRegistry& srv_reg;
	vFPGAScheduler& scheduler;

public:
	Daemon(QueueManager& q_mgr, ResourceRegistry& rsc_reg, ServiceRegistry& srv_reg, vFPGAScheduler& scheduler);
	~Daemon();
	void run();
	void rsc_reg_init();
};

#endif
