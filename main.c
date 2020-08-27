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

void        SetGenerator(rp_channel_t ScanChannel, float ScanFrequency, float OtherChannelOffset);
double      getDeltatimeS(void);
void        sortPeaksX(uint16_t numOfPeaks,uint16_t* peaksxP,float* peaksyP);
void        sortPeaksY(uint16_t numOfPeaks,uint16_t* peaksxP,float* peaksyP);
void        fsortPeaksX(uint16_t numOfPeaks,float* peaksxP,float* peaksyP);
int16_t     sgn(int16_t x);
uint16_t    get_num_valid_peaks(uint16_t numpeaks,float* peaksyP,float PeakDiscardTreshold,float average);
int16_t     get_least_peak_dist(uint16_t numpeaks1,uint16_t* peakx1P,uint16_t numpeaks2,uint16_t* peakx2P);
float       get_average(int numsamples,float* samplesY);
float       get_median_peakdist(uint16_t numpeaks,uint16_t* peakxP);
void        findPeaks(uint16_t numOfPoints,float* ydata,uint16_t numOfPeaks,uint16_t deadzoneSize,uint16_t* peaksx_returnp,float* peaksy_returnp);
void        waitTrgUnarmAndGetData(rp_acq_trig_src_t last_trgsrc, float* acqbufferP, uint32_t numOfSamples);
void        printPeaks(int numpeaks,uint16_t* peaksx,float* peaksy);
void        packRealIntoFFTcomplex(float* ReInP, float* ReCoOutP);
void        doCorrelation(float* fft_in_outP, float* corr_luP,int* ooura_fft_ipP, float* ooura_fft_wP);


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

