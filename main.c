#include <stdio.h>      //for fprintf
#include <stdint.h>     //for uint16_t
#include <stdlib.h>     //for malloc
#include <string.h>     //for memcpy
#include <unistd.h>     //for sleep
#include <time.h>       //for waiting for trigger timeout
#include <float.h>      //for FLT_MIN
#include "network_thread.h"
#include "redpitaya/rp.h"
#include "fft2g.h"

//Buffer size is 2^14 samples
//Sample rate is 125MSamples

//Red pitaya will trigger in the middle of the rising

//SLOW SCAN MODE IS CLOSE TO 0.5Hz
//time to fill full buffer is is (2^14)*(2^13)/(125e6)=1.073s
#define SLOW_DEC RP_DEC_8192
//chose freq so that fits exactely into twice the aquistion buffer 1/1.073s
#define SLOW_FREQ (0.931322f/2.0f)

//FAST SCAN MODE IS CLOSE TO 50Hz
//time to fill full buffer is is (2^14)*(2^6)/(125e6)=0.00838s
#define FAST_DEC RP_DEC_64
//chose freq so that fits exactely into twice the aquistion buffer 1/0.00838s
#define FAST_FREQ (119.209f/2.0f)

#define CHK_ERR(function)                                                   \
    do {                                                                    \
        int errorcode=function;                                             \
        if(errorcode){                                                      \
            printf("Error code %d occured in line %d\n",errorcode,__LINE__);\
        }                                                                   \
    } while (0)

#define CHK_ERR_ACT(function,action)                                        \
    do {                                                                    \
        int errorcode=function;                                             \
        if(errorcode){                                                      \
            printf("Error code %d occured in line %d\n",errorcode,__LINE__);\
            action;                                                         \
        }                                                                   \
    } while (0)

void SetGenerator(rp_channel_t ScanChannel, float ScanFrequency, float OtherChannelOffset);
double getDeltatimeS(void);
void sortPeaksX(uint16_t numOfPeaks,uint16_t* peaksxP,float* peaksyP);
void findPeaks(uint16_t numOfPoints,float* ydata,uint16_t numOfPeaks,uint16_t deadzoneSize,uint16_t* peaksx_returnp,float* peaksy_returnp);
void waitTrgUnarmAndGetData(rp_acq_trig_src_t last_trgsrc, float* acqbufferP, uint32_t numOfSamples);

//operation modes
void doLock(int firstrun,float* acqbufferP){
    if(firstrun){
        SetGenerator(RP_CH_1,FAST_FREQ,0.0);
        CHK_ERR(rp_AcqSetDecimation(FAST_DEC));
        CHK_ERR(rp_AcqSetTriggerDelay(ADCBUFFERSIZE/2));       //trigger will be centered around maximum of triangle

        CHK_ERR(rp_AcqStart());  //start aquisition into ringbuffer
        CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));
        CHK_ERR(rp_GenTriggerEventCondition(RP_GEN_TRIG_EVT_A_START));
    }
    waitTrgUnarmAndGetData(RP_TRIG_SRC_AWG_PE,acqbufferP,ADCBUFFERSIZE);
    //rearm aquisition
    CHK_ERR(rp_AcqStart());
    CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));

}

void doScan(int firstrun,float* acqbufferP){
    if(firstrun){
        //Setup Generator
        SetGenerator(RP_CH_1,FAST_FREQ,0.0);
        //Setup Aquisition
        CHK_ERR(rp_AcqSetDecimation(FAST_DEC));
        CHK_ERR(rp_AcqSetTriggerDelay(ADCBUFFERSIZE/2));
        //Start aquisition
        CHK_ERR(rp_AcqStart());  //start aquisition into ringbuffer
        CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));
        CHK_ERR(rp_GenTriggerEventCondition(RP_GEN_TRIG_EVT_A_START));
    }
    waitTrgUnarmAndGetData(RP_TRIG_SRC_AWG_PE,acqbufferP,ADCBUFFERSIZE);
    //rearm aquisition
    CHK_ERR(rp_AcqStart());
    CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));
}

