//和开启子进程相关


#include <errno.h>  //errno
#include <signal.h> //信号相关头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"

// 创建共享内存, 在 shmptr 上放一个互斥锁变量, 这样处理惊群,
static void shm_lock_create()
{
    int shmid = shmget(THUNDERING_HERD_KEY, PTHREAD_MUTEX_T_SIZE, IPC_CREAT | 0664);
    if (-1 == shmid)
    {
        ngx_log_stderr(0, "shmget(100, PTHREAD_MUTEX_T_SIZE, IPC_CREAT | 0664), 返回的错误码为%d!", errno);
        return;
    }
    void *shmptr = shmat(shmid, NULL, 0);
    pthread_mutex_t *lp_thundering_herd_mtx = (pthread_mutex_t *)shmptr;
    new (lp_thundering_herd_mtx) pthread_mutex_t();
    static pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(lp_thundering_herd_mtx, &mattr);
    // pthread_mutex_init(lp_thundering_herd_mtx, NULL);
}

// 参考 "https://www.likecs.com/ask-359860.html"  "https://www.cnblogs.com/fortunely/p/14923945.html" 创建 PTHREAD_PROCESS_SHARED 进程锁,
pthread_mutex_t *g_mptr; // 互斥锁变量指针, 互斥锁变量存放到共享内存,
static void mmap_lock_create()
{
    int mutex_fdesc;
    pthread_mutexattr_t mattr;

    // mutex_fdesc = shm_open("/mutex", O_RDWR, S_IRWXU | S_IRWXG);

    mutex_fdesc = open("/dev/zero", O_RDWR, 0);
    
    if (mutex_fdesc < 0)
        ngx_log_stderr(0, "open(), 返回的错误码为%d!", errno);
    g_mptr = (pthread_mutex_t *)mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED, mutex_fdesc, 0);
    if (g_mptr == MAP_FAILED)
        ngx_log_stderr(0, "mmap(pthread_mutex_t), 返回的错误码为%d!", errno);
    if (close(mutex_fdesc))
        ngx_log_stderr(0, "close(fd), 返回的错误码为%d!", errno);

    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    // pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(g_mptr, &mattr);
}

// 使用 pthread_mutex_trylock() 不能完成进程间的锁, 解决不了惊群, 因为是非阻塞斑斑, 
// 使用 pthread_mutex_lock() 能解决进程间的锁, 解决惊群问题,

///////////////////////////////////////////////

//函数声明
static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums, const char *pprocname);
static void ngx_worker_process_cycle(int inum, const char *pprocname);
static void ngx_worker_process_init(int inum);

//变量声明
static u_char master_process[] = "master process";

