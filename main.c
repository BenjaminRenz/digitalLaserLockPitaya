#include <stdio.h>      //for fprintf
#include <stdint.h>     //for uint16_t
#include <stdlib.h>     //for malloc
#include <string.h>     //for memcpy
#include <unistd.h>     //for sleep
#include <time.h>       //for waiting for trigger timeout
#include "network_thread.h"
#include "redpitaya/rp.h"

enum {operation_mode_scan,operation_mode_characterize,operation_mode_lock};

//Buffer size is 2^14 samples
//Sample rate is 125MSamples

//SLOW SCAN MODE IS 1Hz
//time to fill full buffer is is (2^14)*(2^13)/(125e6)=1.073s
#define SLOW_DEC 7630
//#define SLOW_DEC 8192
//index of the last sample floor(0.25/(SLOW_DEC/(125e6)))
#define SLOW_FIRST_SAMPLE_IDX_RAMP 3814
//index of the last sample ceil(0.75s/((2^13)/(125e6)))
#define SLOW_LAST_SAMPLE_IDX_RAMP 11445

//FAST SCAN MODE IS 100Hz
//time to fill full buffer is is (2^14)*(2^7)/(125e6)=0.0167s
#define FAST_DEC 77
//#define FAST_DEC 128
//index of the last sample floor(0.0025s/(FAST_DEC/(125e6)))
#define SLOW_FIRST_SAMPLE_IDX_RAMP 2441
//index of the last sample ceil(0.0075s/((2^7)/(125e6)))
#define SLOW_LAST_SAMPLE_IDX_RAMP 7325

#define CHK_ERR(function)                                                   \
    do {                                                                    \
        int errorcode=function;                                             \
        if(errorcode){                                                      \
            printf("Error code %d occured in line __LINE__\n",errorcode);   \
        }                                                                   \
    } while (0)

#define CHK_ERR_ACT(function,action)                                        \
    do {                                                                    \
        int errorcode=function;                                             \
        if(errorcode){                                                      \
            printf("Error code %d occured in line __LINE__\n",errorcode);   \
            action;                                                         \
        }                                                                   \
    } while (0)

void InitGenerator(float freq){
    //Generate Scan Ramp
    CHK_ERR(rp_GenFreq(RP_CH_1,freq));
    CHK_ERR(rp_GenAmp(RP_CH_1,1.0));
    CHK_ERR(rp_GenWaveform(RP_CH_1, RP_WAVEFORM_TRIANGLE));
    CHK_ERR(rp_GenOutEnable(RP_CH_1));

    CHK_ERR(rp_GenWaveform(RP_CH_2, RP_WAVEFORM_DC));
    CHK_ERR(rp_GenAmp(RP_CH_2,0));
    CHK_ERR(rp_GenOffset(RP_CH_2,0.0));
    CHK_ERR(rp_GenOutEnable(RP_CH_2));
}