#define MaxNumOfPeaks_Scan_Cav 10
#define MaxNumOfPeaks_Scan_Grat 10
#define DeadzoneSamplepoints_Scan_Cav 100
#define DeadzoneSamplepoints_Scan_Grat 100
#define PeakDiscardFactor 0.6
#define NumScanSteps 1000
enum {char_step_scan_cav,char_step_scan_grat,char_step_scan_cav_check_range,char_step_scan_cav_mv_grat,char_step_finished};
void doCharacterise(int firstrun,float* acqbufferP){
    static int CharacterisationStep=char_step_scan_cav;
    static uint16_t* peaksx_scan_cav_P;
    static float* peaksy_scan_cav_P;
    static uint16_t valid_peaks_scan_cav=0;
    static float cav_voltage_per_peakdist=0.0f;
    static float grat_voltage_per_peakdist=0.0f;
    if(firstrun){
        SetGenerator(RP_CH_1,SLOW_FREQ,0.0);
        CHK_ERR(rp_AcqSetDecimation(SLOW_DEC));
        CHK_ERR(rp_AcqSetTriggerDelay(ADCBUFFERSIZE/2));
        CHK_ERR(rp_AcqStart());  //this commands initiates the pitaya to start aquiring samples
        CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));
        CHK_ERR(rp_GenTriggerEventCondition(RP_GEN_TRIG_EVT_A_START));
    }
    waitTrgUnarmAndGetData(RP_TRIG_SRC_AWG_PE,acqbufferP,ADCBUFFERSIZE);
    CHK_ERR(rp_AcqStart());
    CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));    //rearm trigger source
    switch(CharacterisationStep){
        case char_step_scan_cav:
            //Setup generator for char_step_scan_grat
            SetGenerator(RP_CH_2,SLOW_FREQ,-1.0f);

            peaksx_scan_cav_P=(uint16_t*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(uint16_t));
            peaksy_scan_cav_P=(float*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(float));
            findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Cav,DeadzoneSamplepoints_Scan_Cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            sortPeaksX(MaxNumOfPeaks_Scan_Cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            for(uint16_t peak_idx=1;peak_idx<MaxNumOfPeaks_Scan_Cav;peak_idx++){    //ceck all other points except largest one if they are large enough
                if(peaksy_scan_cav_P[0]*PeakDiscardFactor<peaksy_scan_cav_P[peak_idx]){
                    valid_peaks_scan_cav++;
                    printf("Cavity scan peak at (%d,%f)\n",peaksx_scan_cav_P[peak_idx],peaksy_scan_cav_P[peak_idx]);
                }
            }
            for(uint16_t peak_idx=1;peak_idx<valid_peaks_scan_cav;peak_idx++){
                //first factor is scan range of 2Vpp, the other part is delta_samples_between_peaks/total_num_samples_per_rising edge
                cav_voltage_per_peakdist+=2.0f*((float)(peaksx_scan_cav_P[peak_idx]-peaksx_scan_cav_P[peak_idx-1]))/ADCBUFFERSIZE;
            }
            cav_voltage_per_peakdist/=valid_peaks_scan_cav;
            //TODO set cav_voltage_per_peakdist
            CharacterisationStep=char_step_scan_grat;

            break;
        case char_step_scan_grat: ;//need this here
            uint16_t valid_peaks_scan_grat=0;
            uint16_t* peaksx_scan_grat_P=(uint16_t*)malloc(MaxNumOfPeaks_Scan_Grat*sizeof(uint16_t));
            float*    peaksy_scan_grat_P=(float*)malloc(MaxNumOfPeaks_Scan_Grat*sizeof(float));
            findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Grat,DeadzoneSamplepoints_Scan_Grat,peaksx_scan_grat_P,peaksy_scan_grat_P);
            sortPeaksX(MaxNumOfPeaks_Scan_Grat,peaksx_scan_grat_P,peaksy_scan_grat_P);
            printf("Grating scan peak at (%d,%f)\n",peaksx_scan_grat_P[0],peaksy_scan_grat_P[0]);
            for(uint16_t peak_idx=1;peak_idx<MaxNumOfPeaks_Scan_Grat;peak_idx++){    //ceck all other points except largest one if they are large enough
                if(peaksy_scan_grat_P[0]*PeakDiscardFactor<peaksy_scan_grat_P[peak_idx]){
                    valid_peaks_scan_grat++;
                    printf("Grating scan peak at (%d,%f)\n",peaksx_scan_grat_P[peak_idx],peaksy_scan_grat_P[peak_idx]);
                }
            }
            for(uint16_t peak_idx=1;peak_idx<valid_peaks_scan_grat;peak_idx++){
                //first factor is scan range of 2Vpp, the other part is delta_samples_between_peaks/total_num_samples_per_rising edge
                grat_voltage_per_peakdist+=2.0f*((float)(peaksx_scan_grat_P[peak_idx]-peaksx_scan_grat_P[peak_idx-1]))/ADCBUFFERSIZE;
            }
            grat_voltage_per_peakdist/=valid_peaks_scan_grat;
            free(peaksx_scan_grat_P);
            free(peaksy_scan_grat_P);
            //Setup generator for char_step_scan_cav_check_range
            CharacterisationStep=char_step_scan_cav_check_range;
            SetGenerator(RP_CH_1,SLOW_FREQ,-1.0f+grat_voltage_per_peakdist);
            break;
        case char_step_scan_cav_check_range:


            //Setup generator for char_step_scan_cav_mv_grat
            CharacterisationStep=char_step_scan_cav_mv_grat;
            SetGenerator(RP_CH_1,SLOW_FREQ,grat_voltage_per_peakdist);
            break;
        case char_step_scan_cav_mv_grat:

            //TODO IF ALL SCANS ARE DONE CharacterisationStep=char_step_finished;
            break;
        case char_step_finished:
            free(peaksx_scan_cav_P);
            free(peaksy_scan_cav_P);
            break;
    }
}

