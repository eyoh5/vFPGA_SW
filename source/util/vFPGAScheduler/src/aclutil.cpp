#include "aclutil.h"

aocl_mmd_interrupt_handler_fn irq_fn(int handle, void* user_data);
aocl_mmd_status_handler_fn srq_fn (int handle, void* user_data, aocl_mmd_op_t op, int status);
unsigned char *acl_loadFileIntoMemory (const char *in_file, size_t *file_size_out) {

  FILE *f = NULL;
  unsigned char *buf;
  size_t file_size;

  // When reading as binary file, no new-line translation is done.
  f = fopen (in_file, "rb");
  if (f == NULL) {
    fprintf (stderr, "Couldn't open file %s for reading\n", in_file);
    return NULL;
  }

  // get file size
  fseek (f, 0, SEEK_END);
  file_size = (size_t)ftell (f);
  rewind (f);

  // slurp the whole file into allocated buf
  buf = (unsigned char*) malloc (sizeof(char) * file_size);
  *file_size_out = fread (buf, sizeof(char), file_size, f);
  fclose (f);

  if (*file_size_out != file_size) {
    fprintf (stderr, "Error reading %s. Read only %lu out of %lu bytes\n",
                     in_file, *file_size_out, file_size);
    return NULL;
  }
  return buf;
}

aocl_mmd_interrupt_handler_fn irq_fn(int handle, void* user_data){

	printf("This is interrupt handler\n");
	std::cout<<user_data<<" from user \n";

} 

aocl_mmd_status_handler_fn srq_fn(int handle, void* user_data, aocl_mmd_op_t op, int status){

	printf("This is status handler\n");
	std::cout<<user_data<<"from user\n";
}


