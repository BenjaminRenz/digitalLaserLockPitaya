#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>    //for sleep
#include "threads.h"

struct threadinfo{
    mutex_request_new_rawdata;
    
    
};

int thread_needs_rawdata(mtx_t* mutex_request_new_rawdatap){
    //lock mutex
        if(thrd_success!=mtx_lock(mutex_request_new_rawdatap)){
            exit(1);
        }
        int request_new_rawdata=0;
        //unlock mutex
        if(thrd_success!=mtx_unlock(mutex_request_new_rawdatap)){
            exit(1);
        }
}

int main(int argc, char **argv){
    thrd_t networkingThread;
    mtx_t mutex_request_new_rawdata;    //used by the network thread to indicate to the server that the client has requested the raw data
    //create mutex
    if(thrd_success!=mtx_init(&mutex_request_new_rawdata,mtx_plain)){
        exit(1);
    }
    //lock mutex
    if(thrd_success!=mtx_lock(&mutex_request_new_rawdata)){
        exit(1);
    }
    int request_new_rawdata=0;
    //unlock mutex
    if(thrd_success!=mtx_unlock(&mutex_request_new_rawdata)){
        exit(1);
    }
    if(thrd_success!=thrd_create(&networkingThread,,void )){
        exit(1);
    }
    
    while(1){
        
    }
    
    return 0;
}
