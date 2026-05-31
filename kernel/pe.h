#ifndef PE_H
#define PE_H

#include "types.h"

int load_pe_image(const char* path, uint64_t* entry_point, uint64_t* image_base);
int pe_load_and_exec(const char* path, const char* args);
int pe_spawn(const char* path);
int pe_check_format(const char* path);

#endif