//描述：创建worker子进程
void ngx_master_process_cycle()
{
    sigset_t set; // 信号集,

    sigemptyset(&set); // 清空信号集,

    //下列这些信号在执行本函数期间不希望收到【考虑到官方nginx中有这些信号，】（保护不希望由信号中断的代码临界区）
    //建议fork()子进程时学习这种写法，防止信号的干扰；
    sigaddset(&set, SIGCHLD);  // 子进程状态改变
    sigaddset(&set, SIGALRM);  // 定时器超时
    sigaddset(&set, SIGIO);    // 异步I/O
    sigaddset(&set, SIGINT);   // 终端中断符
    sigaddset(&set, SIGHUP);   // 连接断开
    sigaddset(&set, SIGUSR1);  // 用户定义信号
    sigaddset(&set, SIGUSR2);  // 用户定义信号
    sigaddset(&set, SIGWINCH); // 终端窗口大小改变
    sigaddset(&set, SIGTERM);  // 终止
    sigaddset(&set, SIGQUIT);  // 终端退出符
    //.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......

    //设置，此时无法接受的信号；阻塞期间，你发过来的上述信号，多个会被合并为一个，暂存着，等你放开信号屏蔽后才能收到这些信号。。。
    // sigprocmask()在第三章第五节详细讲解过
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) //第一个参数用了SIG_BLOCK表明设置 进程 新的信号屏蔽字 为 “当前信号屏蔽字 和 第二个参数指向的信号集的并集
    {
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_master_process_cycle()中sigprocmask()失败!");
    }
    //即便sigprocmask失败，程序流程 也继续往下走

    //首先我设置主进程标题---------begin
    size_t size;
    int i;
    size = sizeof(master_process); // 注意我这里用的是sizeof，所以字符串末尾的\0是被计算进来了的,
    size += g_argvneedmem;         // argv参数长度加进来,
    if (size < 1000)               // 长度小于这个，我才设置标题,
    {
        char title[1000] = {0};
        strcpy(title, (const char *)master_process); // "master process"
        strcat(title, " ");                          // 跟一个空格分开一些，清晰    //"master process "
        for (i = 0; i < g_os_argc; i++)              // 标题变成 "master process ./nginx"
        {
            strcat(title, g_os_argv[i]);
        }
        ngx_setproctitle(title);                                                              //设置标题,
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "%s %P 启动并开始运行......!", title, ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    }
    // 首先我设置主进程标题---------end,

    // 在创建worker进程之前创建共享内存, 进程间通讯用, 用来共同使用一把锁, 防止惊群,
    shm_lock_create();
    mmap_lock_create();

    // 从配置文件中读取要创建的worker进程数量,
    CConfig *p_config = CConfig::GetInstance();                      // 单例类,
    int workprocess = p_config->GetIntDefault("WorkerProcesses", 1); // 从配置文件中得到要创建的worker进程数量,
    g_workprocess = workprocess;
    ngx_start_worker_processes(workprocess); // 这里要创建worker子进程,

    // === 创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来
    sigemptyset(&set); // 信号屏蔽字清空, 但是并没有调用 sigprocmask() 去设置, 所以这里仅仅是把变量清空,
                       // 这里是主进程 sigemptyset() 清空, 子进程并没有进来,
    for (;;)
    {
        // master进程会一直在 for() 这里死循环,

        // sigsuspend() 函数的作用, 是这样保证原子性的:
        // (a). 根据给定的参数 set 设置新的 mask, 并阻塞当前进程【因为是个空集，所以不阻塞任何信号】
        // (b). 此时，一旦收到信号，便恢复原先的信号屏蔽 (保证原子性的原理),
        //        【原来的mask在上边设置的，阻塞了多达10个信号，从而保证我下边的执行流程不会再次被其他信号截断】
        // (c). 调用该信号对应的信号处理函数,
        // (d). 信号处理函数返回后，sigsuspend 才会返回，解除阻塞, 使程序流程继续往下走,
        //

        // printf("for进来了！\n"); //发现，如果print不加\n，无法及时显示到屏幕上，是行缓存问题，以往没注意；可参考https://blog.csdn.net/qq_26093511/article/details/53255970

        sigsuspend(&set); // 阻塞在这里，等待一个信号，此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒 (返回),
                          // 前面虽然已经 sigprocmask() 屏蔽了所有信号, 但是这个函数还是能收到 -SIGUSR1  -SIGUSR1 等所有信号,
                          // "kill -SIGUSR1 7301" 、 "kill -SIGQUIT 7301" 都是能收到的,
                          // sigprocmask() 是非原子性操作, 当正在执行 sigprocmask() 突然来了一个信号, 那么这个信号可能会被丢弃,
                          // sigsuspend() 是原子操作, 不会丢信号, 在某些场合来取代 sigprocmask(),
                          // 此时master进程完全靠信号驱动干活,

        // 当收到信号后, 唤醒了以后, 解除阻塞、执行下面代码,
        printf("执行到sigsuspend()下边来了, sigsuspend() 收到信号被唤醒了! \n"); // printf() 对应的是 STDERR_FILENO, 如果开启了守护进程的话, STDIN_FILENO STDOUT_FILENO 输出到 "/dev/null", 就不会打印了,
                                                                                 // 如果开启了守护进程的话, 需要使用 STDERR_FILENO 输出到终端,

        printf("master进程休息1秒\n");
        sleep(1);
        // 以后扩充.......
    }
    return;
}

// 描述：根据给定的参数创建指定数量的子进程，因为以后可能要扩展功能，增加参数，所以单独写成一个函数
// threadnums:要创建的子进程数量
static void ngx_start_worker_processes(int threadnums)
{
    int i;
    for (i = 0; i < threadnums; i++) // master进程在走这个循环，来创建若干个子进程
    {
        ngx_spawn_process(i, "worker process");
    }
    return;
}

