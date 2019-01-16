#ifndef ACLUTIL_H
#define ACLUTIL_H

#include <unistd.h>
#include <iostream>

#include "aocl_mmd.h"

#define MMD_STRING_RETURN_SIZE 1024
#define MAX_PRR_PER_DEV 5


//aocl_mmd_interrupt_handler_fn irq_fn(int handle, void* user_data);
//aocl_mmd_status_handler_fn srq_fn (int handle, void* user_data, aocl_mmd_op_t op, int status);
unsigned char* acl_loadFileIntoMemory(const char *in_file, size_t *file_size_out);


#endif
