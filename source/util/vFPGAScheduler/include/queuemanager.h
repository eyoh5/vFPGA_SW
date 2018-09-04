#ifndef QUEUEMANAGER_H
#define QUEUEMANAGER_H

#include "vfpgascheduler.h"
#include "serviceregistry.h"
#include "aclutil.h"

#include <unistd.h>
#include <stdio.h>
#include <queue>


class vFPGAScheduler;

struct HardwareInfo{
	int dev_handle;
	int part_id;
	int kernel_if;
} typedef HardwareInfo;


struct IQEntry{
	ServiceInfo serv_info;
	HardwareInfo hw_info;
	bool is_done=false;
} typedef IQEntry;


class QueueManager{
private:
	std::queue<IQEntry*>& IQ;
	std::queue<IQEntry*>& PRQ;
	vFPGAScheduler& scheduler;
public:
	QueueManager(std::queue<IQEntry*>& IQ, std::queue<IQEntry*>& PRQ, vFPGAScheduler& scheduler);
	~QueueManager();
	void runTaskQueue();
	void runPRQueue();
	IQEntry* popTaskQueue();
	IQEntry* popPRQueue();
	void pushTaskQueue(IQEntry& entry);
	void pushPRQueue(IQEntry& entry);

};

#endif
