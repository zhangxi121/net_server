//和设置课执行程序标题（名称）相关的放这里 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ngx_global.h"

// 设置可执行程序的标题相关函数,分配内存, 并且把环境变量拷贝到新的内存中来,
void ngx_init_setproctitle()
{
    // 这里无需判断 penvmen == NULL, 有些编译器new会返回NULL,有些会报异常, 但是不管怎样, 申请失败,
    gp_envmem = new char[g_envneedmem];
    memset(gp_envmem, 0, g_envneedmem);

    char *ptmp = gp_envmem;

    // 把原来的 enviro 内存搬到新的地方来,
    for (int i = 0; environ[i] != NULL; i++)
    {
        size_t size = strlen(environ[i]) + 1; // 注意 +1,
        strcpy(ptmp, environ[i]);             // 把原环境变量内容拷贝到新地方(新内存),
        environ[i] = ptmp;                    // 要让环境变量指向这段新内存,
        ptmp += size;
    }
    return;
}

// 设置可执行程序标题,
void ngx_setproctitle(const char *title)
{
    // 假设, 所有命令行参数都不需要用到了, 可以被随意覆盖了,
    // 注意: 标题长度, 不会长到原始标题和原始环境变量都装不下, 否则怕出问题, 不处理,

    // (1) 计算新标题长度,
    size_t ititlelen = strlen(title);

    // (2) 计算原来的总的 argv[] 那块内存的总长度(包括各种参数),
    size_t esy = g_argvneedmem + g_envneedmem;
    if (esy <= ititlelen)
    {
        // 如果标题太长了, argv[] 和 envirolen 总和都装不下, 就返回,
        // 注意 (ititlelen + 1) 才是新的标题, 字符串末尾有一个 '\0' 结束符, 所以是 "<=", 当 (esy == ititlelen) 也返回,
        return;
    }

    // (3) 设置后续的命令行参数为空, 表示只有一个 argv[] 中只有 argv[0] 这一个元素,
    //  这是好习惯, 防止后续 argv[]被滥用, 因为很多判断是用 "argv[] == NULL" 来做结束标记判断的,
    g_os_argv[1] = NULL;

    // (4) 把标题弄进来, 注意原来命令行参数都会被覆盖掉, 不要再使用这些命令行参数, 而且 g_os_argv[1] 已经被设置为NULL了,
    char *ptmp = g_os_argv[0];
    strcpy(ptmp, title);
    ptmp += ititlelen; // 跳过标题,  ptmp 指向标题的结尾的下一个位置, 即标题带的 '\0'这字符, 下面代码是从这个位置起把剩余空间清零,

    // (5) 把剩余的原 argv[] 以及 environ 所占的内存全部清0, 否则会出现在 ps 列可能还会残余一些没有被覆盖的内容,
    size_t cha = esy - ititlelen; // ititlelen是标题的长度,
                                  // ptmp是指向标题的结尾的下一个位置,即标题带的 '\0'这字符,
                                  // esy是总的长度, argv[] 和 environ[] 占的空间, 这里是为了把剩余的空间清零,
    memset(ptmp, 0, cha);
    return;
}

// 