void SetGenerator(rp_channel_t ScanChannel, float ScanFrequency, float OtherChannelOffset){
    CHK_ERR(rp_GenFreq(ScanChannel,ScanFrequency));
    CHK_ERR(rp_GenAmp(ScanChannel,1.0));
    CHK_ERR(rp_GenWaveform(ScanChannel, RP_WAVEFORM_TRIANGLE));
    CHK_ERR(rp_GenOutEnable(ScanChannel));

    rp_channel_t otherChannel;
    if(ScanChannel==RP_CH_1){
        otherChannel=RP_CH_2;
    }else{
        otherChannel=RP_CH_1;
    }

    CHK_ERR(rp_GenAmp(otherChannel, 0.0));
    CHK_ERR(rp_GenWaveform(otherChannel, RP_WAVEFORM_DC));
    CHK_ERR(rp_GenOffset(RP_CH_2, OtherChannelOffset));
    CHK_ERR(rp_GenOutEnable(otherChannel));
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

void findPeaks(uint16_t numOfPoints,float* ydata,uint16_t numOfPeaks,uint16_t deadzoneSize,uint16_t* peaksx_returnp,float* peaksy_returnp){
    for(uint16_t peakNum=0;peakNum<numOfPeaks;peakNum++){
        uint16_t bestx=0;
        float    besty=FLT_MIN;
        for(uint16_t sampleNum=0;sampleNum<numOfPoints;sampleNum++){
            //check for deadzone
            int deadzoneHit=0;
            for(int deadzone=0;deadzone<peakNum;deadzone++){
                if((peaksx_returnp[deadzone]-deadzoneSize)<sampleNum&&sampleNum<(peaksx_returnp[deadzone]+deadzoneSize)){
                    deadzoneHit=1;
                }
            }
            if(deadzoneHit){
                continue;
            }
            //check if current point is higher then last one
            if(best_y<ydata[sampleNum]){
                besty=ydata[sampleNum];
                bestx=sampleNum;
            }
        }
        peaksx_returnp[peakNum]=sampleNum;
        peaksy_returnp[peakNum]=besty;
    }
    return;
}

int testfunc(void){
    return 1;
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
    CHK_ERR_ACT(mtx_init(&mutex_network_acqbuffer,mtx_plain),exit(1));
    mtx_t mutex_new_operation_mode;
    CHK_ERR_ACT(mtx_init(&mutex_new_operation_mode,mtx_plain),exit(1));
    mtx_t mutex_settings;
    CHK_ERR_ACT(mtx_init(&mutex_settings,mtx_plain),exit(1));

    //create condition
    cnd_t condidion_mainthread_finished_memcpy;
    if(thrd_success!=cnd_init(&condidion_mainthread_finished_memcpy)){
        exit(1);
    }

    //create data structures which are protected by mutexes
    float* network_acqbufferP=(float*) malloc(ADCBUFFERSIZE*sizeof(float));
    int new_operation_mode=operation_mode_scan;


    //initialize interprocess communication sturct
    struct threadinfo threadinf;

    threadinf.mutex_rawdata_bufferP=&mutex_network_acqbuffer;
    threadinf.mutex_settingsP=&mutex_settings;
    threadinf.mutex_new_operation_modeP=&mutex_new_operation_mode;

    threadinf.condidion_mainthread_finished_memcpyP=&condidion_mainthread_finished_memcpy;

    threadinf.network_acqBufferP=network_acqbufferP;


    //create thread
    thrd_t networkingThread;
    if(thrd_success!=thrd_create(&networkingThread,thrd_startServer,(void*)&threadinf)){
        exit(1);
    }


    float* acqbufferP=(float*) malloc(ADCBUFFERSIZE*sizeof(float));
    CHK_ERR(rp_AcqReset());
    //TODO test rp_AcqSetDecimationFactor(uint32_t decimation); instead, free range 16-66536
    #CHK_ERR(rp_AcqSetDecimation(RP_DEC_1024));


    CHK_ERR(rp_AcqStart());  //this commands initiates the pitaya to start aquiring samples

    CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));
	CHK_ERR(rp_GenTriggerEventCondition(RP_GEN_TRIG_EVT_A_START));



    while(1){
        int last_operation_mode=operation_mode_scan;
        //check if a new mode of operation is requested
        mtx_lock(&mutex_new_operation_mode);
        if(last_operation_mode!=new_operation_mode){
            last_operation_mode=new_operation_mode;
            switch(new_operation_mode){
                case operation_mode_scan:
                    SetGenerator(RP_CH_1,100.0,0.0);
                    CHK_ERR(rp_AcqSetDecimationFactor(MODE_FAST_DEC));
                    CHK_ERR(rp_AcqSetTriggerDelay(8192));  //we can set the trigger conditions immediately since the delay will wait as long as we need

                break;
                case operation_mode_characterize:
                    SetGenerator(RP_CH_1,1.0,0.0);
                    CHK_ERR(rp_AcqSetDecimationFactor(MODE_SLOW_DEC));
                    CHK_ERR(rp_AcqSetTriggerDelay(8192));  //we can set the trigger conditions immediately since the delay will wait as long as we need


                break;
            }
        }
        mtx_unlock(&mutex_new_operation_mode);

        CHK_ERR(rp_DpinSetState(RP_LED0,RP_HIGH));
        CHK_ERR(rp_DpinSetState(RP_LED1,RP_LOW));

        rp_acq_trig_src_t trgsrc;
        do{
            rp_AcqGetTriggerSrc(&trgsrc);
        }while(trgsrc==RP_TRIG_SRC_AWG_PE);

        //when this condition is met the red pitaya will reset the triggerSrc to Disabled
        CHK_ERR(rp_DpinSetState(RP_LED0,RP_LOW));
        CHK_ERR(rp_DpinSetState(RP_LED1,RP_HIGH));
        //printf("triggered\n");

        uint32_t samplenum=ADCBUFFERSIZE;
        CHK_ERR(rp_AcqGetLatestDataV(RP_CH_1,&samplenum,acqbufferP));
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

        CHK_ERR(rp_AcqStart());
        CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));    //rearm trigger source
    }
	/* Releasing resources */
	free(network_acqbufferP);
    free(acqbufferP);
    //TODO delete mutexes, signal and destroy thread
    rp_Release();
    return 0;
}