void doScan(int firstrun,float* acqbufferP){
    if(firstrun){
        CHK_ERR(rp_AcqReset());
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
#define PeakDiscardFactor 0.65f
#define NumScanSteps 20
#define NewPeakTresholdFactor 0.2f         //check if new peak has entered from the left, this is the treshold ratio between dist_peaks_old_vs_new/distpeaks
enum {
    char_step_scan_cav,                 //initial scan of the cavity to determine cav_voltage_per_peakdist
    char_step_scan_grat,                //initial scan of the laser to determine grat_voltage_per_peakdist
    char_step_scan_direction,           //scan moving the grating 1/4 * grat_voltage_per_peakdist to check movement direction
    char_step_scan_finetune,            //rescan cavity to check if applying grat_voltage_per_peakdist as offset returns the same image as the initial scan (if not correct offset)
    char_step_scan_cav_mv_grat,         //scan cavity 1000 times while slowly varying the grating voltage from -grat_voltage_per_peakdist/2 to grat_voltage_per_peakdist/2
    char_step_finished,
    char_step_testhack
};

void doCharacterise(int firstrun,float* acqbufferP,struct threadinfo* threadinfP){
    //TODO remove this hack, just used for testing
    static int CharacterisationStep=char_step_testhack;
    //static int CharacterisationStep=char_step_scan_cav;
    //TODO the three entries below could be localized inside their specific case section
    static uint16_t* peaksx_scan_cav_P;
    static float* peaksy_scan_cav_P;
    static uint16_t valid_peaks_scan_cav;

    static float cav_voltage_per_peakdist;
    static float grat_voltage_per_peakdist;
    static float average;
    //output of the function
    static float* characterisationDataXP;
    static float* characterisationDataYP;
    static uint32_t valid_peaks_charact;

    static int scandir;
    if(firstrun){
        CHK_ERR(rp_AcqReset());
        SetGenerator(RP_CH_1,SLOW_FREQ,-1.0f);
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
        case char_step_testhack:
            characterisationDataXP=(float*)malloc(NumScanSteps*MaxNumOfPeaks_Scan_Cav*sizeof(float));
            characterisationDataYP=(float*)malloc(NumScanSteps*MaxNumOfPeaks_Scan_Cav*sizeof(float));
            for(uint32_t scannum=0;scannum<NumScanSteps;scannum++){
                for(uint32_t peaknum=0;peaknum<MaxNumOfPeaks_Scan_Cav;peaknum++){
                    if(((float)rand())/(float)RAND_MAX*MaxNumOfPeaks_Scan_Cav<=peaknum){
                        characterisationDataXP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=((float)rand())/(float)RAND_MAX;
                        characterisationDataYP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=((float)rand())/(float)RAND_MAX;
                    }else{
                        characterisationDataXP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=FLT_MAX;
                        characterisationDataYP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=FLT_MAX;
                    }
                }
            }
            
            fsortPeaksX(NumScanSteps*MaxNumOfPeaks_Scan_Cav,characterisationDataXP,characterisationDataYP);
            valid_peaks_charact=0;
            for(;valid_peaks_charact<NumScanSteps*MaxNumOfPeaks_Scan_Cav;valid_peaks_charact++){

                if(characterisationDataXP[valid_peaks_charact]==FLT_MAX){
                    break;
                }
            }
            printf("got %d valid points\n",valid_peaks_charact);

            //send data to thread
            mtx_lock(&threadinfP->mutex_network_characterization);
            threadinfP->network_numOfCharacterizationPoints=valid_peaks_charact;
            threadinfP->network_characterisationXP=characterisationDataXP;
            threadinfP->network_characterisationYP=characterisationDataYP;
            mtx_unlock(&threadinfP->mutex_network_characterization);
            printf("send data to thread2\n");
            //exit the characerization mode, by pretending that the client has selected scan (TODO hacky)
            mtx_lock(&threadinfP->mutex_network_operation_mode);
            printf("lockopmode\n");
            threadinfP->network_operation_mode=operation_mode_scan;
            printf("lockopmode2\n");
            mtx_unlock(&threadinfP->mutex_network_operation_mode);
            printf("switch mode\n");
        break;
        case char_step_scan_cav:
            peaksx_scan_cav_P=(uint16_t*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(uint16_t));
            peaksy_scan_cav_P=(float*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(float));
            average=get_average(ADCBUFFERSIZE,acqbufferP);
            printf("Average is %f\n",average);
            findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Cav,DeadzoneSamplepoints_Scan_Cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            printPeaks(MaxNumOfPeaks_Scan_Cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            valid_peaks_scan_cav=get_num_valid_peaks(MaxNumOfPeaks_Scan_Cav,peaksy_scan_cav_P,PeakDiscardFactor,average);
            sortPeaksX(valid_peaks_scan_cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            printPeaks(valid_peaks_scan_cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            if(valid_peaks_scan_cav==1){
                printf("Error, not enough peaks found during the first scan of the cavity, repeat scan.\n");
                return;
            }
            cav_voltage_per_peakdist=(2.0f/ADCBUFFERSIZE)*get_median_peakdist(valid_peaks_scan_cav,peaksx_scan_cav_P);
            printf("cav_voltage_per_peakdist %f\n",cav_voltage_per_peakdist);
            CharacterisationStep=char_step_scan_grat;
            SetGenerator(RP_CH_2,SLOW_FREQ,0.0f);
        break;
        case char_step_scan_grat: ;//need this here
            uint16_t* peaksx_scan_grat_P=(uint16_t*)malloc(MaxNumOfPeaks_Scan_Grat*sizeof(uint16_t));
            float*    peaksy_scan_grat_P=(float*)malloc(MaxNumOfPeaks_Scan_Grat*sizeof(float));
            findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Grat,DeadzoneSamplepoints_Scan_Grat,peaksx_scan_grat_P,peaksy_scan_grat_P);
            uint16_t valid_peaks_scan_grat=get_num_valid_peaks(MaxNumOfPeaks_Scan_Grat,peaksy_scan_grat_P,PeakDiscardFactor,average);
            sortPeaksX(valid_peaks_scan_grat,peaksx_scan_grat_P,peaksy_scan_grat_P);
            printPeaks(valid_peaks_scan_grat,peaksx_scan_grat_P,peaksy_scan_grat_P);
            if(valid_peaks_scan_grat==1){
                printf("Error, not enough peaks found while scanning grating, repeat scan.\n");
                return;
            }
            //TODO why is this negative?????
            grat_voltage_per_peakdist=(2.0f/ADCBUFFERSIZE)*get_median_peakdist(valid_peaks_scan_grat,peaksx_scan_grat_P);
            printf("grat_voltage_per_peakdist %f\n",grat_voltage_per_peakdist);
            free(peaksx_scan_grat_P);
            free(peaksy_scan_grat_P);
            CharacterisationStep=char_step_scan_direction;
            SetGenerator(RP_CH_1,SLOW_FREQ,-1.0f+grat_voltage_per_peakdist*0.25f);
        break;
        case char_step_scan_direction: ;
            uint16_t* peaksx_scan_dir_cav_P=(uint16_t*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(uint16_t));
            float* peaksy_scan_dir_cav_P=(float*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(float));
            findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Cav,DeadzoneSamplepoints_Scan_Cav,peaksx_scan_dir_cav_P,peaksy_scan_dir_cav_P);
            uint16_t valid_peaks_dir_scan_cav=get_num_valid_peaks(MaxNumOfPeaks_Scan_Cav,peaksy_scan_dir_cav_P,PeakDiscardFactor,average);
            sortPeaksX(valid_peaks_dir_scan_cav,peaksx_scan_dir_cav_P,peaksy_scan_dir_cav_P);
            int16_t leastSampleDist=get_least_peak_dist(valid_peaks_dir_scan_cav,peaksx_scan_dir_cav_P,valid_peaks_scan_cav,peaksx_scan_cav_P); //do not change order or arguments!
            if( (fabsf(leastSampleDist/(cav_voltage_per_peakdist*ADCBUFFERSIZE/2.0f))<0.2f) || (0.3f<fabsf(leastSampleDist/(cav_voltage_per_peakdist*ADCBUFFERSIZE/2.0f)))){
                printf("Error while finding scan direction, expected sample dist to be between +/- 0.2 and +/- 0.3, but got %f, retry.\n",leastSampleDist/(cav_voltage_per_peakdist*ADCBUFFERSIZE/2.0f));
                return;
            }
            scandir=sgn(leastSampleDist);
            printf("Scan direction is %d\n",scandir);
            free(peaksx_scan_dir_cav_P);
            free(peaksy_scan_dir_cav_P);
            CharacterisationStep=char_step_scan_finetune;
            SetGenerator(RP_CH_1,SLOW_FREQ,-1.0f+grat_voltage_per_peakdist);
            break;
        case char_step_scan_finetune:;{
            uint16_t* peaksx_scan_fine_cav_P=(uint16_t*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(uint16_t));
            float*    peaksy_scan_fine_cav_P=(float*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(float));
            findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Cav,DeadzoneSamplepoints_Scan_Cav,peaksx_scan_fine_cav_P,peaksy_scan_fine_cav_P);
            uint16_t valid_peaks_scan_fine_cav=get_num_valid_peaks(MaxNumOfPeaks_Scan_Cav,peaksy_scan_fine_cav_P,PeakDiscardFactor,average);
            sortPeaksX(valid_peaks_scan_fine_cav,peaksx_scan_fine_cav_P,peaksy_scan_fine_cav_P);
            if(valid_peaks_scan_fine_cav==1){
                printf("Error, not enough peaks found while scanning cavity for the second time, repeat scan.\n");
                return;
            }
            int16_t leastSampleDist=get_least_peak_dist(valid_peaks_scan_fine_cav,peaksx_scan_fine_cav_P,valid_peaks_scan_cav,peaksx_scan_cav_P); //do not change order or arguments!
            printf("Smallest samplenum between peak of first and second scan of cavity was %d\n",leastSampleDist);
            //correct grat_voltage_per_peakdist
            printf("old grat_voltage_per_peakdist=%f\n",grat_voltage_per_peakdist);
            grat_voltage_per_peakdist*=(cav_voltage_per_peakdist/(cav_voltage_per_peakdist+(leastSampleDist*(float)scandir*2.0f/ADCBUFFERSIZE)));
            printf("corrected grat_voltage_per_peakdist=%f\n",grat_voltage_per_peakdist);
            free(peaksx_scan_fine_cav_P);
            free(peaksy_scan_fine_cav_P);
            //Setup generator for char_step_scan_cav_check_range
            CharacterisationStep=char_step_scan_cav_mv_grat;
            SetGenerator(RP_CH_1,SLOW_FREQ,(grat_voltage_per_peakdist*-0.5f*(float)scandir));
        }
        break;
        case char_step_scan_cav_mv_grat:;{
            static uint16_t scannum=0;
            static uint16_t valid_peaks_old_scan;
            static int idx_offset=0;

            static uint16_t* peaksx_scan_old_cav_P=0;
            static float*    peaksy_scan_old_cav_P=0;
            if(scannum==0){
                peaksx_scan_old_cav_P=(uint16_t*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(uint16_t));
                peaksy_scan_old_cav_P=(float*)malloc(MaxNumOfPeaks_Scan_Cav*sizeof(float));
                findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Cav,DeadzoneSamplepoints_Scan_Cav,peaksx_scan_old_cav_P,peaksy_scan_old_cav_P);
                valid_peaks_old_scan=get_num_valid_peaks(MaxNumOfPeaks_Scan_Cav,peaksy_scan_old_cav_P,PeakDiscardFactor,average);
                sortPeaksX(valid_peaks_old_scan,peaksx_scan_old_cav_P,peaksy_scan_old_cav_P);
                characterisationDataXP=(float*)malloc(NumScanSteps*MaxNumOfPeaks_Scan_Cav*sizeof(float));
                characterisationDataYP=(float*)malloc(NumScanSteps*MaxNumOfPeaks_Scan_Cav*sizeof(float));
                for(uint32_t peaknum=0;peaknum<MaxNumOfPeaks_Scan_Cav;peaknum++){
                    if(valid_peaks_old_scan<=peaknum){
                        characterisationDataXP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=scannum/(float)NumScanSteps+(float)peaknum+(float)idx_offset;
                        characterisationDataYP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=peaksx_scan_cav_P[peaknum];
                    }else{
                        characterisationDataXP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=FLT_MAX;
                        characterisationDataYP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=FLT_MAX;
                    }
                }
                scannum++;
                return;
            }
            findPeaks(ADCBUFFERSIZE,acqbufferP,MaxNumOfPeaks_Scan_Cav,DeadzoneSamplepoints_Scan_Cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            valid_peaks_scan_cav=get_num_valid_peaks(MaxNumOfPeaks_Scan_Cav,peaksy_scan_cav_P,PeakDiscardFactor,average);
            sortPeaksX(valid_peaks_scan_cav,peaksx_scan_cav_P,peaksy_scan_cav_P);
            //check if a new peak has entered from the left
            int16_t sampledistFirstPeaks=(int16_t)((int16_t)peaksx_scan_cav_P[0]-(int16_t)peaksx_scan_old_cav_P[0]);
            printf("sampledist %d\n",sampledistFirstPeaks);
            float peakdelta_pre_peakdist=sampledistFirstPeaks/(cav_voltage_per_peakdist*(ADCBUFFERSIZE/2.0f));
            printf("peakdelta_pre_peakdist %f\n",peakdelta_pre_peakdist);

            if(fabsf(peakdelta_pre_peakdist)<NewPeakTresholdFactor){
                printf("First peak of current and last scan had a ratio to scan_dist of %f\n",peakdelta_pre_peakdist);
            }else if( (1.0f-NewPeakTresholdFactor)<fabsf(peakdelta_pre_peakdist) && (fabsf(peakdelta_pre_peakdist)<(1.0f+NewPeakTresholdFactor))){
                printf("leftmost peak appeared or disappeared, delta is %f\n",peakdelta_pre_peakdist);
                idx_offset+=sgn(sampledistFirstPeaks);
            }else{
                printf("strange peak missmatch, discarding data and retry\n");
                return;
            }
            for(uint32_t peaknum=0;peaknum<MaxNumOfPeaks_Scan_Cav;peaknum++){
                if(valid_peaks_scan_cav<=peaknum){
                    characterisationDataXP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=scannum/(float)NumScanSteps+(float)peaknum+(float)idx_offset;
                    characterisationDataYP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=peaksx_scan_cav_P[peaknum];
                }else{
                    characterisationDataXP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=FLT_MAX;
                    characterisationDataYP[(uint32_t)MaxNumOfPeaks_Scan_Cav*scannum+peaknum]=FLT_MAX;
                }
            }
            uint16_t* tempx=peaksx_scan_old_cav_P;
            float* tempy=peaksy_scan_old_cav_P;
            peaksx_scan_old_cav_P=peaksx_scan_cav_P;
            peaksy_scan_old_cav_P=peaksy_scan_cav_P;
            peaksx_scan_cav_P=tempx;
            peaksy_scan_cav_P=tempy;
            scannum++;
            if(scannum<NumScanSteps){
                CHK_ERR(rp_GenOffset(RP_CH_2, grat_voltage_per_peakdist*((-0.5f+(scannum/(float)NumScanSteps))*(float)scandir)));
            }else{
                //cleanup
                free(peaksx_scan_old_cav_P);
                free(peaksy_scan_old_cav_P);
                free(peaksx_scan_cav_P);
                free(peaksy_scan_cav_P);
                CharacterisationStep=char_step_finished;

                //check how many peaks were found
                fsortPeaksX(NumScanSteps*MaxNumOfPeaks_Scan_Cav,characterisationDataXP,characterisationDataYP);
                valid_peaks_charact=0;
                for(;valid_peaks_charact<NumScanSteps*MaxNumOfPeaks_Scan_Cav;valid_peaks_charact++){

                    if(characterisationDataXP[valid_peaks_charact]==FLT_MAX){
                        break;
                    }
                }
                printf("got %d valid points\n",valid_peaks_charact);

                //send data to thread
                mtx_lock(&threadinfP->mutex_network_characterization);
                threadinfP->network_numOfCharacterizationPoints=valid_peaks_charact;
                threadinfP->network_characterisationXP=characterisationDataXP;
                threadinfP->network_characterisationYP=characterisationDataYP;
                mtx_unlock(&threadinfP->mutex_network_characterization);

                //exit the characerization mode, by pretending that the client has selected scan (TODO hacky)
                mtx_lock(&threadinfP->mutex_network_operation_mode);
                threadinfP->network_operation_mode=operation_mode_scan;
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
    threadinfP->network_operation_mode=operation_mode_scan;
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
                case operation_mode_scan:
                    printf("sw scan\n");
                    doScan(1,acqbufferP);
                break;
                case operation_mode_characterise:
                    printf("sw char\n");
                    doCharacterise(1,acqbufferP,threadinfP);
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
                case operation_mode_scan:
                    doScan(0,acqbufferP);
                break;
                case operation_mode_characterise:
                    doCharacterise(0,acqbufferP,threadinfP);
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

void printPeaks(int numpeaks,uint16_t* peaksx,float* peaksy){
    printf("npeaks found: %d\n",numpeaks);
    for(int peak=0;peak<numpeaks;peak++){
        printf("Peak at x=\t%d\t, y=\t%f\t\n",peaksx[peak],peaksy[peak]);
    }
}

uint16_t get_num_valid_peaks(uint16_t numpeaks,float* peaksyP,float PeakDiscardTreshold,float average){
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
}

float get_median_peakdist(uint16_t numpeaks,uint16_t* peakxP){
    float median_peakdist=0.0f;
    for(uint16_t peak_idx=1;peak_idx<numpeaks;peak_idx++){
        median_peakdist+=fabsf((float)peakxP[peak_idx]-(float)peakxP[peak_idx-1])/(float)(numpeaks-1);
        printf("Peakdist %f\n",median_peakdist);
    }
    return median_peakdist;
}

int16_t sgn(int16_t x){
    if(x<0){
        return -1;
    }
    return 1;
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

void fsortPeaksX(uint16_t numOfPeaks,float* peaksxP,float* peaksyP){
    int swapped_flag;
    uint16_t numOfSwaps=numOfPeaks;
    do{
        swapped_flag=0;
        for(uint16_t bubble_offset=0;bubble_offset<numOfSwaps-1;bubble_offset++){
            if(peaksxP[bubble_offset+1]<peaksxP[bubble_offset]){
                swapped_flag=1;
                //swap peaksx
                float tempx=peaksxP[bubble_offset];
                peaksxP[bubble_offset]=peaksxP[bubble_offset+1];
                peaksxP[bubble_offset+1]=tempx;
                //swap peaksy
                float tempy=peaksyP[bubble_offset];
                peaksyP[bubble_offset]=peaksyP[bubble_offset+1];
                peaksyP[bubble_offset+1]=tempy;
            }
        }
        numOfSwaps--;
    }while(swapped_flag);
}

void sortPeaksX(uint16_t numOfPeaks,uint16_t* peaksxP,float* peaksyP){
    int swapped_flag;
    uint16_t numOfSwaps=numOfPeaks;
    do{
        swapped_flag=0;
        for(uint16_t bubble_offset=0;bubble_offset<numOfSwaps-1;bubble_offset++){
            if(peaksxP[bubble_offset+1]<peaksxP[bubble_offset]){
                swapped_flag=1;
                //swap peaksx
                uint16_t tempx=peaksxP[bubble_offset];
                peaksxP[bubble_offset]=peaksxP[bubble_offset+1];
                peaksxP[bubble_offset+1]=tempx;
                //swap peaksy
                float tempy=peaksyP[bubble_offset];
                peaksyP[bubble_offset]=peaksyP[bubble_offset+1];
                peaksyP[bubble_offset+1]=tempy;
            }
        }
        numOfSwaps--;
    }while(swapped_flag);
}

void sortPeaksY(uint16_t numOfPeaks,uint16_t* peaksxP,float* peaksyP){
    int swapped_flag;
    uint16_t numOfSwaps=numOfPeaks;
    do{
        swapped_flag=0;
        for(uint16_t bubble_offset=0;bubble_offset<numOfSwaps-1;bubble_offset++){
            if(peaksyP[bubble_offset+1]<peaksyP[bubble_offset]){
                swapped_flag=1;
                //swap peaksx
                uint16_t tempx=peaksxP[bubble_offset];
                peaksxP[bubble_offset]=peaksxP[bubble_offset+1];
                peaksxP[bubble_offset+1]=tempx;
                //swap peaksy
                float tempy=peaksyP[bubble_offset];
                peaksyP[bubble_offset]=peaksyP[bubble_offset+1];
                peaksyP[bubble_offset+1]=tempy;
            }
        }
        numOfSwaps--;
    }while(swapped_flag);
}

void findPeaks(uint16_t numOfPoints,float* ydata,uint16_t numOfPeaks,uint16_t deadzoneSize,uint16_t* peaksx_returnp,float* peaksy_returnp){
    for(uint16_t peakNum=0;peakNum<numOfPeaks;peakNum++){
        uint16_t bestx=0;
        float    besty;
        besty=-FLT_MAX;
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
