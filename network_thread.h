#include "threads.h"
#include <stdint.h>

#define ADCBUFFERSIZE 16384

struct threadinfo{
    mtx_t* mutex_rawdata_bufferP;
    cnd_t* condidion_mainthread_finished_memcpyP;
    float* network_acqBufferP;
    mtx_t* mutex_settingsP;
    float* settingsP;
    mtx_t* mutex_new_operation_modeP;
    uint16_t offsetsP;
};

int thrd_startServer(void* threadinfp);