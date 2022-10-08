#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// 自定义头文件放下边,
#include "ngx_c_conf.h" // 和配置文件相关处理的类, 名字带 c_ 表示和类相关,
#include "ngx_func.h"   //

// 静态成员赋值,
CConfig *CConfig::m_instance = nullptr;

// 构造函数,
CConfig::CConfig()
{
}

// 析构函数,
CConfig::~CConfig()
{
    std::vector<LPCConfItem>::iterator pos;
    for (pos = m_configItemList.begin(); pos != m_configItemList.end(); ++pos)
    {
        delete (*pos);
    }
    m_configItemList.clear();
}

// 装配配置文件,
bool CConfig::Load(const char *pconfName)
{
    FILE *fp;
    fp = fopen(pconfName, "r");
    if (fp == NULL)
        return false;

    // 每一行配置文件读出来都放这里,
    char linebuf[501] = {0}; // 每行配置不要太长, 保持 <500 字符以内, 防止出现问题,

    while (!feof(fp))
    {
        // 大家注意写法的严密性, 商业代码, 就是首先要确保代码的严密性,
        if (fgets(linebuf, 500, fp) == NULL)
            continue;

        // 读取到空行,
        if (linebuf[0] == 0)
            continue;

        // 处理注释行,
        if (*linebuf == ';' || *linebuf == ' ' || *linebuf == '#' || *linebuf == '\t' || *linebuf == '\n')
            continue;

    lblprocstring:
        if (strlen(linebuf) > 0)
        {
            // 去除掉末尾 linebuf[strlen(linebuf) - 1] 是 换行、回车、空格 的字符, 
            // 这样 "ListenPort = 5678 \t" 这样的就会变成  "ListenPort = 5678", 末尾就不会有 \t 和 空格等存在了,
            if (linebuf[strlen(linebuf) - 1] == 10 || linebuf[strlen(linebuf) - 1] == 13 || linebuf[strlen(linebuf) - 1] == 32)
            {
                linebuf[strlen(linebuf) - 1] = 0;
                goto lblprocstring;
            }
        }
        if (linebuf[0] == 0) // 除去空行的,
            continue;
        if (*linebuf == '[' && linebuf[strlen(linebuf) - 1] == ']') // [Socket] 这样的组信息, 也等价于注释行,
            continue;

        // 上面过滤了末尾最后一个元素是 换行、回车、空格, 剩下的有效信息就是 "ListenPort = 5678",
        // 现在将 "ListenPort = 5678" 过滤出来里面的 键 和 值,
        char *ptmp = strchr(linebuf, '=');
        if (nullptr != ptmp)
        {
            LPCConfItem p_confitem = new CConfItem;
            memset(p_confitem, 0, sizeof(CConfItem));
            strncpy(p_confitem->ItemName, linebuf, (int)(ptmp - linebuf)); // ptmp是等号, 把等号左边的拷贝到 p_confitem->ItemName,
            strcpy(p_confitem->ItemContent, ptmp + 1);                     // ptmp是等号, 把等号右边的拷贝到 p_confitem->ItemContent,

            // key = "ListenPort ", value = " 5678", 现在过滤掉里面的首尾空格, 如下 Ltrim() Rtrim() 就是, 得到 key = "ListenPort", value = "5678",
            Rtrim(p_confitem->ItemName);
            Ltrim(p_confitem->ItemName);
            Rtrim(p_confitem->ItemContent);
            Ltrim(p_confitem->ItemContent);

            m_configItemList.emplace_back(p_confitem);
        }
    } // while (!feof(fp))

    fclose(fp);
    return true;
}

// 根据 ItemName 获取配置信息字符串, 只读、不修改不用互斥,
const char *CConfig::GetString(const char *p_itemname)
{
    std::vector<LPCConfItem>::iterator pos;
    for (pos = m_configItemList.begin(); pos != m_configItemList.end(); ++pos)
    {
        if (strcasecmp((*pos)->ItemName, p_itemname) == 0)
            return (*pos)->ItemContent;
    }
    return nullptr;
}

// 根据 ItemName 获取数字类型的配置信息, 只读、不修改不用互斥,
int CConfig::GetIntDefault(const char *p_itemname, const int def)
{
    std::vector<LPCConfItem>::iterator pos;
    for (pos = m_configItemList.begin(); pos != m_configItemList.end(); ++pos)
    {
        if (strcasecmp((*pos)->ItemName, p_itemname) == 0)
            return atoi((*pos)->ItemContent);
    }
    return def;
}
