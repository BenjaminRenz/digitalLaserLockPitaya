#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
int main(int argv, char* argc){
    //create tcp socket with ipv4 address
    int socket = socket( AF_INET, SOCK_STREAM, 0 );
    if(socket<0){
        fprintf(stderr,"Could not create socket\n");
    }
    //connect to soclet
    
    
    //close socket
    int return_val=close(socket);
    if(return_val!=0){
        
    }
}