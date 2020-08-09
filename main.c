#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>    //for sleep

#include "redpitaya/rp.h"
//#include ""

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
    
    uint32_t buff_size=16384;
    float* buffer=(float*) malloc(buff_size*sizeof(float));
    rp_AcqReset();
    rp_AcqSetDecimation(RP_DEC_8);
    
	rp_AcqSetTriggerDelay(0);
	
    rp_AcqStart();  //this commands initiates the pitaya to start aquiring samples, but we need to wait a bit  to enable the trigger
	sleep(10);   //sleep 10s
	
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
        
        sleep(0.01);
        rp_AcqSetTriggerSrc(RP_TRIG_SRC_AWG_PE);    //rearm trigger source

    }
	rp_AcqGetOldestDataV(RP_CH_1, &buff_size, buffer);
	int i;
	for(i = 0; i < buff_size; i++){
			printf("%f\n", buffer[i]);
	}
	/* Releasing resources */
	free(buffer);
    rp_Release();
    return 0;
}
