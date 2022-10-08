
//和网络 中 接受连接【accept】 有关的函数放这里

#include <errno.h>  //errno
#include <fcntl.h>  //open
#include <stdarg.h> //va_start....
#include <stdint.h> //uintptr_t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h> //gettimeofday
#include <time.h>     //localtime_r
#include <unistd.h>   //STDERR_FILENO等
// #include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h> //ioctl

#include "ngx_c_conf.h"
#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_macro.h"

// 建立新连接专用函数，当新连接进入时，本函数会被 ngx_epoll_process_events() 所调用,
void CSocekt::ngx_event_accept(lpngx_connection_t oldc)
{

    //*********** 惊群 ***********
    //
    //=== 官方 nginx 解决惊群问题, 一把进程锁, 谁拿到这把锁, 然后才 epoll_ctl(epfd, EPOLL_CTL_ADD, EPOLLIN | EPOLLRDHUP, &ev),
    //=== 没有拿到这把进程锁的, epoll_ctl(epfd, EPOLL_CTL_ADD, 0 | EPOLLRDHUP, &ev),
    //=== linux 3.9 版本以上的内核, 已经在内核中解决了惊群问题, 而且性能比官方 nginx 效率高很多,
    //=== 通过复用端口,  reuseport(复用端口), 套接字的复用机制, 允许将多个 socket bind 到同一个 ip地址/port,
    //=== setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, ...) 是将多个套接字, 咱们这里是 fork() 出来, 多个子进程是同一个套接字,
    //=== 注意 咱们这里是 fork() 出来, 多个子进程是同一个套接字, 所以不适合咱们这种情况,
    //===
    //
    // 使用 pthread_mutex_t 解决惊群失败, 必须用进程间的锁, pthread_mutex_trylock() 只适用于本进程的锁, 不适合进程间,
    //

#if defined(USE_SHMGET_LOCK)
    int shmid = shmget(THUNDERING_HERD_KEY, 0, 0);
    if (-1 == shmid)
    {
        ngx_log_stderr(0, "shmget(THUNDERING_HERD_KEY, 0, 0), 返回的错误码为%d!", errno);
        return false;
    }
    void *shmptr = shmat(shmid, NULL, 0);
    pthread_mutex_t *lp_thundering_herd_mtx = (pthread_mutex_t *)shmptr;

#elif defined(USE_MMAP_LOCK)
    pthread_mutex_t *lp_thundering_herd_mtx = g_mptr;
#endif
    // 函数 pthread_mutex_trylock() 是 pthread_mutex_lock() 的非阻塞版本。
    // 如果mutex参数所指定的互斥锁已经被锁定的话, 调用pthread_mutex_trylock函数不会阻塞当前线程, 而是立即返回一个值来描述互斥锁的状况。
    // pthread_mutex_trylock() 尝试加锁，如果该互斥锁已经锁定，则返回一个不为0的错误值，如果该互斥锁没有锁定，则返回0，表示尝试加锁成功
    int lock_status = pthread_mutex_trylock(lp_thundering_herd_mtx);

    if (lock_status == EOWNERDEAD)
    {
        pthread_mutex_unlock(lp_thundering_herd_mtx);
        ngx_log_stderr(0, "P1 mutex status: EOWNERDEAD, 进程id=%d", getpid());
        return;
    }
    else if (lock_status == ENOTRECOVERABLE)
    {
        ngx_log_stderr(0, "P1 mutex status: ENOTRECOVERABLE, 进程id=%d", getpid());
        return;
    }
    else if (lock_status == EBUSY)
    {
        ngx_log_stderr(0, "P1 mutex status: EBUSY, 进程id=%d", getpid());
        return;
    }
    else if (0 == lock_status)
    {
        // 加锁成功, 执行 accept4(listenfd,),
        ngx_log_stderr(0, "pthread_mutex_trylock() 加锁成功, 进程id=%d, 准备 accept(), 解决惊群!", ngx_pid);
        //         
        // pthread_mutex_trylock() 加锁成功后也需要释放锁,
        // 测试发现 pthread_mutex_trylock() 无法解决惊群,
        // pthread_mutex_unlock(lp_thundering_herd_mtx);
    }
    else
    {
        ngx_log_stderr(0, "lock_status = %d, 进程id=%d", lock_status, ngx_pid);
        return;
    }

    // 因为listen套接字上用的不是ET【边缘触发】，而是LT【水平触发】，意味着客户端连入如果我要不处理，这个函数会被多次调用，所以，我这里这里可以不必多次accept()，可以只执行一次accept()
    // 这也可以避免本函数被卡太久，注意，本函数应该尽快返回，以免阻塞程序运行；
    struct sockaddr mysockaddr; //远端服务器的socket地址
    socklen_t socklen;
    int err;
    int level;
    int s;
    static int use_accept4 = 1; //先认为能够使用accept4()函数
    lpngx_connection_t newc;    //代表连接池中的一个连接【注意这是指针】

    // ngx_log_stderr(0,"这是几个\n"); 这里会惊群，也就是说，epoll技术本身有惊群的问题

    socklen = sizeof(mysockaddr);
    do
    {
        if (use_accept4)
        {
            // 因为listen套接字是非阻塞的，所以即便已完成连接队列为空，accept4()也不会卡在这里；
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK); //从内核获取一个用户端连接，最后一个参数SOCK_NONBLOCK表示返回一个非阻塞的socket，节省一次ioctl【设置为非阻塞】调用
        }
        else
        {
            // 因为listen套接字是非阻塞的，所以即便已完成连接队列为空，accept()也不会卡在这里；
            s = accept(oldc->fd, &mysockaddr, &socklen);
        }

        // 惊群，有时候不一定完全惊动所有4个worker进程，可能只惊动其中2个等等，其中一个成功其余的accept4()都会返回-1；错误 (11: Resource temporarily unavailable【资源暂时不可用】)
        // 所以参考资料：https://blog.csdn.net/russell_tao/article/details/7204260
        // 其实，在linux2.6内核上，accept系统调用已经不存在惊群了（至少我在2.6.18内核版本上已经不存在）。大家可以写个简单的程序试下，在父进程中bind,listen，然后fork出子进程，
        // 所有的子进程都accept这个监听句柄。这样，当新连接过来时，大家会发现，仅有一个子进程返回新建的连接，其他子进程继续休眠在accept调用上，没有被唤醒。
        // ngx_log_stderr(0,"测试惊群问题，看惊动几个worker进程%d\n",s); 【我的结论是：accept4可以认为基本解决惊群问题，但似乎并没有完全解决，有时候还会惊动其他的worker进程】

        if (s == -1)
        {
            err = errno;

            // 对accept、send和recv而言，事件未发生时 errno 通常被设置成 EAGAIN (意为“再来一次”) 或者 EWOULDBLOCK (意为“期待阻塞”)
            if (err == EAGAIN || err == EWOULDBLOCK) // accept()没准备好，这个EAGAIN错误EWOULDBLOCK是一样的
            {                                        // 除非你用一个循环不断的 accept() 取走所有的连接，不然一般不会有这个错误【这里只取一个连接】,
                // EAGAIN "https://blog.csdn.net/boshuzhang/article/details/50608494", (1)发送时 send 函数中的size变量大小超过了tcp_sendspace的值。(2)
                pthread_mutex_unlock(lp_thundering_herd_mtx);
                return;
            }
            level = NGX_LOG_ALERT;
            if (err == ECONNABORTED) // ECONNRESET 错误则发生在对方意外关闭套接字后【您的主机中的软件放弃了一个已建立的连接--由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
            {
                // 该错误被描述为“software caused connection abort”，即“软件引起的连接中止”。原因在于当服务和客户进程在完成用于 TCP 连接的“三次握手”后，
                // 客户 TCP 却发送了一个 RST （复位）分节，在服务进程看来，就在该连接已由 TCP 排队，等着服务进程调用 accept 的时候 RST 却到达了。
                // POSIX 规定此时的 errno 值必须 ECONNABORTED。源自 Berkeley 的实现完全在内核中处理中止的连接，服务进程将永远不知道该中止的发生。
                // 服务器进程一般可以忽略该错误，直接再次调用accept。
                level = NGX_LOG_ERR;
            }
            else if (err == EMFILE || err == ENFILE) // EMFILE:进程的 fd 已用尽【已达到系统所允许单一进程所能打开的文件/套接字总数】。可参考：https://blog.csdn.net/sdn_prc/article/details/28661661   以及 https://bbs.csdn.net/topics/390592927
                                                     // ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
            // ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有process-specific的resource limits。按照常识，process-specific的resource limits，一定受限于system-wide的resource limits。
            {
                level = NGX_LOG_CRIT;
            }
            ngx_log_error_core(level, errno, "CSocekt::ngx_event_accept()中accept4()失败!"); // 这行一定要关闭, 频繁输出这行日志导致进程崩溃,

            if (use_accept4 && err == ENOSYS) // accept4()函数没实现，坑爹？
            {
                use_accept4 = 0; //标记不使用accept4()函数，改用accept()函数
                continue;        //回去重新用accept()函数搞
            }

            if (err == ECONNABORTED) //对方关闭套接字,
            {
                // 这个错误因为可以忽略，所以不用干啥,
                // do nothing,
            }

            if (err == EMFILE || err == ENFILE)
            {
                // do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
                // 我这里目前先不处理吧【因为上边已经写这个日志了】；
            }
            pthread_mutex_unlock(lp_thundering_herd_mtx);
            return;
        }

        // 走到这里的，表示accept4()成功了,
        if (m_onlineUserCount >= m_worker_connections) //用户连接数过多，要关闭该用户socket，因为现在也没分配连接，所以直接关闭即可
        {
            ngx_log_stderr(0, "超出系统允许的最大连入用户数(最大允许连入数%d)，关闭连入请求(%d)。", m_worker_connections, s);
            close(s);
            pthread_mutex_unlock(lp_thundering_herd_mtx);
            return;
        }

        //如果某些恶意用户连上来发了1条数据就断，不断连接，会导致频繁调用ngx_get_connection()使用我们短时间内产生大量连接，危及本服务器安全
        if (m_connectionList.size() > (m_worker_connections * 5))
        {
            //比如你允许同时最大2048个连接，但连接池却有了 2048*5这么大的容量，这肯定是表示短时间内 产生大量连接/断开，因为我们的延迟回收机制，这里连接还在垃圾池里没有被回收
            if (m_freeconnectionList.size() < m_worker_connections)
            {
                // 整个连接池这么大了，而空闲连接却这么少了，所以我认为是  短时间内 产生大量连接，发一个包后就断开，我们不可能让这种情况持续发生，所以必须断开新入用户的连接
                // 一直到m_freeconnectionList变得足够大【连接池中连接被回收的足够多】,
                close(s);
                pthread_mutex_unlock(lp_thundering_herd_mtx);
                return;
            }
        }

        // ngx_log_stderr(errno,"accept4成功s=%d",s); //s这里就是 一个句柄了
        newc = ngx_get_connection(s);
        if (newc == NULL)
        {
            //连接池中连接不够用，那么就得把这个socekt直接关闭并返回了，因为在ngx_get_connection()中已经写日志了，所以这里不需要写日志了
            if (close(s) == -1)
            {
                ngx_log_error_core(NGX_LOG_ALERT, errno, "CSocekt::ngx_event_accept()中close(%d)失败!", s);
            }
            pthread_mutex_unlock(lp_thundering_herd_mtx);
            return;
        }
        //...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理,

        // 成功的拿到了连接池中的一个连接,
        memcpy(&newc->s_sockaddr, &mysockaddr, socklen); // 拷贝客户端地址到连接对象【要转成字符串ip地址参考函数ngx_sock_ntop()】
        // {
        //    // 测试将收到的地址弄成字符串，格式形如"192.168.1.126:40904"或者"192.168.1.126"
        //    u_char ipaddr[100]; memset(ipaddr,0,sizeof(ipaddr));
        //    ngx_sock_ntop(&newc->s_sockaddr,1,ipaddr,sizeof(ipaddr)-10); //宽度给小点
        //    ngx_log_stderr(0,"ip信息为%s\n",ipaddr);
        // }

        if (!use_accept4)
        {
            //如果不是用accept4()取得的socket，那么就要设置为非阻塞【因为用accept4()的已经被accept4()设置为非阻塞了】
            if (setnonblocking(s) == false)
            {
                // 设置非阻塞居然失败,
                ngx_close_connection(newc); //回收连接池中的连接（千万不能忘记），并关闭socket,
                pthread_mutex_unlock(lp_thundering_herd_mtx);
                return;                     // 直接返回,
            }
        }

        newc->listening = oldc->listening; // 连接对象 和监听对象关联，方便通过连接对象找监听对象【关联到监听端口】
        // newc->w_ready = 1;                 // 标记可以写，新连接写事件肯定是ready的；【从连接池拿出一个连接时这个连接的所有成员都是0】

        newc->rhandler = &CSocekt::ngx_read_request_handler;  // 设置数据来时的读处理函数，其实官方nginx中是ngx_http_wait_request_handler()
        newc->whandler = &CSocekt::ngx_write_request_handler; // 设置数据发送时的写处理函数。

        // 客户端应该主动发送第一次的数据，这里将读事件加入epoll监控，这样当客户端发送数据来时，会触发 ngx_wait_request_handler() 被 ngx_epoll_process_events() 调用,
        if (ngx_epoll_oper_event(
                s,                    // socekt句柄
                EPOLL_CTL_ADD,        // 事件类型，这里是增加
                EPOLLIN | EPOLLRDHUP, // 标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭
                0,                    // 对于事件类型为增加的，不需要这个参数
                newc                  // 连接池中的连接
                ) == -1)
        {
            // 增加事件失败，失败日志在ngx_epoll_add_event中写过了，因此这里不多写啥；
            ngx_close_connection(newc);
            pthread_mutex_unlock(lp_thundering_herd_mtx);
            return; //直接返回
        }
        /*
        else
        {
           //打印下发送缓冲区大小
           int           n;
           socklen_t     len;
           len = sizeof(int);
           getsockopt(s,SOL_SOCKET,SO_SNDBUF, &n, &len);
           ngx_log_stderr(0,"发送缓冲区的大小为%d!",n); //87040

           n = 0;
           getsockopt(s,SOL_SOCKET,SO_RCVBUF, &n, &len);
           ngx_log_stderr(0,"接收缓冲区的大小为%d!",n); //374400

           int sendbuf = 2048;
           if (setsockopt(s, SOL_SOCKET, SO_SNDBUF,(const void *) &sendbuf,n) == 0)
           {
               ngx_log_stderr(0,"发送缓冲区大小成功设置为%d!",sendbuf);
           }

            getsockopt(s,SOL_SOCKET,SO_SNDBUF, &n, &len);
           ngx_log_stderr(0,"发送缓冲区的大小为%d!",n); //87040
        }
        */

        pthread_mutex_unlock(lp_thundering_herd_mtx);
        
        if (m_ifkickTimeCount == 1)
        {
            AddToTimerQueue(newc);
        }
        ++m_onlineUserCount; // 连入用户数量+1,
        break;               // 一般就是循环一次就跳出,
    } while (1);

    return;
}
