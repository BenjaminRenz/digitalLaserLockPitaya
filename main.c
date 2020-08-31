#include <stdio.h>      //for fprintf
#include <stdint.h>     //for uint16_t
#include <stdlib.h>     //for malloc
#include <string.h>     //for memcpy
#include <unistd.h>     //for sleep
#include <time.h>       //for waiting for trigger timeout
#include <float.h>      //for FLT_MIN
#include <math.h>       //for fabsf
#include "network_thread.h"
#include "redpitaya/rp.h"
#include "fft4g.h"

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

struct fpointData{
    uint32_t numOfValidPeaks;
    float* peaksxP;
    float* peaksyP;
};

void                SetGenerator(rp_channel_t ScanChannel, float ScanFrequency, float OtherChannelOffset);
void                printPeaks(struct fpointData peakdat);
float               get_x_median_peakdist(struct fpointData dataIn);
double              getDeltatimeS(void);
struct fpointData   samplePeakNtime(uint32_t SampleOverNbuffers,uint32_t numOfPeaksPerScan,uint32_t peaksForValidCluster,uint32_t numOfPeaksToAverageForUpperLimit,float deadzonePeakwidth,float peakvalidMulti,struct fpointData rawDataInP);
void                sortPeaksX(struct fpointData peakdat);
void                sortPeaksY(struct fpointData peakdat);
struct              fpointData findPeaks(struct fpointData inputdat,uint32_t maxNumOfPeaksToFind,float deadzoneSize);
void                waitTrgUnarmAndGetData(rp_acq_trig_src_t last_trgsrc, float* acqbufferP, uint32_t numOfSamples);
void                packRealIntoFFTcomplex(float* ReInP, float* ReCoOutP);
void                doCorrelation(float* fft_in_outP, float* corr_luP,int* ooura_fft_ipP, float* ooura_fft_wP);




