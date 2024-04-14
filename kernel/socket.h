#include<lwip/inet.h>
#include<lwip/ip4_addr.h>
typedef enum  {AF_INET=0,AF_INET6,AF_LOCAL,AF_ROUTE} sa_family_t;
struct socket {
 // socket_state state; //Connect
  short type; 
  sa_family_t sin_family; 
 // unsigned long flags;
 // struct socket_wq __rcu * wq; //spin
 
  struct file * file;
 // struct sock * sk;
//   uint16 sin_port;
  struct tcp_pcb * pcb;
  int socket_fd;
//   const struct proto_ops * ops;
};  


// struct sockaddr {
// };

// typedef enum sa_family_t {AF_INET,AF_INET6,AF_LOCAL,AF_ROUTE};
// typedef enum sock_type {SOCK_STREAM,SOCK_DGRAM,SOCK_RAW,SOCK_PACKET,SOCK_SEQPACKET};
// typedef enum sock_prot {IPPROTO_TCP,IPPTOTO_UDP,IPPROTO_SCTP,IPPROTO_TIPC};

// /* Internet address. */ 
// struct in_addr { uint32_t s_addr; /* address in network byte order */ }; 

struct sockaddr { 
    sa_family_t sin_family; /* address family: AF_INET */ 
    uint16 sin_port; /* port in network byte order */ 
    struct ip4_addr sin_addr; /* internet address */ 
}; 
    


// struct cmsghdr {
//     socklen_t     cmsg_len;     /* data byte count, including hdr */
//     int           cmsg_level;   /* originating protocol */
//     int           cmsg_type;    /* protocol-specific type */
// /* followed by
//     unsigned char cmsg_data[]; */
// };


	// struct hostent
	// {
	// 	char *h_name;         //正式主机名
	// 	char **h_aliases;     //主机别名
	// 	int h_addrtype;       //主机IP地址类型：IPV4-AF_INET
	// 	int h_length;		  //主机IP地址字节长度，对于IPv4是四字节，即32位
	// 	char **h_addr_list;	  //主机的IP地址列表
	// };
	
	// #define h_addr h_addr_list[0]   //保存的是IP地址
