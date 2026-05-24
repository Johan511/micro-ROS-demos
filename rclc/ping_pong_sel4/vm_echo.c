#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(12345),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    printf("EchoServer listening on UDP port 12345\n");

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(54321);
    dest.sin_addr.s_addr = inet_addr("10.0.2.100");
    socklen_t destlen = sizeof(dest);

    char buf[1500] = {0};

    while (1) {
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&src, &srclen);
        if (n > 0) {
            buf[n] = '\0';
            printf("EchoServer received %d bytes: %s\n", n, buf);
            sendto(sock, buf, n, 0, (struct sockaddr*)&src, srclen);
        }
        else
            printf("recvfrom() returned %d\n", n);
    }
}