// 描述: 产生一个子进程
// inum: 进程编号【0开始】
// pprocname:子进程名字"worker process"
static int ngx_spawn_process(int inum, const char *pprocname)
{
    pid_t pid;

    pid = fork(); // fork()系统调用产生子进程
    switch (pid)  // pid判断父子进程，分支处理
    {
    case -1: // 产生子进程失败
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_spawn_process() fork() 产生子进程num=%d,procname=\"%s\"失败!", inum, pprocname);
        return -1;

    case 0:                                        // 子进程分支,
        ngx_parent = ngx_pid;                      // 因为是子进程了，所有原来的pid变成了父pid,
        ngx_pid = getpid();                        // 重新获取pid,即本子进程的pid,
        ngx_worker_process_cycle(inum, pprocname); // 我希望所有worker子进程，在这个函数里不断循环着不出来，也就是说，子进程流程不往下边走;
        break;

    default: //这个应该是父进程分支，直接break;，流程往switch之后走
        break;
    }

    //父进程分支会走到这里，子进程流程不往下边走-------------------------
    //若有需要，以后再扩展增加其他代码......
    return pid;
}

// 描述：worker子进程的功能函数，每个woker子进程，就在这里循环着了（无限循环【处理网络事件和定时器事件以对外提供web服务】）
//     子进程分叉才会走到这里
// inum：进程编号【0开始】
static void ngx_worker_process_cycle(int inum, const char *pprocname)
{
    //设置一下变量
    ngx_process = NGX_PROCESS_WORKER; // 设置进程的类型，是worker进程,

    //重新为子进程设置进程名，不要与父进程重复------
    ngx_worker_process_init(inum); // 里面处理了 epoll_ctl() 增加 listenfd, epoll_ctl() 并不会阻塞,
    ngx_setproctitle(pprocname);   // 设置标题,
    kill(getppid(), SIGUSR1);
    ngx_log_error_core(NGX_LOG_NOTICE, 0, "%s %P 启动并开始运行......!", pprocname, ngx_pid); // 设置标题时顺便记录下来进程名，进程id等信息到日志

    // 暂时先放个死循环，在这个循环里一直不出来
    // setvbuf(stdout,NULL,_IONBF,0); // 这个函数. 直接将 printf() 缓冲区禁止， printf就直接输出了, 不走行缓存,
    for (;;)
    {
        // 先sleep一下 以后扩充.......

        // usleep(1000000);
        // printf("1212");  // 如果开启了守护进程的话, 需要使用 STDERR_FILENO 输出到终端,
        // if(inum == 1)
        // {
        //     ngx_log_stderr(0,"good--这是子进程，编号为%d,pid为%P",inum,ngx_pid);
        //     printf("good--这是子进程，编号为%d,pid为%d\r\n",inum,ngx_pid);
        //     ngx_log_error_core(0,0,"good--这是子进程，编号为%d",inum,ngx_pid);
        //     printf("我的测试哈inum=%d",inum++);
        //     fflush(stdout);
        // }

        ngx_process_events_and_timers(); // 处理了 epoll_wait(), 这里设置的 timeout 是 -1, 一直阻塞直到有事件到来,
                                         // 处理网络事件和定时器事件,

        // if (false) //优雅的退出
        // {
        //     g_stopEvent = 1;
        //     break;
        // }

        //
    }

    //如果从这个循环跳出来，
    g_threadpool.StopAll();      // 考虑在这里停止线程池；
    g_socket.Shutdown_subproc(); // socket需要释放的东西考虑释放；
    return;
}

//描述：子进程创建时调用本函数进行一些初始化工作
static void ngx_worker_process_init(int inum)
{
    sigset_t set; //信号集

    sigemptyset(&set);                              // 清空信号集
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) // 原来是屏蔽那10个信号【防止fork()期间收到信号导致混乱】，现在不再屏蔽任何信号【接收任何信号】
    {
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_worker_process_init()中sigprocmask()失败!");
    }

    //线程池代码，率先创建，至少要比和socket相关的内容优先
    CConfig *p_config = CConfig::GetInstance();
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount", 5); // 处理接收到的消息的线程池中线程数量
    if (g_threadpool.Create(tmpthreadnums) == false)                              // 线程池的初始化一定要在 epoll 初始化之前, 因为一旦 epoll 初始化之后, 就要来事件了,
    {
        // 内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    sleep(1); //再休息1秒；

    if (g_socket.Initialize_subproc() == false) // 初始化子进程需要具备的一些多线程能力相关的信息,
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }

    // 如下这些代码参照官方nginx里的ngx_event_process_init()函数中的代码
    g_socket.ngx_epoll_init(); // 里面处理了 epoll_ctl() 增加 listenfd, epoll_ctl() 并不会阻塞,
                               // 初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责

    // g_socket.ngx_epoll_listenportstart();//往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走】

    //....将来再扩充代码
    //....
    return;
}