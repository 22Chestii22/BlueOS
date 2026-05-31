#include "types.h"
#include "kernel_api.h"

void module_entry(kernel_api_t* api)
{
    api->printf("[DEMO] Hello from a loadable .sys module!\n");
    api->printf("[DEMO] This module was loaded from /SYSTEM/DRIVERS/\n");
}
