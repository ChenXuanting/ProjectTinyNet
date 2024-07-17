#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/spinlock.h"
#include "kernel/socket.h"
#include "user/user.h"

#define BUF_SIZE 100
#define SERVER_HOST "10.0.2.15"
#define SERVER_PORT 20480

int main(int argc, char *argv[]) {
    struct sockaddr serv_addr = {
        .sa_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
    };
    inetaddress(SERVER_HOST,&serv_addr);  // server ip

    printf("server: address in network byte order: %x\n", serv_addr.sin_addr);
    printf("server: port in network byte order: %x\n", serv_addr.sin_port);

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock < 0) {
        printf("server: failed to create socket\n");
        exit(1);
    }

    if (bind(serv_sock, &serv_addr, sizeof(serv_addr)) < 0) {
        printf("server: failed to bind the server socket");
        exit(1);
    }
    printf("server: bind successfully\n");

    if (listen(serv_sock, 200) < 0) {
        printf("server: failed to listen on server socket");
        exit(1);
    }
    printf("server: listen successfully\n");
    
    struct sockaddr clnt_addr;
    int clnt_addr_size;
    char buffer[BUF_SIZE];

    while (1) {
        printf("server: waiting for connection...\n");
        int clnt_sock = accept(serv_sock, &clnt_addr, &clnt_addr_size); 
        if (clnt_sock < 0) {
            printf("server: failed to accept client connection");
            continue;
        }
        printf("server: accept successfully\n");
        
        memset(buffer, 0, BUF_SIZE);
        printf("server: reading from socket %d\n", clnt_sock);
        int recv_len = read(clnt_sock, buffer, BUF_SIZE);  
        printf("server: received %d bytes: %s\n", recv_len, buffer);
        
        int sent_len = write(clnt_sock, buffer, recv_len);
        if (sent_len != recv_len)
            printf("server: failed to send all data to client");
        
        close(clnt_sock);
    }
    
    close(serv_sock);
    exit(0);
}