void packRealIntoFFTcomplex(float* ReInP, float* ReCoOutP){
    for(int i=0;i<ADCBUFFERSIZE;i++){
        ReCoOutP[2*i]=ReInP[i];
        ReCoOutP[2*i+1]=0.0f;
    }
}

void doCorrelation(float* fft_in_outP, float* corr_luP,int* ooura_fft_ipP, float* ooura_fft_wP){
    //normalise
    float median=0;
    for(int i=0;i<ADCBUFFERSIZE;i++){
        median+=fft_in_outP[2*i]/ADCBUFFERSIZE;
    }
    for(int i=0;i<ADCBUFFERSIZE;i++){
        fft_in_outP[2*i]-=median;
    }
    //Do fft
    cdft(ADCBUFFERSIZE*2,1,in_and_outP,ooura_fft_ipP,ooura_fft_wP);
    //multiply for correlation
    for(int i=0;i<ADCBUFFERSIZE;i++){
        float temp=fft_in_outP[2*i]*ooura_fft_ipP[2*i]-fft_in_outP[2*i+1]*ooura_fft_ipP[2*i+1];
        fft_in_outP[2*i+1]=fft_in_outP[2*i]*ooura_fft_ipP[2*i+1]+fft_in_outP[2*i+1]*ooura_fft_ipP[2*i];
        fft_in_outP[2*i]=temp;
    }
    //Do ifft
    cdft(ADCBUFFERSIZE*2,-1,in_and_outP,ooura_fft_ipP,ooura_fft_wP);
    return;
}

int main(int argc, char **argv){

    CHK_ERR(rp_Init());

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
    float* network_acqbufferP=(float*)malloc(ADCBUFFERSIZE*sizeof(float));
    uint32_t new_operation_mode=operation_mode_scan;


    //initialize interprocess communication sturct
    struct threadinfo threadinf;

    threadinf.mutex_rawdata_bufferP=&mutex_network_acqbuffer;
    threadinf.mutex_settingsP=&mutex_settings;
    threadinf.mutex_new_operation_modeP=&mutex_new_operation_mode;

    threadinf.new_operation_modeP=&new_operation_mode;
    threadinf.condidion_mainthread_finished_memcpyP=&condidion_mainthread_finished_memcpy;
    threadinf.network_acqBufferP=network_acqbufferP;

    //create thread
    thrd_t networkingThread;
    if(thrd_success!=thrd_create(&networkingThread,thrd_startServer,(void*)&threadinf)){
        exit(1);
    }

    //create storage for methods
    float* acqbufferP=(float*) malloc(ADCBUFFERSIZE*sizeof(float));
    int* ooura_fft_ipP=(int*) malloc(sqrt(ADCBUFFERSIZE)*sizeof(int));
    float* ooura_fft_wP=(float*) malloc(ADCBUFFERSIZE/2*sizeof(float));
    //lookup storage for correlation
    float* fft_in_outP=(float*) malloc(2*ADCBUFFERSIZE*sizeof(float));
    float* corr_luP=(float*) malloc(2*ADCBUFFERSIZE*sizeof(float));
    //initialize fft lookup tables
    makewt(ADCBUFFERSIZE>>2,ooura_fft_ipP,ooura_fft_wP);

    //startup
    CHK_ERR(rp_AcqReset());
    printf("do scan\n");
    doScan(1,acqbufferP);

    while(1){
        static uint32_t last_operation_mode=operation_mode_scan;

        //check if a new mode of operation is requested
        mtx_lock(&mutex_new_operation_mode);
        if(last_operation_mode!=new_operation_mode){
            last_operation_mode=new_operation_mode;
            switch(new_operation_mode){
                case operation_mode_scan:
                    printf("sw scan\n");
                    doScan(1,acqbufferP);
                break;
                case operation_mode_characterise:
                    printf("sw char\n");
                    doCharacterise(1,acqbufferP);
                break;
                case operation_mode_lock:
                    printf("Not ready\n");
                break;
                default:
                    printf("Error invalid opmode %d",new_operation_mode);
                break;
            }
            mtx_unlock(&mutex_new_operation_mode);
        }else{
            mtx_unlock(&mutex_new_operation_mode);
            switch(last_operation_mode){
                case operation_mode_scan:
                    doScan(0,acqbufferP);
                break;
                case operation_mode_characterise:
                    doCharacterise(0,acqbufferP);
                break;
                case operation_mode_lock:
                    printf("Not ready\n");
                break;
                default:
                    printf("Error invalid opmode %d",new_operation_mode);
                break;
            }

        }

        //Check if network thread is requesting data
        int ret=mtx_trylock(&mutex_network_acqbuffer);
        if(ret==thrd_success){
            //networking thread wants us to copy data into buffer, copy data
            memcpy(network_acqbufferP,acqbufferP,sizeof(float)*ADCBUFFERSIZE);
            if(thrd_success!=mtx_unlock(&mutex_network_acqbuffer)){
                exit(1);
            }
            //inform the thread that we are finished
            cnd_signal(&condidion_mainthread_finished_memcpy);
        }else if(ret!=thrd_busy){   //proceed normally on thrd_busy
            exit(1);
        }
    }
	/* Releasing resources */
	free(network_acqbufferP);
    free(acqbufferP);
    //TODO delete mutexes, signal and destroy thread
    rp_Release();
    return 0;
}

