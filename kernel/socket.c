#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "socket.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/debug.h"
#include "lwip/inet.h"

struct socket sockets[NSOCK];

struct {
    struct spinlock lock;
    int sem;
} dns_sem;

// initialize socket module, called from main.c
void sockinit(void)
{
    // initialize the global DNS semaphore
    initlock(&dns_sem.lock, "dns");
    dns_sem.sem = 0;
}

static void sem_wait(struct spinlock *lock, int *sem)
{
    acquire(lock);
    while (*sem == 0) {
        sleep(sem, lock);
    }
    *sem = 0;
    release(lock);
}

static void sem_signal(struct spinlock *lock, int *sem)
{
    acquire(lock);
    *sem = 1;
    wakeup(sem);
    release(lock);
}


/* CALLBACK FUNCTIONS */


err_t sock_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct socket *sock = (struct socket *)arg;

    // received EOF: connection closed by peer
    if (p == NULL) {
        sock->eof_reached = 1;
        printf("sock_recv: received EOF\n");
        sem_signal(&sock->lock, &sock->recv_sem);
        return ERR_OK;
    }
    
    // if p->len is larger than the available space in the ring buffer, store the packet for later
    int avail_space = RECV_BUFLEN - (sock->recv_avail - sock->recv_used + 1);
    if (p->len > avail_space) {
        printf("sock_recv: no sufficient space in recv_buf\n");
        printf("sock_recv: p->len = %d, avail_space = %d\n", p->len, avail_space);
        return ERR_MEM;  // data will be stored in lwip's internal buffer
    }
    
    // copy data from pbuf to socket ring buffer
    int avail_ptr = sock->recv_avail % RECV_BUFLEN;
    if (avail_ptr + p->len >= RECV_BUFLEN) {
        int len1 = RECV_BUFLEN - avail_ptr - 1;
        int len2 = p->len - len1;
        memmove(sock->recv_buf + avail_ptr + 1, p->payload, len1);
        memmove(sock->recv_buf, p->payload + len1, len2);
    } else {
        memmove(sock->recv_buf + avail_ptr + 1, p->payload, p->len);
    }
    sock->recv_avail += p->len;

    printf("sock_recv: read %d bytes\n", p->len);

    // inform lwip that we have read some data
    tcp_recved(sock->pcb, p->tot_len);
    
    // free the pbuf
    pbuf_free(p);

    // signal the socket
    sem_signal(&sock->lock, &sock->recv_sem);

    return ERR_OK;
}

// callback function called when some data has been acknowledged by the remote host
err_t sock_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct socket *sock = (struct socket *)arg;
    
    sock->sent_len += len;

    printf("sock_sent: sent %d bytes, waking up process\n", len);

    // wake up the process that is waiting for the data to be sent
    sem_signal(&sock->lock, &sock->sem);
    
    return ERR_OK;
}

// called periodically from the scheduler thread
err_t sock_poll(void *arg, struct tcp_pcb *tpcb)
{
    // struct socket *sock = (struct socket *)arg;
    return ERR_OK;
}

// callback function called when a connection could not be properly established
void sock_err(void *arg, err_t err)
{
    struct socket *sock = (struct socket *)arg;

    // if this assert fails, we may need to consider more cases
    LWIP_ASSERT("sock_connect_err: invalid socket state", sock->state == SS_CONNECTING);

    printf("sock_err: connection failed: err = %d, waking up process\n", err);

    // set socket state from SS_CONNECTING to SS_UNCONNECTED
    sock->state = SS_UNCONNECTED;
    sem_signal(&sock->lock, &sock->sem);
}

