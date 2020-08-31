#include "threads.h"
#include <stdint.h>

#define ADCBUFFERSIZE 16384

struct threadinfo{  //the mutexes will protect the memory of the directly following pointers on heap
    mtx_t mutex_network_acqBuffer;
    cnd_t condidion_mainthread_finished_memcpy;
    float* network_acqBufferP;

    mtx_t mutex_network_settings;
    float* network_settingsP;

    mtx_t mutex_network_operation_mode;
    uint32_t network_operation_mode;

    mtx_t mutex_network_characterization;
    uint32_t network_numOfCharacterizationPoints;
    float* network_characterisationXP;
    float* network_characterisationYP;
};



enum {
    operation_mode_not_initialized=0,
    operation_mode_scan_cav=40,
    operation_mode_scan_lsr=41,
    operation_mode_characterise=42,
    operation_mode_lock=43,
    operation_mode_shutdown=44
};


int thrd_startServer(void* threadinfp);
