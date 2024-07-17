#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/spinlock.h"
#include "kernel/socket.h"
#include "user/user.h"

#define BUF_SIZE 100
#define SEND_NUM 10

#define SERVER_HOST "34.176.172.133"
#define SERVER_PORT 1234

int main(){

    struct sockaddr serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr)); 
    serv_addr.sa_family = AF_INET; 
    serv_addr.sin_port = htons(SERVER_PORT); 
    inetaddress(SERVER_HOST,&serv_addr); 

    char bufSend[BUF_SIZE] = {0};
    char bufRecv[BUF_SIZE] = {0};
    int receive_num=0;

    //count time cost
    uint start,end;
    start=timenow();
    
    for(int i=0;i<SEND_NUM;i++){

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        printf("starting connection.\n");
        if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))!=0){
            printf("connect failed\n");
        } 
        printf("connection sucessful\n");
        char* tmp ="Hello world!";
        strcpy(bufSend,tmp);
        printf("Time %d", i);
        printf("Message sent from xv6: %s\n", bufSend);

        write(sock, bufSend, strlen(bufSend)+1);
        read(sock, bufRecv, BUF_SIZE);

        if(strcmp(bufSend,bufRecv)==0) receive_num++;
        printf("Message form server: %s\n", bufRecv);
       
        memset(bufSend, 0, BUF_SIZE);  
        memset(bufRecv, 0, BUF_SIZE); 
        close(sock); 
    }

    end=timenow();
    printf("\n");
    printf("Send package num:%d\n",SEND_NUM);
    printf("Received package num:%d\n",receive_num);
    printf("Time cost: %d ms\n", (end-start)*10);
    exit(0);
}