// callback function called when a connection is established
err_t sock_connected(void *arg, struct tcp_pcb *tpcb, err_t err) 
{
    struct socket *sock = (struct socket *)arg;

    // the err argument is not used: always ERR_OK
    LWIP_ASSERT("sock_connected: err != ERR_OK", err == ERR_OK);
    
    // set socket state from SS_CONNECTING to SS_CONNECTED
    LWIP_ASSERT("sock_connected: invalid socket state", sock->state == SS_CONNECTING);
    sock->state = SS_CONNECTED;

    // wake up the process that is waiting for the connection to be established
    printf("sock_connected: connection established, waking up process\n");
    sem_signal(&sock->lock, &sock->sem);
    
    return ERR_OK;
}

static void sock_setup_callbacks(struct socket *sock);

// callback function called when a connection is accepted or an error occurs
err_t sock_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    struct socket *sock = (struct socket *)arg;

    LWIP_ASSERT("sock_accept: invalid socket state", sock->state == SS_LISTENING);

    if (err == ERR_MEM) {
        printf("sock_accept: no memory available for the new pcb\n");
        return ERR_OK;  // no need to abort the connection
    }
    if (err != ERR_OK) {
        printf("sock_accept: unrecognized error %d\n", err);
        if (newpcb != NULL) {
            tcp_abort(newpcb);
        }
        return ERR_ABRT;  // abort the connection
    }

    // print remote IP and port in the newpcb
    printf("sock_accept: accepted new connection from %s:%d\n",
        inet_ntoa(newpcb->remote_ip), newpcb->remote_port);

    // allocate a new socket for the new connection
    int newsockfd = sockalloc(sock->domain, sock->type, sock->protocol, newpcb, sock->owner);
    if (newsockfd < 0) {
        printf("sock_accept: failed to allocate new socket, aborting connection\n");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    printf("new socket allocated: %d\n", newsockfd);

    // set up callbacks for the new socket
    struct socket *newsock = sock->owner->ofile[newsockfd]->sock;
    newsock->state = SS_CONNECTED;
    sock_setup_callbacks(newsock);

    // save the new pcb in the socket temporarily
    // the pcb will be saved in the connected socket later
    sock->accept_pcb = newpcb;
    sock->accept_fd = newsockfd;

    // set socket state from SS_LISTENING to SS_ACCEPTING
    sock->state = SS_ACCEPTING;

    // wake up the process that called accept()
    // and is waiting for an incoming connection
    sem_signal(&sock->lock, &sock->sem);

    return ERR_OK;
}

// callback function called when a hostname is found or an error occurs (failure/timeout)
void sock_dns_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    ip_addr_t *addr = (ip_addr_t *)callback_arg;

    if (ipaddr == NULL) {
        printf("sock_dns_found: failed to resolve hostname %s\n", name);
        sem_signal(&dns_sem.lock, &dns_sem.sem);
        return;
    }

    // return the resolved IP address
    addr->addr = ipaddr->addr;

    printf("sock_dns_found: resolved hostname %s to %s\n", name, ipaddr_ntoa(ipaddr));

    // wake up the process that called gethostbyname()
    sem_signal(&dns_sem.lock, &dns_sem.sem);
}

static void sock_setup_callbacks(struct socket *sock)
{
    tcp_arg(sock->pcb, sock);
    tcp_recv(sock->pcb, sock_recv);
    tcp_sent(sock->pcb, sock_sent);
    tcp_poll(sock->pcb, sock_poll, 1);
    tcp_err(sock->pcb, sock_err);
}

static void sock_setup_callbacks_accept(struct socket *sock)
{
    tcp_arg(sock->pcb, sock);
    tcp_accept(sock->pcb, sock_accept);
}


/* APIS FOR SERVER & CLIENT */


static int initsock(struct socket *sock)
{
    sock->domain = 0;
    sock->type = 0;
    sock->protocol = 0;
    
    sock->state = SS_UNCONNECTED;
    
    // per-socket mutex used to implement semaphore
    initlock(&sock->lock, "socket");
    
    sock->pcb = NULL;
    sock->accept_pcb = NULL;
    sock->accept_fd = -1;

    // ring buffer for received data
    sock->recv_avail = -1;
    sock->recv_used = 0;
    sock->eof_reached = 0;

    sock->owner = NULL;

    sock->file = NULL;
    sock->fd = -1;

    sock->sem = 0;
    sock->recv_sem = 0;

    return 0;
}

