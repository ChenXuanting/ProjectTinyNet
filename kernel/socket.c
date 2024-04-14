#include "types.h"
#include "riscv.h"
#include "defs.h"
// #include "param.h"
// #include "spinlock.h"
#include "proc.h"
// #include "fs.h"
// #include "sleeplock.h"
#include "file.h"
#include "socket.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/tcp.h"

#define BUFFER_LENGTH 50
typedef struct socket_buffer_type{
    struct pbuf *p;
    // int socket_fd;
    int state;
}socket_buffer_type;
socket_buffer_type socket_buffer[BUFFER_LENGTH];
// struct socket_list{
//     int socket_fd;
//     struct socket_list * next;
// };

err_t recv_tcp (void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    err_t serr=0;
    int i=0;
    for(;i<BUFFER_LENGTH;i++){
        if(socket_buffer[i].state==0){
            memcpy(socket_buffer[i].p,p,p->len);
            return serr;
        }
    }
    return serr;
}
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}
// called from sys_socket() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/socket.2.html
int sockalloc(int domain, int type, int protocol) {
    struct file * sock_file=filealloc();
    sock_file->type=FD_SOCK;
    //socket generation
    sock_file->sock=(struct socket *)kalloc();
    sock_file->sock->file=sock_file;
    sock_file->sock->pcb=tcp_new();
    sock_file->sock->type=type;
    sock_file->sock->sin_family=domain;
    sock_file->sock->socket_fd=fdalloc(sock_file);
    // TODO: save domain protocol
    //tcp_recv (struct tcp_pcb *pcb , err_t (*recv) (void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) );
    tcp_recv(sock_file->sock->pcb,&recv_tcp);
    return sock_file->sock->socket_fd;
}

// called from fileread() in kernel/file.c
int sockread(struct socket *sock, uint64 addr, int n) 
{
    struct proc *p = myproc();
    int i=0;
    for(;i<BUFFER_LENGTH;i++){
        if(socket_buffer[i].state==1){
            copyout(p->pagetable, addr, socket_buffer[i].p->payload, n); 
            return n;
        }
    }
    return 0;
}

// called from filewrite() in kernel/file.c
int sockwrite(struct socket *sock, uint64 addr, int n) 
{
    return 0;
}

// called from fileclose() in kernel/file.c
void sockclose(struct socket *sock)
{
    // close sock pcb? & ref-1?
    /*
    tcp_close	(	struct tcp_pcb * 	pcb	)	

    */
   tcp_close(sock->pcb);
   fileclose(sock->file);
}

// called from sys_connect() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/connect.2.html
int sockconnect(int sockfd, const struct sockaddr *addr, int addrlen) 
{
/*
err_t tcp_connect	(	struct tcp_pcb * 	pcb,
const ip_addr_t * 	ipaddr,
u16_t 	port,
tcp_connected_fn 	connected 
)	
*/
    return 0;
}

// called from sys_bind() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/bind.2.html
/*
err_t tcp_bind	(	struct tcp_pcb * 	pcb,
const ip_addr_t * 	ipaddr,
u16_t 	port )	
*/
int sockbind(struct socket *sock, const struct sockaddr * addr, int addrlen)
{
    uint16 u_port;
    if(sock->sin_family==AF_INET)
        u_port=80;
    if(tcp_bind(sock->pcb,&(addr->sin_addr),u_port))
        return -1;
    return 0;
}

//tcp_listen(pcb)   tcp_listen_with_backlog(pcb, TCP_DEFAULT_LISTEN_BACKLOG)
/*struct tcp_pcb* tcp_listen_with_backlog	(	struct tcp_pcb * 	pcb,
u8_t 	backlog 
)		
*/

// called from sys_listen() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/listen.2.html
int socklisten(struct socket *sock, int backlog)
{
    if(tcp_listen(sock->pcb))
        return -1;
    return 0;
}

// called from sys_accept() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/accept.2.html
int sockaccept(int sockfd, struct sockaddr *addr, int *addrlen)
{
/*
void tcp_accept	(	struct tcp_pcb * 	pcb,
tcp_accept_fn 	accept 
)	
*/
    return 0;
}

//  ssize_t recv(int sockfd, void *buf, size_t len, int flags);