#include <stdio.h>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ctime>
#include <cstring>
#define BUF_SIZE 100
#define SEND_NUM 1


int main(){

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET; 
    // test pingpong-server
    // serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  
    // serv_addr.sin_port = htons(25502); //local port

    // connect remote server
    serv_addr.sin_addr.s_addr = inet_addr("34.176.172.133");  
    serv_addr.sin_port = htons(1234); //local port
    char bufSend[BUF_SIZE] = {0};
    char bufRecv[BUF_SIZE] = {0};

    int receive_num=0;
    std::clock_t start,end;
    start=clock();
    for(int i=0;i<SEND_NUM;i++){
        //创建套接字
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))!=0){
            printf("connect failed\n");
        } 

        // printf("Input a string: ");
        // fgets(bufSend, BUF_SIZE, stdin);
        // strcpy(bufSend,s);
        // std::stringstream strs;
        // strs<<i;
        // std::string tmp=strs.str();
        // strcpy(bufSend,(char*)tmp.c_str());

        strcpy(bufSend, "hello");

        printf("Message sent: %s\n", bufSend);
        int n_sent = send(sock, bufSend, strlen(bufSend)+1, 0);
        printf("Message sent: %d bytes\n", n_sent);

        int n_recv = recv(sock, bufRecv, BUF_SIZE, 0);
        printf("Message received: %d bytes\n", n_recv);

        if(strcmp(bufSend, bufRecv)==0) receive_num++;
        printf("Message from server: %s\n", bufRecv);
       
        memset(bufSend, 0, BUF_SIZE);  
        memset(bufRecv, 0, BUF_SIZE);  
        close(sock);  
    }
    end=clock();
    printf("Send package num:%d\n",SEND_NUM);
    printf("Received package num:%d\n",receive_num);
    printf("Time takes %ld ms\n",(end-start));
    

    return 0;
}