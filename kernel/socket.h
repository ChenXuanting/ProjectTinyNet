// lwip/sockets.h
/* Socket address family */
#define AF_INET         2

/* Socket protocol types (TCP/UDP) */
#define SOCK_STREAM     1
#define SOCK_DGRAM      2

// linux: include/uapi/linux/net.h
typedef enum {
	SS_FREE = 0,			// not allocated
	SS_UNCONNECTED,			// unconnected to any socket
	SS_CONNECTING,			// in process of connecting
    SS_LISTENING,           // in listen mode
    SS_ACCEPTING,           // accepting incoming connection
	SS_CONNECTED,			// connected to socket
    SS_SENDING,             // sending data
    SS_RECVING,             // receiving data
} socket_state;

#define SEND_BUFLEN 1024
#define RECV_BUFLEN 1024

struct socket {
    int domain;                     // address family, always AF_INET
    int type;                       // socket type, SOCK_STREAM or SOCK_DGRAM
    int protocol;                   // always 0
    socket_state state;             // socket state

    struct spinlock lock;           // socket lock
    struct tcp_pcb *pcb;
    struct tcp_pcb *accept_pcb;     // for listening sockets
    int accept_fd;                  // for listening sockets

    int sent_len;                   // total number of bytes sent
    uint8 send_buf[SEND_BUFLEN];    // send buffer

    int recv_avail;                 // pointer to the next available byte in recv_buf
    int recv_used;                  // pointer to the next byte to be read from recv_buf
    int eof_reached;                // end of file reached
    uint8 recv_buf[RECV_BUFLEN];    // receive buffer

    struct proc *owner;             // process that owns this socket

    struct file *file;              // file pointer
    int fd;                         // file descriptor

    int sem;                        // semaphore for async operations, protected by socket lock
    int recv_sem;                   // semaphore for async recv operations, protected by socket lock
};

struct sockaddr
{
    uint16 sa_family;       // address family, AF_INET
    uint16 sin_port;        // port in network byte order
    uint32 sin_addr;        // address in network byte order
    uint8 sin_zero[8];      // zero this if you want to
};

// fixed DNS server address
#define MAX_DOMAIN_NAME 256
#define MAX_ADDRESS_LENGTH 256
#define DNS_SERVER_IP 0x08080808    // Google DNS server