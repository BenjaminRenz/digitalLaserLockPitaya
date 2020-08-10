#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


#define TCP_PORT 4242
#define TCP_MAX_REC_CHUNK_SIZE 4096
#define BACKLOG 10
#define MAXRECDATA (100*sizeof(uint32_t))
#define NUMSETTINGS 4
#define NUMLASERS 2

enum {getGraph=0,getGraph_return=1,setSettings=2,setSettings_return=3,
      getSettings=4,getSettings_return=5,getOffset=6,getOffset_return=7};

float settings[NUMSETTINGS]={1.0f,2.0f,3.0f,4.0f};
int offsets[NUMLASERS]={10,50};

uint32_t htonf(float in){
    return htonl(*((uint32_t*)(&in)));
}

float ntohf(uint32_t in){
    uint32_t temp=ntohl(in);
    return *((float*)(&temp));
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
        length-=bytes_transm;
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
        length-=bytes_rec;
    }
    return 0;
}

int main(int argv, char** argc){
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
        int clnt_len = sizeof(clnt_address);    //initializes as the size of clnt_address, accept will write the actual size of the clnt_address
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
                        send_databufferp[i]=htonl(offsets[i]);
                    }
                    send_all(accept_socket_fd,send_databufferp,NUMLASERS*sizeof(int32_t));
                    free(send_databufferp);
                }
                break;
                default:
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