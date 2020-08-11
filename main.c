#include <stdio.h>      //for fprintf
#include <stdint.h>     //for uint16_t
#include <stdlib.h>     //for malloc
#include <string.h>     //for memcpy
#include <unistd.h>     //for sleep
#include "network_thread.h"
#include "redpitaya/rp.h"

void InitGenerator(){
    //Generate Scan Ramp
    rp_GenFreq(RP_CH_1,1000.0);
    rp_GenAmp(RP_CH_1,1.0);
    rp_GenWaveform(RP_CH_1, RP_WAVEFORM_TRIANGLE);
    rp_GenOutEnable(RP_CH_1);
}

int main(int argc, char **argv){
    
    if(rp_Init()!=RP_OK){
        fprintf(stderr, "Red Pitaya API failed to initialize!\n");
    }
    InitGenerator();
    
    
    //create mutex
    mtx_t mutex_network_acqbuffer;     //protects one buffer of oscilloscope data used for network transfer
    if(thrd_success!=mtx_init(&mutex_network_acqbuffer,mtx_plain)){
        exit(1);
    }
    //create condition
    cnd_t condidion_mainthread_finished_memcpy;
    if(thrd_success!=cnd_init(&condidion_mainthread_finished_memcpy)){
        exit(1);
    }
    //create network_acq_buffer
    int16_t* network_acqbufferP=(int16_t*) malloc(ADCBUFFERSIZE*sizeof(int16_t));
    
    //initialize interprocess communication sturct
    struct threadinfo threadinf;
    threadinf.mutex_rawdata_bufferP=&mutex_network_acqbuffer;
    threadinf.condidion_mainthread_finished_memcpyP=&condidion_mainthread_finished_memcpy;
    threadinf.network_acqBufferP=&network_acqbufferP;
    
    //create thread
    thrd_t networkingThread;
    if(thrd_success!=thrd_create(&networkingThread,thrd_startServer,(void*)&threadinf)){
        exit(1);
    }
    

    int16_t* acqbufferP=(int16_t*) malloc(ADCBUFFERSIZE*sizeof(int16_t));
    rp_AcqReset();
    rp_AcqSetDecimation(RP_DEC_8);
    
	rp_AcqSetTriggerDelay(0);
	
    rp_AcqStart();  //this commands initiates the pitaya to start aquiring samples, but we need to wait a bit to enable the trigger
	sleep(1);   //sleep 1s
	
    rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE);
	rp_GenTriggerEventCondition(RP_GEN_TRIG_EVT_A_START);
    //uint32_t ledState=RP_LOW;
    while(1){
        rp_DpinSetState(RP_LED0,RP_HIGH);
        rp_DpinSetState(RP_LED1,RP_LOW);
        rp_acq_trig_state_t state = RP_TRIG_STATE_WAITING;
        while(state!=RP_TRIG_STATE_TRIGGERED){
            rp_AcqGetTriggerState(&state);
        }
        //when this condition is met the red pitaya will reset the triggerSrc to Disabled
        rp_DpinSetState(RP_LED0,RP_LOW);
        rp_DpinSetState(RP_LED1,RP_HIGH);
        //printf("triggered\n");
        
        uint32_t samplenum=ADCBUFFERSIZE;
        rp_AcqGetOldestDataRaw(RP_CH_1,&samplenum,acqbufferP);
        ret=mtx_trylock(&mutex_rawdata_buffer)
        if(ret==thrd_success){
            //networking thread wants us to copy data into buffer, so lock it and copy data
            if(thrd_success!=mtx_lock(&mutex_network_acqbuffer)){
                exit(1);
            }
            memcpy(network_acqbufferP,acqbufferP,sizeof(uint16_t)*ADCBUFFERSIZE);
            if(thrd_success!=mtx_unlock(&mutex_network_acqbuffer)){
                exit(1);
            }
            //inform the thread that we are finished
            cnd_signal(&condidion_mainthread_finished_memcpy);
        }else if(ret!=thrd_busy){   //proceed normally on thrd_busy
            exit(1);
        }
        
        sleep(0.01);
        rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE);    //rearm trigger source

    }
	/* Releasing resources */
	free(network_acqbufferP);
    free(acqbufferP);
    //TODO delete mutexes, signal and destroy thread
    rp_Release();
    return 0;
}
