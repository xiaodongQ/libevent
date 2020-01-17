/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
/* For fcntl */
#include <fcntl.h>
/* for select */
#include <sys/select.h>

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
    int i, maxfd;
    fd_set readset, writeset, exset;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(40713);

    for (i = 0; i < FD_SETSIZE; ++i)
        state[i] = NULL;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    make_nonblocking(listener);

#ifndef WIN32
    {
        int one = 1;
        // SO_REUSEADDR用于对TCP套接字处于TIME_WAIT状态下的socket，允许重复绑定使用。
        // 一般server程序总是应该在调用bind()之前设置SO_REUSEADDR套接字选项。
        // [How do SO_REUSEADDR and SO_REUSEPORT differ?](https://stackoverflow.com/questions/14388706/how-do-so-reuseaddr-and-so-reuseport-differ)
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
#endif

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        return;
    }

    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&exset);

    // select返回后会把以前加入的但并无事件发生的fd清空，所以每次开始select前都要重新从array取得fd逐一加入（先将set FD_ZERO），扫描array的同时取得fd最大值maxfd，用于select的第一个参数
    while (1) {
        maxfd = listener;

        // 宏FD_ZERO清理文件描述符集fdset
        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        FD_ZERO(&exset);

        // 向文件描述符集fdset中新增文件描述符fd (listener描述符)；另外有FD_CLR将对应fd移除
        // 通常，操作系统通过宏 FD_SETSIZE 来声明在一个进程中select所能操作的文件描述符的最大数目。
            // 查看CentOS，#define __FD_SETSIZE	1024；而MinGW中 #define FD_SETSIZE      64
        // 参考: https://www.cnblogs.com/skyofbitbit/p/3654466.html
        FD_SET(listener, &readset);  // 创建的监听fd每次手动添加到read set中
        for (i=0; i < FD_SETSIZE; ++i) {
            // 通过是否为NULL判断是否有该描述符
            if (state[i]) {
                if (i > maxfd)
                    maxfd = i;
                // 在readset集中添加感兴趣的描述符
                FD_SET(i, &readset);
                // 读取结束后才将句柄加到写set中进行检查
                if (state[i]->writing) {
                    FD_SET(i, &writeset);
                }
            }
        }

        // 注册 读、写、异常 描述符集
        // (参考man手册 man 3 select)select会检查第一个参数nfds指定的范围 [0,nfds)，只检查到nfds-1，所以此处指定max+1
        // 若调用成功，select()会返回准备好的fd总数(在三个set中的描述符总数)；select将更新这个集合,把其中不可读的套节字去掉
        // 如果没有准备好的fd，则阻塞到至少有一个fd(读/写/异常都可)准备好，直到设置的超时时间(最后参数设置，若为NULL则不超时)或收到打断的信号
        // 若最后参数:struct timeval *restrict timeout 非NULL，则按指定的超时时间处理，最大31天，超出也按31天
            // restrict C99引入的关键字，告诉编译器对象已经被指针所引用，不能通过除该指针外所有其他直接或间接的方式修改该对象的内容
        // 调用成功结束返回数量，否则返回-1并设置errno
        if (select(maxfd+1, &readset, &writeset, &exset, NULL) < 0) {
            perror("select");
            return;
        }

        // FD_ISSET来检测(返回true) 文件描述符集fdset(此处为readset) 中是否存在文件描述符fd(对应描述符listener)
        // 主线程创建的监听fd也用select来检测
        if (FD_ISSET(listener, &readset)) {
            struct sockaddr_storage ss;
            socklen_t slen = sizeof(ss);
            int fd = accept(listener, (struct sockaddr*)&ss, &slen);
            if (fd < 0) {
                perror("accept");
            } else if (fd > FD_SETSIZE) { // 按1024算，接收的fd不能大于1024?
                close(fd);
            } else {
                printf("accept request[ip:%s, port:%d]...\n", inet_ntoa(((struct sockaddr_in*)&ss)->sin_addr), ntohs(((struct sockaddr_in*)&ss)->sin_port));
                // 设置接收fd为非阻塞，并在读/写该fd时判断 errno，如果是 EAGAIN 则本次read/write/send/recv不报错(体现在do_read/do_write函数中)
                // man FD_SET，BUGS章节。为了防止使用self-pipe管道时导致可能的阻塞，当向一个满的pipe管道写或者从一个空的pipe管道读，设置为非阻塞IO
                make_nonblocking(fd);
                state[fd] = alloc_fd_state();
                assert(state[fd]);/*XXX*/
            }
        }

        for (i=0; i < maxfd+1; ++i) {
            int r = 0;
            if (i == listener)
                continue;

            // 遍历判断accept后新建的描述符(除监听fd外) 是否在read set集中(即是否准备好读取)
            if (FD_ISSET(i, &readset)) {
                r = do_read(i, state[i]); // 和do_write一样其中都有非阻塞情况errno的判断
            }
            // 读取成功后才判断是否准备好write，是则write
            if (r == 0 && FD_ISSET(i, &writeset)) {
                r = do_write(i, state[i]);
            }
            // 返回值0才表示成功
            if (r) {
                free_fd_state(state[i]);
                state[i] = NULL;
                close(i);
            }
        }
    }
}

int
main(int c, char **v)
{
    // 设置标准输出为无缓冲(_IONBF)
    setvbuf(stdout, NULL, _IONBF, 0);

    run();
    return 0;
}