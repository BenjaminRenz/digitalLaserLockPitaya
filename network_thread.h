#include "threads.h"
#include <stdint.h>

#define ADCBUFFERSIZE 16384

struct threadinfo{
    mtx_t* mutex_rawdata_bufferP;
    cnd_t* condidion_mainthread_finished_memcpyP;
    int16_t* network_acqBufferP;
    mtx_t* mutex_settingsP;
    float* settingsP;
    mtx_t* mutex_offsetsP;
    uint16_t offsetsP;
};

int thrd_startServer(void* threadinfp);