void SetGenerator(rp_channel_t ScanChannel, float ScanFrequency, float OtherChannelOffset){
    CHK_ERR(rp_GenWaveform(ScanChannel, RP_WAVEFORM_ARBITRARY));
    float* wavetable=(float*)malloc(ADCBUFFERSIZE*sizeof(float));
    for(uint16_t index=0;index<ADCBUFFERSIZE;index++){
        if(index<ADCBUFFERSIZE/2){
            wavetable[index]=-1.0f+index*4.0f/ADCBUFFERSIZE;
        }else{
            wavetable[index]=3.0f-index*4.0f/ADCBUFFERSIZE;
        }
    }
    CHK_ERR(rp_GenArbWaveform(ScanChannel, wavetable, ADCBUFFERSIZE));
    free(wavetable);
    CHK_ERR(rp_GenFreq(ScanChannel,ScanFrequency));
    CHK_ERR(rp_GenAmp(ScanChannel,1.0));
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

void waitTrgUnarmAndGetData(rp_acq_trig_src_t last_trgsrc, float* acqbufferP, uint32_t numOfSamples){

    CHK_ERR(rp_DpinSetState(RP_LED0,RP_HIGH));
    CHK_ERR(rp_DpinSetState(RP_LED1,RP_LOW));
    rp_acq_trig_src_t trgsrc;
    //TODO check if this triggers instantly and if so we took to long with out last data processing
    do{
        rp_AcqGetTriggerSrc(&trgsrc);
    }while(trgsrc==last_trgsrc);
    //when this condition is met the red pitaya will reset the triggerSrc to Disabled
    CHK_ERR(rp_DpinSetState(RP_LED0,RP_LOW));
    CHK_ERR(rp_DpinSetState(RP_LED1,RP_HIGH));

    CHK_ERR(rp_AcqGetLatestDataV(RP_CH_1,&numOfSamples,acqbufferP));
    if(numOfSamples!=ADCBUFFERSIZE){
        printf("Missing DATA\n");
    }
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

void sortPeaksX(uint16_t numOfPeaks,uint16_t* peaksxP,float* peaksyP){
    int swapped_flag;
    uint16_t numOfSwaps=numOfPeaks;
    do{
        swapped_flag=0;
        for(uint16_t bubble_offset=0;bubble_offset<numOfSwaps-1;bubble_offset++){
            if(peaksxP[bubble_offset+1]<peaksxP[bubble_offset]){
                //swap peaksx
                uint16_t tempx=peaksxP[bubble_offset];
                peaksxP[bubble_offset]=peaksxP[bubble_offset+1];
                peaksxP[bubble_offset+1]=tempx;
                //swap peaksy
                float tempy=peaksyP[bubble_offset];
                peaksyP[bubble_offset]=peaksyP[bubble_offset+1];
                peaksyP[bubble_offset+1]=tempy;
            }
            numOfSwaps--;
        }
    }while(swapped_flag);
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
            if(besty<ydata[sampleNum]){
                besty=ydata[sampleNum];
                bestx=sampleNum;
            }
        }
        peaksx_returnp[peakNum]=bestx;
        peaksy_returnp[peakNum]=besty;
    }
    return;
}
