#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/spinlock.h"
#include "kernel/socket.h"
#include "user/user.h"

/* RFC 867: Daytime Protocol */
#define SERVER_HOST "utcnist.colorado.edu"
#define SERVER_PORT 13

int main(int argc, char *argv[])
{
    int sockfd, r;
    struct sockaddr addr = {
        .sa_family = AF_INET, 
        .sin_port = htons(SERVER_PORT),
    };

    printf("daytime: running DNS lookup for %s\n", SERVER_HOST);
    if ((r = gethostbyname(SERVER_HOST, &addr)) < 0) {
        printf("gethostbyname failed: %d\n", r);
        exit(1);
    }

    printf("daytime: creating socket\n");
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("daytime: socket failed: %d\n", sockfd);
        exit(1);
    }

    printf("daytime: connecting to %s\n", SERVER_HOST);
    if ((r = connect(sockfd, &addr, sizeof(addr))) < 0) {
        printf("daytime: connect failed: %d\n", r);
        exit(1);
    }

    char buf[512];
    int n;

    while (1) {
        n = read(sockfd, buf, sizeof(buf));
        printf("daytime: read %d bytes\n", n);
        if (n <= 0) break;  // EOF or error
        write(1, buf, n);
    }

    printf("daytime: closing socket\n");
    close(sockfd);
    printf("daytime: socket closed successfully\n");
    exit(0);
}