// called from sys_socket() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/socket.2.html
// returns a file descriptor on success, or -1 on error
int sockalloc(int domain, int type, int protocol, struct tcp_pcb *pcb, struct proc *p) {
    LWIP_ASSERT("sockalloc: invalid domain", domain == AF_INET);
    LWIP_ASSERT("sockalloc: invalid type", type == SOCK_STREAM);
    LWIP_ASSERT("sockalloc: invalid protocol", protocol == 0);  // TODO: make this an enum: IPPROTO_TCP

    // allocate a free socket
    int sock_idx;
    for (sock_idx = 0; sock_idx < NSOCK; sock_idx++)
        if (sockets[sock_idx].state == SS_FREE)
            break;

    if (sock_idx == NSOCK) {
        printf("sockalloc: no free sockets\n");
        return -1;
    }

    // initialize socket fields
    struct socket *s = &sockets[sock_idx];
    initsock(s);
    s->domain = domain;
    s->type = type;
    s->protocol = protocol;
    s->pcb = pcb == NULL ? tcp_new() : pcb;
    s->owner = p == NULL ? myproc() : p;

    // allocate a fd for the socket
    struct file *f = filealloc();
    int fd = fdalloc_for_proc(f, s->owner);
    if (fd < 0) {
        printf("sockalloc: no free fd\n");
        return -1;
    }
    f->type = FD_SOCK;
    f->sock = s;
    f->readable = 1;
    f->writable = 1;
    s->file = f;
    s->fd = fd;

    return fd;
}

// called from fileread() in kernel/file.c
// https://man7.org/linux/man-pages/man2/read.2.html
// returns the number of bytes read on success, or -1 on error
int sockread(struct socket *sock, uint64 addr, int n) 
{
    LWIP_ASSERT("sockread: invalid socket state", sock->state == SS_CONNECTED);

    // save recv_avail in case it is changed by the scheduler thread
    // no need to save recv_used because it is only changed by this thread
    int recv_avail = sock->recv_avail;
    int num_avail = recv_avail - sock->recv_used + 1;
    
    // EOF received and all data has been read
    // returning 0 indicates EOF to the user application
    if (sock->eof_reached && num_avail == 0)
        return 0;

    // we only implement socket in blocking mode
    // EOF not received and no data available, so wait for some data
    if (num_avail == 0) {
        // will be woken up by tcp_poll() when data is available or EOF is received
        sock->state = SS_RECVING;

        printf("sockread: waiting for some data\n");
        sem_wait(&sock->lock, &sock->recv_sem);

        sock->state = SS_CONNECTED;

        // some data is available or EOF is received
        // read in updated recv_avail
        num_avail = sock->recv_avail - sock->recv_used + 1;

        // no new data is available, so EOF is received
        if (num_avail == 0)
            return 0;
    }

    // copy data from socket ring buffer to user buffer
    int to_read = num_avail < n ? num_avail : n;
    pagetable_t pt = myproc()->pagetable;
    int used_ptr = sock->recv_used % RECV_BUFLEN;
    
    // copy data in two parts if the data wraps around the end of the ring buffer
    if (used_ptr + to_read >= RECV_BUFLEN) {
        int len1 = RECV_BUFLEN - used_ptr;
        int len2 = to_read - len1;
        if (copyout(pt, addr, (char *)sock->recv_buf + used_ptr, len1) < 0 ||
            copyout(pt, addr + len1, (char *)sock->recv_buf, len2) < 0) {
            printf("sockread: copyout failed\n");
            return -1;
        }
    } else {
        if (copyout(pt, addr, (char *)sock->recv_buf + used_ptr, to_read) < 0) {
            printf("sockread: copyout failed\n");
            return -1;
        }
    }

    // update recv_used pointer
    sock->recv_used += to_read;

    return to_read;
}

