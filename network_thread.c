#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "network_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
//check if we can represent a float as four bytes, this property is used to send the float over the network
static_assert(sizeof(uint32_t) == sizeof(float),

#define TCP_PORT 4242
#define TCP_MAX_REC_CHUNK_SIZE 4096
#define BACKLOG 10
#define MAXRECDATA (100*sizeof(uint32_t))
#define NUMSETTINGS 4
#define NUMLASERS 2

enum {
    getGraph=0,
    getGraph_return=1,
    setSettings=2,
    setSettings_return=3,
    getSettings=4,
    getSettings_return=5,
    getOffset=6,
    getOffset_return=7,

    setOpmode=8,
    setOpmode_return=9,
    getOpmode=10,
    getOpmode_return=11,

    getCharacterization=12,
    getCharacterization_return=13

};

float settings[NUMSETTINGS]={1.0f,2.0f,3.0f,4.0f};
int offsets[NUMLASERS]={10,50};

//float to network uint32 conversion routines
union networkfloat{
    float       flt;
    uint32_t* uint32;
};

uint32_t htonf(float in){
    union networkfloat temp;
    temp.flt=in;
    return htonl(temp.uint32);
}

float ntohf(uint32_t in){
    union networkfloat temp;
    temp.uint32=ntohl(in);
    return temp.flt;
}

int send_all(int socket, void* buffer, size_t length){
    uint8_t* ptr=(uint8_t*)buffer;
    while(0<length){
        int bytes_transm;
        if(TCP_MAX_REC_CHUNK_SIZE<length){
            bytes_transm=send(socket,ptr,TCP_MAX_REC_CHUNK_SIZE,0);
        }else{
            bytes_transm=send(socket,ptr,length,0);
        }
        if(bytes_transm<1){
            return 1;   //Error
        }
        ptr+=bytes_transm;
        length-=(size_t)bytes_transm;
    }
    return 0;
}

int recv_all(int socket, void* buffer, size_t length){
    uint8_t* ptr=(uint8_t*)buffer;
    while(0<length){
        int bytes_rec;
        if(TCP_MAX_REC_CHUNK_SIZE<length){
            bytes_rec=recv(socket,ptr,TCP_MAX_REC_CHUNK_SIZE,0);
        }else{
            bytes_rec=recv(socket,ptr,length,0);
        }
        if(bytes_rec<1){
            return 1;   //Error
        }
        ptr+=bytes_rec;
        length-=(size_t)bytes_rec;
    }
    return 0;
}

int thrd_startServer(void* threadinfp){
    //lock rawdata buffer until the user request data
    if(thrd_success!=mtx_lock(threadinfP->mutex_rawdata_bufferP)){
        exit(1);
    }

    int ret;    //return temp int
    //create tcp socket with ipv4 address, use AF_INET6  for ipv6
    int socket_fd=socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd<0){
        fprintf(stderr,"Could not create socket.\n");
        exit(1);
    }
    //bind to socket
    struct sockaddr_in srv_address;
    srv_address.sin_family=AF_INET;
    srv_address.sin_addr.s_addr=INADDR_ANY;
    srv_address.sin_port=htons(TCP_PORT);
    ret=bind(socket_fd, (struct sockaddr *)&srv_address, sizeof(srv_address));
    if(ret<0){
        fprintf(stderr,"Could not bind to socket.\n");
        close(socket_fd);
        exit(1);
    }
    //set the socket to listen mode
    ret=listen(socket_fd,BACKLOG);
    if(ret<0){
        fprintf(stderr,"Could not mark socket as passive, another socket might already listen on this port.\n");
        close(socket_fd);
        exit(1);
    }



    while(1){
        //accept only one connection in que on this socket
        struct sockaddr_in clnt_address;
        unsigned int clnt_len = sizeof(clnt_address);    //initializes as the size of clnt_address, accept will write the actual size of the clnt_address
        int accept_socket_fd=accept(socket_fd,(struct sockaddr*)&clnt_address,&clnt_len);
        if(accept_socket_fd<0){
            fprintf(stderr,"Accepting connection failed.\n");
            close(socket_fd);
            exit(1);
        }
        fprintf(stdout,"Accepted connection from %s\n",inet_ntoa(clnt_address.sin_addr));
        while(1){
            //Recieve header
            uint32_t header[2];
            ret=recv_all(accept_socket_fd,header,2*sizeof(int));
            if(ret){
                printf("Disconnect while reciveing header.\n");
                close(accept_socket_fd);
                break;
            }
            uint32_t requestType=ntohl(header[0]);
            uint32_t dataLength =ntohl(header[1]);
            if(MAXRECDATA<dataLength){
                fprintf(stderr,"Requested transfer length of %d bytes is to long.\n",ntohl(header[1]));
                close(accept_socket_fd);
                close(socket_fd);
                exit(1);
            }
            //Recieve data if any
            void* rec_databuffp=malloc(dataLength);
            ret=recv_all(accept_socket_fd,rec_databuffp,dataLength);
            if(ret){
                 printf("Disconnect while reciveing data.\n");
                close(accept_socket_fd);
                break;
            }

            //Handle request
            switch(requestType){
                case getGraph:
                    printf("Handle getGraph request\n");
                    //order the main thread to save the next aquisition into the buffer and wait for completion of this task
                    if(thrd_success!=cnd_wait(&threadinfP->condidion_mainthread_finished_memcpy,&threadinfP->mutex_network_acqBuffer)){
                        exit(1);
                    }
                    printf("Start response\n");
                    header[0]=htonl(getGraph_return);
                    header[1]=htonl(ADCBUFFERSIZE*sizeof(float));
                    send_all(accept_socket_fd,header,2*sizeof(int32_t));

                    //uint32_t* send_databufferp=malloc(sizeof(float)*ADCBUFFERSIZE);
                    for(int i=0;i<ADCBUFFERSIZE;i++){
                        //TODO don't use this sloppy conversion to uint32_t*
                        ((uint32_t*)threadinfP->network_acqBufferP)[i]=htonf(threadinfP->network_acqBufferP[i]);
                    }
                    //send_all(accept_socket_fd,send_databufferp,ADCBUFFERSIZE*sizeof(float));
                    send_all(accept_socket_fd,(uint32_t*)threadinfP->network_acqBufferP,ADCBUFFERSIZE*sizeof(float));
                    //free(send_databufferp);
                    printf("send Complete\n");
                break;
                case setSettings:{
                    printf("Handle setSettings request\n");
                    if(dataLength!=sizeof(int)*NUMSETTINGS){
                        fprintf(stderr,"Invalid request format for setSettings.\n");
                        close(accept_socket_fd);
                        close(socket_fd);
                        exit(1);
                    }
                    for(int i=0;i<NUMSETTINGS;i++){
                        settings[i]=ntohf(((uint32_t*)rec_databuffp)[i]);
                        printf("value was: %f\n",settings[i]);
                    }
                    header[0]=htonl(setSettings_return);
                    header[1]=htonl(0);
                    send_all(accept_socket_fd,header,2*sizeof(int32_t));
                }
                break;
                case getSettings:{
                    printf("Handle getSettings request\n");
                    if(dataLength){
                        fprintf(stderr,"Invalid request format for getSettings.\n");
                        close(accept_socket_fd);
                        close(socket_fd);
                        exit(1);
                    }
                    header[0]=htonl(getSettings_return);
                    header[1]=htonl(NUMSETTINGS*sizeof(int32_t));
                    send_all(accept_socket_fd,header,2*sizeof(int32_t));
                    uint32_t* send_databufferp=malloc(sizeof(int32_t)*NUMSETTINGS);
                    for(int i=0;i<NUMSETTINGS;i++){
                        send_databufferp[i]=htonf(settings[i]);
                    }
                    send_all(accept_socket_fd,send_databufferp,NUMSETTINGS*sizeof(int32_t));
                    free(send_databufferp);
                }
                break;
                case getOffset:{
                    printf("Handle getOffset request\n");
                    if(dataLength){
                        fprintf(stderr,"Invalid request format for getOffset.\n");
                        close(accept_socket_fd);
                        close(socket_fd);
                        exit(1);
                    }
                    header[0]=htonl(getSettings_return);
                    header[1]=htonl(NUMLASERS*sizeof(int32_t));
                    send_all(accept_socket_fd,header,2*sizeof(int32_t));
                    uint32_t* send_databufferp=malloc(sizeof(int32_t)*NUMLASERS);
                    for(int i=0;i<NUMLASERS;i++){
                        send_databufferp[i]=htonl((uint32_t)offsets[i]);
                    }
                    send_all(accept_socket_fd,send_databufferp,NUMLASERS*sizeof(int32_t));
                    free(send_databufferp);
                }
                break;
                case setOpmode:{
                    printf("Handle setOpmode request\n");
                    if(dataLength!=sizeof(uint32_t)){
                        fprintf(stderr,"Invalid data format for setOpmode.\n");
                        close(accept_socket_fd);
                        close(socket_fd);
                        exit(1);
                    }
                    uint32_t opmode=ntohl(((uint32_t*)rec_databuffp)[0]);
                    if(opmode!=operation_mode_scan&&opmode!=operation_mode_characterise&&opmode!=operation_mode_lock&&opmode!=operation_mode_shutdown){
                        fprintf(stderr,"Invalid opMode.\n");
                        close(accept_socket_fd);
                        close(socket_fd);
                        exit(1);
                    }
                    mtx_lock(&threadinfP->mutex_network_operation_mode);
                    threadinfP->network_operation_mode=opmode;
                    mtx_unlock(&threadinfP->mutex_network_operation_mode);
                    header[0]=htonl(setOpmode_return);
                    header[1]=0;
                    send_all(accept_socket_fd,header,2*sizeof(uint32_t));
                }
                break;
                case getOpmode:{
                    printf("Handle getOpmode request\n");
                    if(dataLength!=0){
                        fprintf(stderr,"Invalid data format for setOpmode.\n");
                        close(accept_socket_fd);
                        close(socket_fd);
                        exit(1);
                    }
                    mtx_lock(&threadinfP->mutex_network_operation_mode);
                    uint32_t opmode=htonl(threadinfP->network_operation_mode);
                    mtx_unlock(&threadinfP->mutex_network_operation_mode);
                    header[0]=htonl(getOpmode_return);
                    header[1]=htonl(sizeof(uint32_t));
                    send_all(accept_socket_fd,header,2*sizeof(uint32_t));
                    send_all(accept_socket_fd,&opmode,sizeof(uint32_t));
                }
                case getCharacterization:{
                    printf("Handle getCharacterization request\n");
                    if(dataLength!=0){
                        fprintf(stderr,"Invalid data format for getCharacterization.\n");
                        close(accept_socket_fd);
                        close(socket_fd);
                        exit(1);
                    }
                    mtx_lock(&threadinfP->mutex_network_characterization);
                    header[0]=htonl(getCharacterization_return);
                    header[1]=htonl(threadinfP->network_numOfCharacterizationPoints*2*sizeof(float));
                    send_all(accept_socket_fd,header,2*sizeof(uint32_t));
                    printf("send getCharacterization_return header\n");
                    //convert to network byte order
                    /*for(uint32_t datapoint=0;datapoint<numpoints;datapoint++){
                        printf("before conv\n");
                        ((uint32_t*)(threadinf.characterisationXP))[datapoint]=htonf(threadinf.characterisationXP[datapoint]);
                        printf("after conv\n");
                    }*/
                    for(uint32_t datapoint=0;datapoint<threadinfP->network_numOfCharacterizationPoints;datapoint++){
                        printf("before conv2\n");
                        //error happens when dereferencing pointer, possibly because of wrong memory alighment
                        printf("printing test 0 %p",threadinf.characterisationXP);
                        /*printf("printing test 1 %x",(int32_t)threadinf.characterisationYP);
                        ((uint32_t*)(threadinf.characterisationYP))[datapoint]=htonf(threadinf.characterisationYP[datapoint]);
                        printf("after conv2\n");*/
                    }

                    send_all(accept_socket_fd,threadinf.characterisationXP,numpoints*sizeof(uint32_t));
                    send_all(accept_socket_fd,threadinf.characterisationYP,numpoints*sizeof(uint32_t));
                    if(numpoints){
                        free(threadinf.characterisationXP);
                        free(threadinf.characterisationYP);
                    }
                    mtx_unlock(&threadinfP->mutex_network_characterization);
                }
                break;
                default:
                    printf("unknown command\n");
                break;
            }
            free(rec_databuffp);
        }
    }

    //close socket
    int return_val=close(socket_fd);
    if(return_val!=0){
        fprintf(stderr,"Closing socket failed.\n");
        exit(1);
    }
}