//operation modes
void doLock(int firstrun,float* acqbufferP){
    if(firstrun){
        CHK_ERR(rp_AcqReset());
        //Setup Generator
        SetGenerator(RP_CH_1,FAST_FREQ,0.0);
        //Setup Aquisition
        CHK_ERR(rp_AcqSetDecimation(FAST_DEC));
        CHK_ERR(rp_AcqSetTriggerDelay(ADCBUFFERSIZE/2));       //trigger will be centered around maximum of triangle
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

void doScan(int firstrun,float* acqbufferP,rp_channel_t ScanChannel,float offsetOtherChannel){
    if(firstrun){
        CHK_ERR(rp_AcqReset());
        //Setup Generator
        //SetGenerator(ScanChannel,FAST_FREQ,offsetOtherChannel);
        SetGenerator(ScanChannel,FAST_FREQ,offsetOtherChannel);
        //Setup Aquisition
        //CHK_ERR(rp_AcqSetDecimation(FAST_DEC));
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
#define DeadzoneSamplesWidth_Scan 20.0f
#define DeadzoneVoltageWidth_Scan (DeadzoneSamplesWidth_Scan/(float)ADCBUFFERSIZE)
#define PeakDiscardFactor 0.65f
enum {
    char_step_scan_cav,                 //initial scan of the cavity to determine cav_voltage_per_peakdist
    char_step_scan_grat,                //initial scan of the laser to determine grat_voltage_per_peakdist
    char_step_scan_direction,           //scan moving the grating 1/4 * grat_voltage_per_peakdist to check movement direction
    char_step_scan_finetune,            //rescan cavity to check if applying grat_voltage_per_peakdist as offset returns the same image as the initial scan (if not correct offset)
    char_step_scan_cav_mv_grat,         //scan cavity 1000 times while slowly varying the grating voltage from -grat_voltage_per_peakdist/2 to grat_voltage_per_peakdist/2
    char_step_finished,
    char_step_testhack
};

void doCharacterise(int firstrun,float* voltageBufferP,float* acqbufferP,struct threadinfo* threadinfP){
    //TODO remove this hack, just used for testing
    static int CharacterisationStep=char_step_scan_cav;
    
    static struct fpointData lastPeaks;
    static float cav_voltage_per_peakdist;
    static float grat_voltage_per_peakdist;


    if(firstrun){
        CHK_ERR(rp_AcqReset());
        SetGenerator(RP_CH_1,FAST_FREQ,-1.0f);
        CHK_ERR(rp_AcqSetDecimation(FAST_DEC));
        CHK_ERR(rp_AcqSetTriggerDelay(ADCBUFFERSIZE/2));
        CHK_ERR(rp_AcqStart());  //this commands initiates the pitaya to start aquiring samples
        CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));
        CHK_ERR(rp_GenTriggerEventCondition(RP_GEN_TRIG_EVT_A_START));
    }
    waitTrgUnarmAndGetData(RP_TRIG_SRC_AWG_PE,acqbufferP,ADCBUFFERSIZE);
    CHK_ERR(rp_AcqStart());
    CHK_ERR(rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE));    //rearm trigger source
    struct fpointData ADCoutput;
    ADCoutput.peaksxP=voltageBufferP;
    ADCoutput.peaksyP=acqbufferP;
    ADCoutput.numOfValidPeaks=ADCBUFFERSIZE;
    switch(CharacterisationStep){
        case char_step_scan_cav:
            lastPeaks=samplePeakNtime(10,MaxNumOfPeaks_Scan_Cav,7,10,DeadzoneVoltageWidth_Scan,PeakDiscardFactor,ADCoutput);
            if(!lastPeaks.numOfValidPeaks){
                return;     //samplePeakNtime did not recieve enough samples yet, so repeat
            }
            printPeaks(lastPeaks);
            if(lastPeaks.numOfValidPeaks<2){
                printf("Error, not enough peaks found during the first scan of the cavity, will repeat scan.\n");
                free(lastPeaks.peaksxP);
                free(lastPeaks.peaksyP);
                return;
            }
            cav_voltage_per_peakdist=get_x_median_peakdist(lastPeaks);
            //do not free lastPeaks.peaksxP or lastPeaks.peaksyP, they are needed by char_step_scan_direction and char_step_scan_finetune
            printf("cav_voltage_per_peakdist %f\n",cav_voltage_per_peakdist);
            CharacterisationStep=char_step_scan_direction;
            SetGenerator(RP_CH_1,FAST_FREQ,-1.0f);
        break;
        #define starting_voltagestep_scan_direction 2.0f/128.0f
        #define deviation_of_dirpos 0.05f
        case char_step_scan_direction: ;    //Scan until the peak lands into the +/- 1/3 peakdist of one of the peaks
            static float voltagestep_scan_direction=starting_voltagestep_scan_direction;
            static uint32_t current_scan_dir_run=0;
            struct fpointData newPeaks=samplePeakNtime(10,MaxNumOfPeaks_Scan_Cav,7,10,DeadzoneVoltageWidth_Scan,PeakDiscardFactor,ADCoutput);
            if(!newPeaks.numOfValidPeaks){
                return;     //samplePeakNtime needs to be called again if it returns NULL, so repeat
            }
            //Check if the peaks landed in between (0.33 or -0.33)*cav_voltage_per_peakdist of the lastPeak position
            //Average over all peak distances
            float average=0.0f;
            for(uint32_t peak_old_idx=0;peak_old_idx<lastPeaks.numOfValidPeaks;peak_old_idx++){
                for(uint32_t peak_new_idx=0;peak_new_idx<newPeaks.numOfValidPeaks;peak_new_idx++){
                    average+=fmodf(newPeaks.peaksxP[peak_new_idx]-newPeaks.peaksxP[peak_old_idx],cav_voltage_per_peakdist);
                }
            }
            average/=(float)lastPeaks.numOfValidPeaks*(float)newPeaks.numOfValidPeaks;
            free(newPeaks.peaksxP);
            free(newPeaks.peaksyP);
            if(0.3333333f-deviation_of_dirpos<fabs(average)&&fabs(average)<0.3333333f+deviation_of_dirpos){
                grat_voltage_per_peakdist=copysignf(1.0f, average)*3.0f*(float)current_scan_dir_run*voltagestep_scan_direction;//positive is increasing voltage moves peaks to the right
                CharacterisationStep=char_step_scan_finetune;
                printf("found peak in around +/- 0.333, resulting grat_voltage_per_peakdist: %f\n",grat_voltage_per_peakdist);
                SetGenerator(RP_CH_1,FAST_FREQ,-1.0f+grat_voltage_per_peakdist);
            }else if(0.3333333f+deviation_of_dirpos<fabs(average)){   //overshoot repeat whole procedure with finer scan steps
                voltagestep_scan_direction/=2.0f;
                current_scan_dir_run=0;
                printf("Overshoot while scanning reducing scan steps to %f\n",voltagestep_scan_direction);
                SetGenerator(RP_CH_1,FAST_FREQ,-1.0f);
            }else{
                printf("Peak has not yet moved into area yet, distance was %f and should be +/-0.3333333f\n",average);
                SetGenerator(RP_CH_1,FAST_FREQ,-1.0f+(float)current_scan_dir_run*voltagestep_scan_direction);
            }
        break;
        #define finetuneTresholdAccept 0.01f
        case char_step_scan_finetune:;{
            struct fpointData newPeaks=samplePeakNtime(10,MaxNumOfPeaks_Scan_Cav,7,10,DeadzoneVoltageWidth_Scan,PeakDiscardFactor,ADCoutput);
            if(!newPeaks.numOfValidPeaks){
                return;     //samplePeakNtime needs to be called again if it returns NULL, so repeat
            }
            //Check if the peaks lands on the peaks of lastPeak
            //Average over all peak distances
            float average=0.0f;
            for(uint32_t peak_old_idx=0;peak_old_idx<lastPeaks.numOfValidPeaks;peak_old_idx++){
                for(uint32_t peak_new_idx=0;peak_new_idx<newPeaks.numOfValidPeaks;peak_new_idx++){
                    average+=fmodf(newPeaks.peaksxP[peak_new_idx]-newPeaks.peaksxP[peak_old_idx],cav_voltage_per_peakdist);
                }
            }
            average/=(float)lastPeaks.numOfValidPeaks*(float)newPeaks.numOfValidPeaks;
            free(newPeaks.peaksxP);
            free(newPeaks.peaksyP);
            if(fabs(average)<finetuneTresholdAccept){
                free(lastPeaks.peaksxP);
                free(lastPeaks.peaksyP);
                CharacterisationStep=char_step_scan_cav_mv_grat;
                SetGenerator(RP_CH_1,FAST_FREQ,-grat_voltage_per_peakdist/2.0f);
                printf("delta of expected and measured grat_voltage_per_peakdist was %f, and accepted\n",average);
            }else{
                printf("delta of grat_voltage_per_peakdist did deviate to much %f, repeat\n",average);
                //try to adjust grat_voltage_per_peakdist
                grat_voltage_per_peakdist=grat_voltage_per_peakdist*average/(average+cav_voltage_per_peakdist);
                SetGenerator(RP_CH_1,FAST_FREQ,-1.0f+grat_voltage_per_peakdist);
            }
        }
        break;
        #define peakAppearDisappearAllowedDeviation 0.1f
        #define numOfStepsMvGrat 100.0f
        case char_step_scan_cav_mv_grat:;{
            static uint32_t scannum=0;
            static  int32_t idx_offset=0;
            static struct fpointData lastPeaks;
            static struct fpointData charOutput;
            if(scannum==0){
                lastPeaks=samplePeakNtime(10,MaxNumOfPeaks_Scan_Cav,7,10,DeadzoneVoltageWidth_Scan,PeakDiscardFactor,ADCoutput);
                if(!lastPeaks.numOfValidPeaks){
                    return;     //samplePeakNtime needs to be called again if it returns NULL, so repeat
                }
                
                charOutput.peaksxP=(float*)malloc((uint32_t)numOfStepsMvGrat*MaxNumOfPeaks_Scan_Cav*sizeof(float));
                charOutput.peaksyP=(float*)malloc((uint32_t)numOfStepsMvGrat*MaxNumOfPeaks_Scan_Cav*sizeof(float));
                charOutput.numOfValidPeaks=0;
                for(uint32_t peakidx=0;peakidx<lastPeaks.numOfValidPeaks;peakidx++){
                    charOutput.peaksxP[charOutput.numOfValidPeaks+peakidx]=(float)peakidx;
                    charOutput.peaksyP[charOutput.numOfValidPeaks+peakidx]=lastPeaks.peaksxP[peakidx];
                }
                charOutput.numOfValidPeaks+=lastPeaks.numOfValidPeaks;
                scannum++;
                SetGenerator(RP_CH_1,FAST_FREQ,-grat_voltage_per_peakdist/2.0f+(float)scannum*grat_voltage_per_peakdist/numOfStepsMvGrat);
                return;
            }
            struct fpointData newPeaks=samplePeakNtime(10,MaxNumOfPeaks_Scan_Cav,7,10,DeadzoneVoltageWidth_Scan,PeakDiscardFactor,ADCoutput);
            if(!newPeaks.numOfValidPeaks){
                return;     //samplePeakNtime needs to be called again if it returns NULL, so repeat
            }
            //check if a new peak has entered from the left
            float distanceFirstPeaks=(newPeaks.peaksxP[0]-lastPeaks.peaksxP[0])/cav_voltage_per_peakdist;
            printf("normalised distance from leftmost peak last and current measurement %f\n",distanceFirstPeaks);
            if(fabsf(distanceFirstPeaks)<peakAppearDisappearAllowedDeviation){
                printf("No new peak appeared or disappeared on the left\n");
            }else if(1.0f-peakAppearDisappearAllowedDeviation<fabsf(distanceFirstPeaks)
                  && fabsf(distanceFirstPeaks)<peakAppearDisappearAllowedDeviation+1.0f){
                printf("leftmost peak dissapeared or reappeared\n");
                idx_offset+=(int)copysignf(1.0f, distanceFirstPeaks);
                printf("correction of index is now at %d\n",idx_offset);
            }else{
                printf("somthing strange happened! repeat measurement\n");
                return;
            }
            //append peaks with new indices
            for(uint32_t peakidx=0;peakidx<newPeaks.numOfValidPeaks;peakidx++){
                charOutput.peaksxP[(uint32_t)(charOutput.numOfValidPeaks+peakidx)]=(float)((int32_t)peakidx+idx_offset)+(float)scannum/numOfStepsMvGrat;
                charOutput.peaksyP[(uint32_t)(charOutput.numOfValidPeaks+peakidx)]=newPeaks.peaksxP[peakidx];
            }
            charOutput.numOfValidPeaks+=newPeaks.numOfValidPeaks;
            free(lastPeaks.peaksxP);
            free(lastPeaks.peaksyP);
            lastPeaks=newPeaks;
            scannum++;
            
            //Check if we are finished yet
            if(scannum<(uint32_t)numOfStepsMvGrat){
                CHK_ERR(rp_GenOffset(RP_CH_2, -grat_voltage_per_peakdist/2.0f+(float)scannum*grat_voltage_per_peakdist/numOfStepsMvGrat));
            }else{
                //cleanup
                free(lastPeaks.peaksxP);
                free(lastPeaks.peaksyP);
                CharacterisationStep=char_step_finished;
                printf("got %d valid points\n",charOutput.numOfValidPeaks);

                //send data to thread
                mtx_lock(&threadinfP->mutex_network_characterization);
                threadinfP->network_numOfCharacterizationPoints=charOutput.numOfValidPeaks;
                threadinfP->network_characterisationXP=charOutput.peaksxP;
                threadinfP->network_characterisationYP=charOutput.peaksyP;
                mtx_unlock(&threadinfP->mutex_network_characterization);

                //exit the characerization mode, by pretending that the client has selected scan_cav
                mtx_lock(&threadinfP->mutex_network_operation_mode);
                threadinfP->network_operation_mode=operation_mode_scan_cav;
                mtx_unlock(&threadinfP->mutex_network_operation_mode);
            }
        }break;
        case char_step_finished:
            return;
        break;
    }
    return;
}





int main(int argc, char **argv){

    CHK_ERR(rp_Init());

    //create inter-thread communication struct in heap memory
    struct threadinfo* threadinfP=(struct threadinfo*)malloc(sizeof(struct threadinfo));

    //create mutex
    //"using pthreads.h it would be sufficient to pass pointers to mutexes on the main thread's stack, but this is implementation dependent, so I'm using the heap allocated inter-thread commuication struct instead.W
    //TODO check against thrd_success and not 0
    CHK_ERR_ACT(mtx_init(&threadinfP->mutex_network_acqBuffer,mtx_plain),exit(1));
    CHK_ERR_ACT(mtx_init(&threadinfP->mutex_network_operation_mode,mtx_plain),exit(1));
    CHK_ERR_ACT(mtx_init(&threadinfP->mutex_network_settings,mtx_plain),exit(1));
    CHK_ERR_ACT(mtx_init(&threadinfP->mutex_network_characterization,mtx_plain),exit(1));

    //create condition
    if(thrd_success!=cnd_init(&threadinfP->condidion_mainthread_finished_memcpy)){
        exit(1);
    }

    //initialize pointers in interprocess communication sturct
    threadinfP->network_operation_mode=operation_mode_scan_cav;
    threadinfP->network_acqBufferP=(float*)malloc(ADCBUFFERSIZE*sizeof(float));
    threadinfP->network_numOfCharacterizationPoints=0;
    threadinfP->network_characterisationXP=NULL;
    threadinfP->network_characterisationYP=NULL;

    //create thread
    thrd_t networkingThread;
    if(thrd_success!=thrd_create(&networkingThread,thrd_startServer,(void*)threadinfP)){
        exit(1);
    }

    //create storage for methods
    float* acqbufferP=(float*) malloc(ADCBUFFERSIZE*sizeof(float));
    float* voltageBufferP=(float*) malloc(ADCBUFFERSIZE*sizeof(float));

    //initialize storage for voltagebuffer (floats from -1.0 to 1.0)
    for(uint32_t index=0;index<ADCBUFFERSIZE;index++){
        voltageBufferP[index]=((float)index*2.0f)/(ADCBUFFERSIZE)-1.0f;
    }

    //create fft storage and initialize
    int* ooura_fft_ipP=(int*) malloc(sqrt(ADCBUFFERSIZE)*sizeof(int));
    float* ooura_fft_wP=(float*) malloc(ADCBUFFERSIZE/2*sizeof(float));
    //lookup storage for correlation
    //TODO uncomment
    //float* fft_in_outP=(float*) malloc(2*ADCBUFFERSIZE*sizeof(float));
    //float* corr_luP=(float*) malloc(2*ADCBUFFERSIZE*sizeof(float));
    //initialize fft lookup tables
    makewt(ADCBUFFERSIZE>>2,ooura_fft_ipP,ooura_fft_wP);

    while(1){
        //TODO remove hack
        static uint32_t last_operation_mode=operation_mode_not_initialized;

        //check if a new mode of operation is requested
        mtx_lock(&threadinfP->mutex_network_operation_mode);
        if(last_operation_mode!=threadinfP->network_operation_mode){
            last_operation_mode=threadinfP->network_operation_mode;
            mtx_unlock(&threadinfP->mutex_network_operation_mode);
            if(last_operation_mode==operation_mode_shutdown){//not inside switch, so break will stop while(1) loop
                printf("Shutdown command reviced\n");
                break;
            }
            switch(last_operation_mode){
                case operation_mode_scan_cav:
                    printf("sw scan cav\n");
                    doScan(1,acqbufferP,RP_CH_1,0.0);
                break;
                case operation_mode_scan_lsr:
                    printf("sw scan lsr\n");
                    doScan(1,acqbufferP,RP_CH_2,-1.0);
                break;
                case operation_mode_characterise:
                    printf("sw char\n");
                    doCharacterise(1,voltageBufferP,acqbufferP,threadinfP);
                break;
                case operation_mode_lock:
                    printf("Not ready\n");
                break;
                default:
                    printf("Error invalid opmode %d",last_operation_mode);
                break;
            }
        }else{
            mtx_unlock(&threadinfP->mutex_network_operation_mode);
            switch(last_operation_mode){
               case operation_mode_scan_cav:
                    doScan(0,acqbufferP,RP_CH_1,0.0);
                break;
                case operation_mode_scan_lsr:
                    doScan(0,acqbufferP,RP_CH_2,-1.0);
                break;
                case operation_mode_characterise:
                    doCharacterise(0,voltageBufferP,acqbufferP,threadinfP);
                break;
                case operation_mode_lock:
                    printf("Not ready\n");
                break;
                default:
                    printf("Error invalid opmode %d",last_operation_mode);
                break;
            }

        }

        //Check if network thread is requesting graph data
        int ret=mtx_trylock(&threadinfP->mutex_network_acqBuffer);
        if(ret==thrd_success){
            //networking thread wants us to copy data into buffer, copy data
            memcpy(threadinfP->network_acqBufferP,acqbufferP,sizeof(float)*ADCBUFFERSIZE);
            if(thrd_success!=mtx_unlock(&threadinfP->mutex_network_acqBuffer)){
                exit(1);
            }
            //inform the thread that we are finished
            cnd_signal(&threadinfP->condidion_mainthread_finished_memcpy);
        }else if(ret!=thrd_busy){   //handle errors !=thrd_success or thrd_busy,
            exit(1);
        }
    }
	/* Releasing resources */
    //TODO delete mutexes and signals?
    printf("terminate network thread\n");
	thrd_join(networkingThread,NULL);
	free(threadinfP->network_acqBufferP);
    free(threadinfP);
    free(acqbufferP);
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
    CHK_ERR(rp_GenOffset(ScanChannel, 0.0f));
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
    CHK_ERR(rp_GenOffset(otherChannel, OtherChannelOffset));
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
    cdft(ADCBUFFERSIZE*2,1,fft_in_outP,ooura_fft_ipP,ooura_fft_wP);
    //multiply for correlation
    for(int i=0;i<ADCBUFFERSIZE;i++){
        float temp=fft_in_outP[2*i]*corr_luP[2*i]-fft_in_outP[2*i+1]*corr_luP[2*i+1];
        fft_in_outP[2*i+1]=fft_in_outP[2*i]*corr_luP[2*i+1]+fft_in_outP[2*i+1]*corr_luP[2*i];
        fft_in_outP[2*i]=temp;
    }
    //Do ifft
    cdft(ADCBUFFERSIZE*2,-1,fft_in_outP,ooura_fft_ipP,ooura_fft_wP);
    return;
}

void printPeaks(struct fpointData peakdat){
    printf("npeaks found: %d\n",peakdat.numOfValidPeaks);
    for(int peak=0;peak<peakdat.numOfValidPeaks;peak++){
        printf("Peak at x=\t%f\t, y=\t%f\t\n",peakdat.peaksxP[peak],peakdat.peaksyP[peak]);
    }
}

/*uint16_t get_num_valid_peaks(uint16_t numpeaks,float* peaksyP,float PeakDiscardTreshold,float average){
    uint16_t numOfValidPeaks=1; //the first peak is always in this selection
    for(uint16_t peak_idx=1;peak_idx<numpeaks;peak_idx++){    //ceck all other points except largest one if they are large enough
        if((peaksyP[0]-average)*PeakDiscardTreshold<(peaksyP[peak_idx]-average)){
            numOfValidPeaks++;
        }
    }
    return numOfValidPeaks;
}

int16_t get_least_peak_dist(uint16_t numpeaks1,uint16_t* peakx1P,uint16_t numpeaks2,uint16_t* peakx2P){
    int16_t smallestDeltaX=INT16_MAX;
    for(uint16_t peak_first_idx=0;peak_first_idx<numpeaks1;peak_first_idx++){
        for(uint16_t peak_second_idx=0;peak_second_idx<numpeaks2;peak_second_idx++){
            int16_t peakDeltaX=(int16_t)(((int16_t)peakx1P[peak_first_idx])-((int16_t)peakx2P[peak_second_idx]));
            if(abs(peakDeltaX)<abs(smallestDeltaX)){
                smallestDeltaX=peakDeltaX;
            }
        }
    }
    return smallestDeltaX;
}

float get_average(int numsamples,float* samplesY){
    float average=0.0f;
    for(int sample=0;sample<numsamples;sample++){
        average+=samplesY[sample]/(float)numsamples;
    }
    return average;
}*/

//When finished returned fpointData peakdatReturn.numOfValidPeaks will be != 0
struct fpointData samplePeakNtime(uint32_t SampleOverNbuffers,uint32_t numOfPeaksPerScan,uint32_t peaksForValidCluster,uint32_t numOfPeaksToAverageForUpperLimit,float deadzonePeakwidth,float peakvalidMulti,struct fpointData rawDataInP){
    static uint32_t runnumber=0;
    static struct fpointData peakdatReturn;
    if(!runnumber){ //firstrun
        peakdatReturn.peaksxP=(float*)malloc(SampleOverNbuffers*numOfPeaksPerScan*sizeof(float));
        peakdatReturn.peaksyP=(float*)malloc(SampleOverNbuffers*numOfPeaksPerScan*sizeof(float));
        peakdatReturn.numOfValidPeaks=0;
    }
    struct fpointData PeakdataCurrentRun=findPeaks(rawDataInP,numOfPeaksPerScan,deadzonePeakwidth);
    memcpy(peakdatReturn.peaksxP+numOfPeaksPerScan*runnumber,PeakdataCurrentRun.peaksxP,sizeof(float)*PeakdataCurrentRun.numOfValidPeaks);
    memcpy(peakdatReturn.peaksyP+numOfPeaksPerScan*runnumber,PeakdataCurrentRun.peaksyP,sizeof(float)*PeakdataCurrentRun.numOfValidPeaks);
    free(PeakdataCurrentRun.peaksxP);
    free(PeakdataCurrentRun.peaksyP);
    if(runnumber==SampleOverNbuffers-1){
        //lastrun evaluate results
        //get y treshold
        sortPeaksY(peakdatReturn);
        float averageUpperLimit=0;
        for(uint32_t peak=0;peak<numOfPeaksToAverageForUpperLimit;peak++){
            averageUpperLimit+=peakdatReturn.peaksyP[SampleOverNbuffers*numOfPeaksPerScan-(1+peak)];
        }
        float tresholdForValidPeaks=(averageUpperLimit/(float)numOfPeaksToAverageForUpperLimit)*peakvalidMulti;
        //sort out all peaks under the y-treshold by asigning a high x-value to them, they will get sorted out by the next xsort
        uint32_t invalidatedPeaks=0;
        for(uint32_t peak=0;peak<SampleOverNbuffers*numOfPeaksPerScan;peak++){
            if(peakdatReturn.peaksyP[peak]<tresholdForValidPeaks){
                peakdatReturn.peaksxP[peak]=FLT_MAX;
                invalidatedPeaks++;
            }
        }
        sortPeaksY(peakdatReturn);
        peakdatReturn.numOfValidPeaks=SampleOverNbuffers*numOfPeaksPerScan-invalidatedPeaks;
        //identify clusters of peaks
        //TODO check if abort condition makes sense
        uint32_t foundClusters=0;
        for(uint32_t startpeak=0;startpeak<peakdatReturn.numOfValidPeaks-peaksForValidCluster;startpeak++){
            if(peakdatReturn.peaksxP[startpeak+peaksForValidCluster]-peakdatReturn.peaksxP[startpeak]<deadzonePeakwidth){
                //cluster is valid, so get all points that belong to it
                uint32_t validPeaksInCluster=peaksForValidCluster;
                for(uint32_t endpeak=startpeak+validPeaksInCluster;endpeak<peakdatReturn.numOfValidPeaks;endpeak++){
                    if((peakdatReturn.peaksxP[endpeak]-peakdatReturn.peaksxP[startpeak])>=deadzonePeakwidth){
                        break;
                    }else{
                        validPeaksInCluster++;
                    }
                }
                //got all peaks that belong to that cluster, so average over this cluster to get average reading for xposition
                float xAverage=0.0f;
                float yAverage=0.0f;
                for(uint32_t peakAverage=0;peakAverage<validPeaksInCluster;peakAverage++){
                    xAverage+=peakdatReturn.peaksxP[startpeak+peakAverage];
                    yAverage+=peakdatReturn.peaksyP[startpeak+peakAverage];
                }
                xAverage/=(float)validPeaksInCluster;
                yAverage/=(float)validPeaksInCluster;
                //write back into the peakdatReturn struct
                peakdatReturn.peaksxP[foundClusters]=xAverage;
                peakdatReturn.peaksyP[foundClusters]=yAverage;
                foundClusters++;
                //TODO check if this makes any sense
                startpeak+=validPeaksInCluster-1;
            }

        }
        peakdatReturn.numOfValidPeaks=foundClusters;
    }
    return peakdatReturn;
}

float get_x_median_peakdist(struct fpointData dataIn){
    float median_x_peakdist=0.0f;
    for(uint32_t peak_idx=0;peak_idx<(dataIn.numOfValidPeaks-1);peak_idx++){
        float peakdistx=dataIn.peaksxP[peak_idx+1]-dataIn.peaksxP[peak_idx];
        printf("last peakdist was %f\n",peakdistx);
        median_x_peakdist+=peakdistx;
    }
    median_x_peakdist/=(float)(dataIn.numOfValidPeaks-1);
    return median_x_peakdist;
}

/*int16_t sgn(int16_t x){
    if(x<0){
        return -1;
    }
    return 1;
}*/


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

//ascending order
void sortPeaksX(struct fpointData peakdat){
    int swapped_flag;
    uint32_t numOfSwaps=peakdat.numOfValidPeaks;
    do{
        swapped_flag=0;
        for(uint32_t bubble_offset=0;bubble_offset<numOfSwaps-1;bubble_offset++){
            if(peakdat.peaksxP[bubble_offset+1]<peakdat.peaksxP[bubble_offset]){
                swapped_flag=1;
                //swap peaksx
                float tempx=peakdat.peaksxP[bubble_offset];
                peakdat.peaksxP[bubble_offset]=peakdat.peaksxP[bubble_offset+1];
                peakdat.peaksxP[bubble_offset+1]=tempx;
                //swap peaksy
                float tempy=peakdat.peaksyP[bubble_offset];
                peakdat.peaksyP[bubble_offset]=peakdat.peaksyP[bubble_offset+1];
                peakdat.peaksyP[bubble_offset+1]=tempy;
            }
        }
        numOfSwaps--;
    }while(swapped_flag);
}

//ascending order
void sortPeaksY(struct fpointData peakdat){
    int swapped_flag;
    uint32_t numOfSwaps=peakdat.numOfValidPeaks;
    do{
        swapped_flag=0;
        for(uint32_t bubble_offset=0;bubble_offset<numOfSwaps-1;bubble_offset++){
            if(peakdat.peaksyP[bubble_offset+1]<peakdat.peaksyP[bubble_offset]){
                swapped_flag=1;
                //swap peaksx
                float tempx=peakdat.peaksxP[bubble_offset];
                peakdat.peaksxP[bubble_offset]=peakdat.peaksxP[bubble_offset+1];
                peakdat.peaksxP[bubble_offset+1]=tempx;
                //swap peaksy
                float tempy=peakdat.peaksyP[bubble_offset];
                peakdat.peaksyP[bubble_offset]=peakdat.peaksyP[bubble_offset+1];
                peakdat.peaksyP[bubble_offset+1]=tempy;
            }
        }
        numOfSwaps--;
    }while(swapped_flag);
}

struct fpointData findPeaks(struct fpointData inputdat,uint32_t maxNumOfPeaksToFind,float deadzoneSize){
    struct fpointData peakdat_ret;
    peakdat_ret.peaksxP=(float*)malloc(maxNumOfPeaksToFind*sizeof(float));
    peakdat_ret.peaksyP=(float*)malloc(maxNumOfPeaksToFind*sizeof(float));
    peakdat_ret.numOfValidPeaks=maxNumOfPeaksToFind;
    for(uint16_t peakFoundSoFar=0;peakFoundSoFar<maxNumOfPeaksToFind;peakFoundSoFar++){
        float bestx=0;
        float besty=-FLT_MAX;
        for(uint16_t sampleNum=0;sampleNum<inputdat.numOfValidPeaks;sampleNum++){
            //check for deadzone hits
            int deadzoneHit=0;
            for(int dzCheckNum=0;dzCheckNum<peakFoundSoFar;dzCheckNum++){
                if((peakdat_ret.peaksxP[dzCheckNum]-deadzoneSize)<sampleNum&&sampleNum<(peakdat_ret.peaksxP[dzCheckNum]+deadzoneSize)){
                    deadzoneHit=1;
                }
            }
            if(deadzoneHit){
                continue;
            }
            //check if current point is higher then last one
            if(besty<inputdat.peaksyP[sampleNum]){
                besty=inputdat.peaksyP[sampleNum];
                bestx=sampleNum;
            }
        }
        peakdat_ret.peaksxP[peakFoundSoFar]=bestx;
        peakdat_ret.peaksyP[peakFoundSoFar]=besty;
    }
    return peakdat_ret;
}
