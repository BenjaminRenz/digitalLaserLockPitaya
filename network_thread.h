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
    uint32_t* new_operation_modeP;
    uint16_t* offsetsP;
    mtx_t* mutex_characterizationP;
    uint32_t* numOfCharacterizationPointsP;
    float* characterisationXP;
    float* characterisationYP;
};

union networkfloat{
    float       flt;
    uint32_t* uint;
};

enum {
    operation_mode_scan=40,
    operation_mode_characterise=41,
    operation_mode_lock=42
};


int thrd_startServer(void* threadinfp);