// called from filewrite() in kernel/file.c
// https://man7.org/linux/man-pages/man2/write.2.html
// returns the number of bytes written on success, or -1 on error
int sockwrite(struct socket *sock, uint64 addr, int n) 
{
    LWIP_ASSERT("sockwrite: invalid socket state", sock->state == SS_CONNECTED);

    // copy data from user space to kernel space
    if (n > SEND_BUFLEN) {
        printf("sockwrite: data too large (n < %d)\n", SEND_BUFLEN);
        return -1;
    }
    
    if (copyin(myproc()->pagetable, (char *)sock->send_buf, addr, n) < 0) {
        printf("sockwrite: copyin failed\n");
        return -1;
    }

    // number of bytes sent so far
    // we do not zero this out when the send buffer is emptied
    int sent_len_old = sock->sent_len;

    // send data
    int sent_len = 0;
    int written_len = 0;
    err_t write_err, output_err;

    printf("sockwrite: sending %d bytes\n", n);

    // send data in chunks due to limited send buffer size
    // TODO: try to simplify this double loop
    while (n-written_len > 0) {
        while (1) {
            // length of available space in send buffer
            int avail_buf_len = tcp_sndbuf(sock->pcb); 

            // amount of data to send in this iteration
            int to_write_len = n-written_len > avail_buf_len ? avail_buf_len : n-written_len;

            // check if this is the last packet
            // TODO: TCP_WRITE_FLAG_MORE: do buffering except for the last packet
            uint8 write_flags = written_len+to_write_len == n ? TCP_WRITE_FLAG_COPY : TCP_WRITE_FLAG_MORE | TCP_WRITE_FLAG_COPY;
            write_err = tcp_write(sock->pcb, sock->send_buf+written_len, to_write_len, write_flags);

            // if ERR_MEM: wait until some of the currently enqueued data has been successfully received
            if (write_err == ERR_MEM) {
                printf("sockwrite: tcp_write failed (ERR_MEM)\n");

                // send buffer is full, try to send some data to make room
                output_err = tcp_output(sock->pcb);
                if (output_err != ERR_OK) {
                    printf("sockwrite: tcp_output failed\n");
                    return sent_len;
                }

                // will be woken up by sock_sent() when some data has been acknowledged
                sem_wait(&sock->lock, &sock->sem);

                // update number of bytes sent in this invocation
                // sock->sent_len is updated by sock_sent()
                sent_len = sock->sent_len - sent_len_old;

                // retry tcp_write() if it failed due to insufficient memory
                // there should be some free space in the send buffer now
                continue;
            }

            // we don't handle other errors for now
            if (write_err != ERR_OK) {
                printf("sockwrite: tcp_write failed: %d\n", write_err);
                return sent_len;
            }

            // successfully written to send buffer
            written_len += to_write_len;
            break;
        }
    }

    // some data may still be in the send buffer
    output_err = tcp_output(sock->pcb);
    if (output_err != ERR_OK) {
        printf("sockwrite: tcp_output failed\n");
        return sent_len;
    }

    // will be woken up by sock_sent() when some data has been acknowledged
    // TODO: wake this process up in tcp_poll in case of missed wakeup
    printf("sockwrite: waiting for data to be acknowledged\n");
    sem_wait(&sock->lock, &sock->sem);

    // update number of bytes sent in this invocation
    // sock->sent_len is updated by sock_sent()
    sent_len = sock->sent_len - sent_len_old;

    // all data are successfully sent
    LWIP_ASSERT("sockwrite: sent_len != n", sent_len == n);
    return n;
}

