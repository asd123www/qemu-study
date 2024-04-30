#include <stdio.h> 
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h> // read(), write(), close()
#define MAX 1024
#define PORT 8080 
#define SA struct sockaddr 
   
// Function designed for chat between client and server. 
void func(int connfd)  {
    char buff[MAX];
    // infinite loop for chat 
    for (;;) {
        bzero(buff, MAX); 
   
        // read the message from client and copy it in buffer 
        uint32_t read_len = read(connfd, buff, sizeof(buff)); 
        // and send that buffer to client 
        write(connfd, buff, read_len); 
    }
}

// Driver function 
int main() {
    int sockfd, connfd, len; 
    struct sockaddr_in servaddr, cli; 
   
    // socket create and verification 
    sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sockfd == -1) { 
        printf("socket creation failed...\n"); 
        exit(0); 
    }
    bzero(&servaddr, sizeof(servaddr)); 
   
    // assign IP, PORT 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = inet_addr("10.10.1.100"); 
    servaddr.sin_port = htons(55999); 
   
    // Binding newly created socket to given IP and verification 
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) { 
        printf("socket bind failed...\n"); 
        exit(0); 
    }
   
    // Now server is ready to listen and verification 
    if ((listen(sockfd, 5)) != 0) { 
        printf("Listen failed...\n"); 
        exit(0); 
    }
    len = sizeof(cli);
   
    // Accept the data packet from client and verification 
    connfd = accept(sockfd, (SA*)&cli, &len); 
    if (connfd < 0) { 
        printf("server accept failed...\n"); 
        exit(0); 
    }
   
    // Function for chatting between client and server 
    func(connfd); 
   
    // After chatting close the socket 
    close(sockfd);
    return 0;
}