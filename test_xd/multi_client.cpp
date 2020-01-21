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
#include <pthread.h>
#include <sys/syscall.h>
// 统计时间 clock
#include <time.h>
// gettimeofday
#include <sys/time.h>

#define REQ_NUM 10000
int g_reqNum = REQ_NUM;
pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
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
		// printf("buf:[%s], len:%d\n", buf, strlen(buf));
        if (recvsize >= remaining)
        {
            // printf("recv end\n");
            break;
        }
	}

    close(fd);
    return 0;
}

#define gettid() syscall(__NR_gettid)
void* thread_func(void*)
{
    while(1)
    {
        pthread_mutex_lock(&g_mtx);
        printf("cur:%d, g_reqNum:%d\n", REQ_NUM-g_reqNum, g_reqNum);
        if (g_reqNum <= 0)
        {
            pthread_mutex_unlock(&g_mtx);
            printf("exit thread[%d]\n", gettid());
            return NULL;
        }
        --g_reqNum;
        pthread_mutex_unlock(&g_mtx);
       
        do_process();
        usleep(100);
    }
    return NULL;
}

int main(int c, char **v)
{
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    gettimeofday(&tv,NULL);
    long timems1 = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    clock_t begin = clock();
    // 创建10个线程发送客户端请求
    for (int i=0; i<10; i++)
    {
        printf("create [%d]thread\n", i);
        pthread_t tid;
        pthread_attr_t attr;
        if (0 != pthread_attr_init(&attr))
        {
            perror("pthread_atter_init");
            return -1;
        }
        if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
        {
            perror("pthread_attr_setdetachstate");
            return -1;
        }
        if (0 != pthread_create(&tid, &attr, thread_func, NULL))
        {
            perror("pthread_create");
            return -1;
        }
    }

    while(1)
    {
        printf("main g_reqNum:%d\n", g_reqNum);
        pthread_mutex_lock(&g_mtx);
        if (g_reqNum <= 0)
        {
            // break时的解锁，容易忘记
            pthread_mutex_unlock(&g_mtx);
            break;
        }
        pthread_mutex_unlock(&g_mtx);
        usleep(100000);
    }
    clock_t end = clock();

    memset(&tv, 0, sizeof(tv));
    gettimeofday(&tv,NULL);
    long timems2 = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    // 注意占位符，%f改成%d则会影响后一个变量打印
    printf("clock() cost:%f ms, gettimeofday():%ld ms\n", (double)(end-begin)/CLOCKS_PER_SEC*1000, timems2-timems1);

    return 0;
}

