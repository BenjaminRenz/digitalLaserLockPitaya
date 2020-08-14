#include <stdio.h>      //for fprintf
#include <stdint.h>     //for uint16_t
#include <stdlib.h>     //for malloc
#include <string.h>     //for memcpy
#include <unistd.h>     //for sleep
#include <time.h>       //for waiting for trigger timeout
#include "network_thread.h"
#include "redpitaya/rp.h"

//#define triggerBySrc

void InitGenerator(float freq){
    //Generate Scan Ramp
    rp_GenFreq(RP_CH_1,freq);
    rp_GenAmp(RP_CH_1,1.0);
    rp_GenWaveform(RP_CH_1, RP_WAVEFORM_TRIANGLE);
    rp_GenOutEnable(RP_CH_1);
}

double getDeltatimeS(void){
    static clock_t start=0.0;
    if(start==0.0){
        start=clock();
        return 0.0;
    }
    clock_t end;
    end=clock();
    double deltatime=((double)(end-start))/CLOCKS_PER_SEC;
    start=end;
    return deltatime;
}

int main(int argc, char **argv){
    int ret;
    if(rp_Init()!=RP_OK){
        fprintf(stderr, "Red Pitaya API failed to initialize!\n");
    }
    float ramp_freq=10.0f;
    InitGenerator(ramp_freq);
    
    
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
    threadinf.network_acqBufferP=network_acqbufferP;
    
    //create thread
    thrd_t networkingThread;
    if(thrd_success!=thrd_create(&networkingThread,thrd_startServer,(void*)&threadinf)){
        exit(1);
    }
    

    int16_t* acqbufferP=(int16_t*) malloc(ADCBUFFERSIZE*sizeof(int16_t));
    rp_AcqReset();
    rp_AcqSetDecimation(RP_DEC_1);
    //TODO test this here, we get a strange timout down for the trigger for dec_8
    
	rp_AcqSetTriggerDelay(8192);  //we can set the trigger conditions immediately since the delay will wait as long as we need
	
    rp_AcqStart();  //this commands initiates the pitaya to start aquiring samples
	
    rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE);
	rp_GenTriggerEventCondition(RP_GEN_TRIG_EVT_A_START);
    
    while(1){
        rp_DpinSetState(RP_LED0,RP_HIGH);
        rp_DpinSetState(RP_LED1,RP_LOW);
        
        //initialize Delattime
        double deltat=0.0;
        getDeltatimeS();
        rp_acq_trig_state_t state;
        #ifdef triggerBySrc
        rp_acq_trig_src_t trgsrc;
        do{
            deltat+=getDeltatimeS();
            rp_AcqGetTriggerSrc(&trgsrc);
        }while(trgsrc==RP_TRIG_SRC_AWG_PE&&deltat<=1.0);
        #else
        do{
            deltat+=getDeltatimeS();
            rp_AcqGetTriggerState(&state);
        }while(state!=RP_TRIG_STATE_TRIGGERED&&deltat<=1.0);
        #endif
        

        if(deltat>=1.0){
            printf("triggered because of timeout\n");
            rp_AcqGetTriggerState(&state);
            if(state==RP_TRIG_STATE_TRIGGERED){
                printf("State is still triggered\n");
            }else if(state==RP_TRIG_STATE_WAITING){
                printf("State is waiting\n");
            }else{
                printf("State is now %d\n",state);
            }
        }/*else{
            printf("triggered normal\n");
            rp_AcqGetTriggerState(&state);
            if(state==RP_TRIG_STATE_TRIGGERED){
                printf("State is still triggered\n");
            }else if(state==RP_TRIG_STATE_WAITING){
                printf("State is waiting\n");
            }else{
                printf("State is now %d\n",state);
            }
        }*/
        
        //when this condition is met the red pitaya will reset the triggerSrc to Disabled
        rp_DpinSetState(RP_LED0,RP_LOW);
        rp_DpinSetState(RP_LED1,RP_HIGH);
        //printf("triggered\n");
        
        uint32_t samplenum=ADCBUFFERSIZE;
        rp_AcqGetOldestDataRaw(RP_CH_1,&samplenum,acqbufferP);
        ret=mtx_trylock(&mutex_network_acqbuffer);
        if(ret==thrd_success){
            //networking thread wants us to copy data into buffer, copy data
            memcpy(network_acqbufferP,acqbufferP,sizeof(uint16_t)*ADCBUFFERSIZE);
            if(thrd_success!=mtx_unlock(&mutex_network_acqbuffer)){
                exit(1);
            }
            //inform the thread that we are finished
            cnd_signal(&condidion_mainthread_finished_memcpy);
        }else if(ret!=thrd_busy){   //proceed normally on thrd_busy
            exit(1);
        }
        rp_AcqStart();
        /*getDeltatimeS();
        double elapsedTime=0.0;
        while(elapsedTime<0.01){
            elapsedTime+=getDeltatimeS();
        }*/
        rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE);    //rearm trigger source
    }
	/* Releasing resources */
	free(network_acqbufferP);
    free(acqbufferP);
    //TODO delete mutexes, signal and destroy thread
    rp_Release();
    return 0;
}
