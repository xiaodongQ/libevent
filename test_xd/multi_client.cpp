/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For gethostbyname */
#include <netdb.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int do_process()
{
     const char query[] =
        "GET / HTTP/1.0\r\n"
        "Host: www.google.com\r\n"
        "\r\n";
    const char hostname[] = "www.google.com";
    struct sockaddr_in sin;
    struct hostent *h;
    const char *cp;
    int fd;
    ssize_t n_written, remaining;
    char buf[1024] = {0};

    /* Allocate a new socket */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    /* Connect to the remote host. */
    sin.sin_family = AF_INET;
    sin.sin_port = htons(40713);
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, (struct sockaddr*) &sin, sizeof(sin))) {
        perror("connect");
        close(fd);
        return 1;
    }

    /* Write the query. */
    /* XXX Can send succeed partially? */
    // 发送http请求时部分多次发送是否允许?
    cp = query;
    remaining = strlen(query);
    while (remaining) {
      n_written = send(fd, cp, remaining, 0);
      if (n_written <= 0) {
        perror("send");
        return 1;
      }
      remaining -= n_written;
      cp += n_written;
    }

	/* Get an answer back. */
    remaining = strlen(query); 
    ssize_t recvsize = 0;
	while (1) {
		ssize_t result = recv(fd, buf, sizeof(buf), 0);
		if (result == 0) {
			break;
		} else if (result < 0) {
			perror("recv");
			close(fd);
			return 1;
		}
        recvsize += result;
		printf("buf:[%s], len:%d\n", buf, strlen(buf));
        if (recvsize >= remaining)
        {
            printf("recv end\n");
            break;
        }
	}

    close(fd);
    return 0;
}

#define REQ_NUM 1026
int main(int c, char **v)
{
    int num = REQ_NUM;
    while (num > 0)
    {
        printf("index:%d\n", REQ_NUM-num);
        --num;
        do_process();
        usleep(5000);
    }
    return 0;
}

