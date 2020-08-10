#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PORT 4242
#define BACKLOG 10
int main(int argv, char* argc){
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
    srv_address.sin_port=htons(PORT);
    ret=bind(socket_fd, (struct sockaddr *)&srv_address, sizeof(srv_address));
    if(ret<0){
        fprintf(stderr,"Could not bind to socket.\n");
        exit(1);
    }
    //set the socket to listen mode
    ret=listen(socket_fd,BACKLOG);
    if(ret<0){
        fprintf(stderr,"Could not mark socket as passive, another socket might already listen on this port.\n");
        exit(1);
    }
    //accept one connection in que on this socket
    struct sockaddr_in clnt_address;
    int clnt_len = sizeof(clnt_address);    //initializes as the size of clnt_address, accept will write the actual size of the clnt_address
    int accept_socket_fd=accept(socket_fd,(struct sockaddr*)&clnt_address,&clnt_len);
    if(accept_socket_fd<0){
        fprintf(stderr,"Accepting connection failed.\n");
        exit(1);
    }
	fprintf(stdout,"Accepted connection from %s",inet_ntoa(clnt_address.sin_addr));
    //close socket
    int return_val=close(socket_fd);
    if(return_val!=0){
        
    }
}