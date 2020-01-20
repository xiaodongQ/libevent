/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
/* For fcntl */
#include <fcntl.h>
/* for poll */
#include <poll.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define MAX_LINE 16384

char
rot13_char(char c)
{
    /* We don't want to use isalpha here; setting the locale would change
     * which characters are considered alphabetical. */
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
        return c + 13;
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
        return c - 13;
    else
        return c;
}

struct fd_state {
    // 要发送的全部数据buf
    char buffer[MAX_LINE];
    // buf中有效数据长度
    size_t buffer_used;

    // 是否正在发送(读取数据结束后置1，发送结束后置0)
    int writing;
    // 已经发送的数据长度
    size_t n_written;
    // 总共要发送的数据长度
    size_t write_upto;
};

struct fd_state *
alloc_fd_state(void)
{
    struct fd_state *state = (fd_state *)malloc(sizeof(struct fd_state));
    if (!state)
        return NULL;
    state->buffer_used = state->n_written = state->writing =
        state->write_upto = 0;
    return state;
}

void
free_fd_state(struct fd_state *state)
{
    free(state);
}

void
make_nonblocking(int fd)
{
    fcntl(fd, F_SETFL, O_NONBLOCK);
}

int
do_read(int fd, struct fd_state *state)
{
    char buf[1024];
    int i;
    ssize_t result;
    while (1) {
        result = recv(fd, buf, sizeof(buf), 0);
        if (result <= 0)
            break;

        for (i=0; i < result; ++i)  {
            if (state->buffer_used < sizeof(state->buffer))
                state->buffer[state->buffer_used++] = rot13_char(buf[i]); // 收到的数据经rot13后存起来，并更新有效长度
            if (buf[i] == '\n') {
                state->writing = 1; // 读取数据结束后置1
                state->write_upto = state->buffer_used; // 读取结束，要发送的长度赋值为存在buf中的数据
            }
        }
    }

    // 和do_write中的分支写在while中效果一样
    if (result == 0) {
        return 1;
    } else if (result < 0) {
        if (errno == EAGAIN)
            return 0;
        return -1;
    }

    return 0;
}

int
do_write(int fd, struct fd_state *state)
{
    while (state->n_written < state->write_upto) {
        // 返回本次实际发送的长度
        ssize_t result = send(fd, state->buffer + state->n_written,
                              state->write_upto - state->n_written, 0);
        // send报错，考虑非阻塞的情况
        if (result < 0) {
            // 由于是非阻塞IO，可能发送队列中的数据还未全部准备好(本次只发送了部分)，返回ok等下次FD_ISSET后再来发送
            if (errno == EAGAIN)
                return 0;
            return -1;
        }
        // asset宏断言，表达式为false时会打印一条信息到stderr，并abort程序(所以为true时没影响，继续)
        // 即 若==0则报错退出，根据上面的条件分支，不应该出现发送成功0字节的情况
        assert(result != 0);

        state->n_written += result;
    }

    if (state->n_written == state->buffer_used)
        state->n_written = state->write_upto = state->buffer_used = 0;

    state->writing = 0;

    return 0;
}

void
run(void)
{
    int listener;
    // 指针数组，成员都是指针的数组，通过是否NULL来判断是否有成员
    struct fd_state *state[FD_SETSIZE];
    struct sockaddr_in sin;
    struct pollfd *fdlist = NULL;
    int i = 0;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(40713);

    for (i = 0; i < FD_SETSIZE; ++i)
    {
        state[i] = NULL;
    }

    // 初始化感兴趣pollfd的内存
    fdlist = (struct pollfd *)calloc(FD_SETSIZE, sizeof(struct pollfd));
    if (NULL == fdlist)
    {
        perror("calloc");
        return;
    }
    for (i = 0; i < FD_SETSIZE; ++i)
    {
        fdlist[i].fd = -1;
        fdlist[i].events = 0;
        fdlist[i].revents = 0;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    make_nonblocking(listener);

#ifndef WIN32
    {
        int one = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
#endif

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        free(fdlist);
        fdlist = NULL;
        return;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        free(fdlist);
        fdlist = NULL;
        return;
    }
    printf("listen[%d]...\n", ntohs(sin.sin_port));
   
    fdlist[listener].fd = listener;
    fdlist[listener].events = POLLIN;  //设置需要监听的信号
    while (1) {
        // 把感兴趣的socket加到pollfd中，传给poll
        for (i=0; i < FD_SETSIZE; ++i)
        {
            if (i == listener)
            {
                continue;
            }
            if (state[i] != NULL)
            {
                fdlist[i].fd = i;
                fdlist[i].events = POLLIN | POLLRDHUP | POLLOUT;
            }
            else
            {
                fdlist[i].fd = -1;
                fdlist[i].events = 0;
                fdlist[i].revents = 0;
            }
        }

        // 第三个参数是等待时间 ms，若为0则poll立即返回，若为-1则阻塞到有事件到来或调用被打断
        if (poll(fdlist, FD_SETSIZE, -1) < 0) {
            perror("poll");
            free(fdlist);
            fdlist = NULL;
            return;
        }

        // FD_ISSET来检测(返回true) 文件描述符集fdset(此处为readset) 中是否存在文件描述符fd(对应描述符listener)
        // 主线程创建的监听fd也用select来检测
        if (fdlist[listener].revents & POLLIN) {
            struct sockaddr_storage ss;
            socklen_t slen = sizeof(ss);
            int fd = accept(listener, (struct sockaddr*)&ss, &slen);
            if (fd < 0) {
                perror("accept");
            } else if (fd > FD_SETSIZE) { // 接收的fd不能大于FD_SETSIZE，并不是端口
                close(fd);
            } else {
                printf("accept request[ip:%s, port:%d]...\n", inet_ntoa(((struct sockaddr_in*)&ss)->sin_addr), ntohs(((struct sockaddr_in*)&ss)->sin_port));
                // 设置接收fd为非阻塞，并在读/写该fd时判断 errno，如果是 EAGAIN 则本次read/write/send/recv不报错(体现在do_read/do_write函数中)
                make_nonblocking(fd);
                state[fd] = alloc_fd_state();
                assert(state[fd]);/*XXX*/
            }
        }

        for (i=0; i < FD_SETSIZE; ++i) {
            int r = 0;
            if (i == listener)
                continue;

            // 遍历判断accept后新建的描述符(除监听fd外) 是否在read set集中(即是否准备好读取)
            if (fdlist[i].revents & POLLIN) {
                r = do_read(i, state[i]); // 和do_write一样其中都有非阻塞情况errno的判断
            }
            // 读取成功后才判断是否准备好write，是则write
            if (r == 0 && fdlist[i].revents & POLLOUT) {
                r = do_write(i, state[i]);
                // printf("do write return:%d\n", r);
            }

            if (fdlist[i].revents & POLLRDHUP) {
                free_fd_state(state[i]);
                state[i] = NULL;
                // 清理pollfd中指定socket
                fdlist[i].fd = -1;
                fdlist[i].events = 0;
                close(i);
            }
        }
    }
    free(fdlist);
    fdlist = NULL;
}

int
main(int c, char **v)
{
    // 设置标准输出为无缓冲(_IONBF)
    setvbuf(stdout, NULL, _IONBF, 0);

    run();
    return 0;
}