// called from fileclose() in kernel/file.c
void sockclose(struct socket *sock)
{
    // socket could be in any state

    // unset callbacks
    tcp_recv(sock->pcb, NULL);
    tcp_sent(sock->pcb, NULL);
    tcp_err(sock->pcb, NULL);
    tcp_poll(sock->pcb, NULL, 0);
    tcp_accept(sock->pcb,NULL);

    // free file descriptor
    myproc()->ofile[sock->fd] = 0;
    
    // TODO: The function may return ERR_MEM if no memory 
    // was available for closing the connection. 
    // If so, the application should wait and try again 
    // either by using the acknowledgment callback or 
    // the polling functionality.

    // close connection and free pcb
    err_t err;
    if ((err = tcp_close(sock->pcb)) != ERR_OK) {
        printf("sockclose: tcp_close failed\n");
    }

    // free socket
    sock->state = SS_FREE;
    sock->pcb = NULL;   // should not be referenced anymore after tcp_close()
}


/* APIS FOR CLIENT */


// called from sys_connect() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/connect.2.html
// returns 0 on success, or -1 on error
int sockconnect(int sockfd, const struct sockaddr *addr, int addrlen) 
{
    struct socket *sock = myproc()->ofile[sockfd]->sock;
    if (sock == NULL) {
        printf("sockconnect: invalid socket\n");
        return -1;
    }
    
    // set socket state from SS_UNCONNECTED to SS_CONNECTING
    LWIP_ASSERT("sockconnect: invalid socket state", sock->state == SS_UNCONNECTED);
    sock->state = SS_CONNECTING;
    
    sock_setup_callbacks(sock);

    ip_addr_t ipaddr = {addr->sin_addr};
    err_t err = tcp_connect(sock->pcb, &ipaddr, ntohs(addr->sin_port), sock_connected);
    if (err != ERR_OK) {
        printf("sockconnect: tcp_connect failed: %d\n", err);
        return -1;
    }

    // will be woken up by sock_connected() when the connection is established
    sem_wait(&sock->lock, &sock->sem);

    // check if the connection was established
    if (sock->state != SS_CONNECTED) {
        printf("sockconnect: connection failed\n");
        return -1;
    }

    return 0;
}


/* APIS FOR SERVER */


// called from sys_bind() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/bind.2.html
// returns 0 on success, or -1 on error
int sockbind(int sockfd, const struct sockaddr *addr, int addrlen)
{
    struct socket *sock = myproc()->ofile[sockfd]->sock;
    if (sock == NULL) {
        printf("sockbind: invalid socket\n");
        return -1;
    }

    LWIP_ASSERT("sockbind: invalid socket state", sock->state == SS_UNCONNECTED);
    LWIP_ASSERT("sockbind: invalid address family", addr->sa_family == AF_INET);

    // bind socket to port
    ip_addr_t ipaddr = {addr->sin_addr};

    err_t err = tcp_bind(sock->pcb, &ipaddr, addr->sin_port);

    if (err == ERR_USE) {
        printf("sockbind: port %d already in use\n", addr->sin_port);
        return -1;
    }
    if (err != ERR_OK) {  // other errors
        printf("sockbind: tcp_bind failed\n");
        return -1;
    }

    return 0;
}

// called from sys_listen() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/listen.2.html
// enable backlog for the socket: set TCP_LISTEN_BACKLOG=1 in lwipopts.h
// returns 0 on success, or -1 on error
int socklisten(int sockfd, int backlog)
{
    struct socket *sock = myproc()->ofile[sockfd]->sock;
    if (sock == NULL) {
        printf("socklisten: invalid socket\n");
        return -1;
    }

    LWIP_ASSERT("socklisten: invalid socket state", sock->state == SS_UNCONNECTED);

    // listen for incoming connections
    printf("listen: local addr %d\n",sock->pcb->local_ip.addr);
    printf("listen: remote addr %d\n",sock->pcb->remote_ip.addr);
    struct tcp_pcb *lpcb = tcp_listen_with_backlog(sock->pcb, backlog);
    if (lpcb == NULL) {
        // no memory was available for the listening connection
        printf("socklisten: tcp_listen_with_backlog failed\n");
        return -1;
    }

    // replace the PCB in the socket with the listening PCB
    // the old PCB is freed by tcp_listen_with_backlog()
    sock->pcb = lpcb;
    sock->state = SS_LISTENING;
    sock->file->readable = 0;
    sock->file->writable = 0;

    return 0;
}

// called from sys_accept() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man2/accept.2.html
// returns a new socket on success, or -1 on error
int sockaccept(int sockfd, struct sockaddr *addr, int *addrlen)
{
    struct socket *sock = myproc()->ofile[sockfd]->sock;
    if (sock == NULL) {
        printf("sockaccept: invalid socket\n");
        return -1;
    }
    LWIP_ASSERT("sockaccept: invalid socket state", sock->state == SS_LISTENING);

    sock_setup_callbacks_accept(sock);

    // will be woken up by sock_accept() when a connection is established
    // the new connected PCB is stored in sock->accept_pcb
    sem_wait(&sock->lock, &sock->sem);

    // check if an incoming connection was accepted
    if (sock->state != SS_ACCEPTING) {
        printf("sockaccept: failed to accept new connection\n");
        return -1;
    }

    // get the file descriptor of the new socket
    int newsockfd = sock->accept_fd;

    // copyout is handled in sys_accept()
    if (addr != NULL && addrlen != NULL) {
        addr->sa_family = sock->domain;                         // always AF_INET
        addr->sin_port = htons(sock->accept_pcb->remote_port);  // convert to network byte order
        addr->sin_addr = sock->accept_pcb->remote_ip.addr;      // already in network byte order
        *addrlen = sizeof(struct sockaddr);
    }

    // reset temporary fields in the socket
    sock->accept_pcb = NULL;
    sock->accept_fd = -1;

    // set socket state from SS_ACCEPTING to SS_LISTENING
    // TODO: try to remove SS_ACCEPTING
    LWIP_ASSERT("sockaccept: invalid socket state", sock->state == SS_ACCEPTING);
    sock->state = SS_LISTENING;

    return newsockfd;
}


/* APIS FOR DNS */


// called from sys_gethostbyname() in kernel/sysfile.c
// https://man7.org/linux/man-pages/man3/gethostbyname.3.html
// populates the sockaddr struct with one IP address of the host
// returns 0 on success, or -1 on error
int sockgethostbyname(const char *name, struct sockaddr *addr)
{
    // set DNS server address to Google's public DNS server
    ip_addr_t dns_server = {
        .addr = DNS_SERVER_IP,
    };
    dns_setserver(0, &dns_server);

    // resolve hostname
    ip_addr_t ipaddr = {0};
    err_t err = dns_gethostbyname(name, &ipaddr, sock_dns_found, &ipaddr);

    if (err == ERR_OK) {
        // address already cached, addr->sin_addr set to the cached address
        printf("sockgethostbyname: found cached address for %s: %s\n", name, ipaddr_ntoa(&ipaddr));
        addr->sin_addr = ipaddr.addr;
        return 0;
    }
    if (err != ERR_INPROGRESS) {
        printf("sockgethostbyname: failed to resolve hostname %s\n", name);
        return -1;
    }

    printf("sockgethostbyname: waiting for hostname %s to be resolved\n", name);

    // enqueue a request to be sent to the DNS server for resolution
    // wait for the DNS server to respond
    // will be woken up by sock_dns_found() when the hostname is resolved
    sem_wait(&dns_sem.lock, &dns_sem.sem);

    if (ipaddr.addr == 0) {
        printf("sockgethostbyname: failed to resolve hostname %s\n", name);
        return -1;
    }

    addr->sin_addr = ipaddr.addr;
    
    return 0;
}

//called from sys_socketinetaddress
int sockinetaddress(const char *name, struct sockaddr *addr){
    addr->sin_addr = inet_addr(name);
    return 0;
}