
# .PHONY:all clean

ifeq ($(DEBUG),true)
# -g 生成调试信息, GNU调试器可以利用该信息,
CC = g++ -std=c++11 -g
VERSION = debug 
else
CC = g++ -std=c++11
VERSION = release
endif

# CC = gcc

# wildcard 是一个函数, 函数是扫描当前目录下的所有 .cxx 文件,
# SRCS = nginx.c  ngx_conf.c 
SRCS = $(wildcard *.cxx)

# OBJS = nginx.o  ngx_conf.o 这么一个一个增加 .o 太麻烦, 下行换一种写法, 把字符串中的 .c 替换为 .o,
OBJS = $(SRCS:.cxx=.o)

# 把字符串中的 .c 替换为 .d, 
# DEPS = nginx.d  ngx_conf.d
DEPS = $(SRCS:.cxx=.d)

# 可以指定BIN文件的位置
# BIN = /home/zhangxi/MyWorkspace/LinuxArchitecture/cpp_ngx_code_dir/nginx
BIN := $(addprefix $(BUILD_ROOT)/,$(BIN))

# 定义存放 obj 文件的目录, 目录统一到一个位置才方便后续链接, 不然整到各个子目录去, 不好连接,
# 注意下边这个字符串,某位不要有空格等,否则语法错误,
LINK_OBJ_DIR = $(BUILD_ROOT)/app/link_obj
DEP_DIR = $(BUILD_ROOT)/app/dep

# -p是递归创建目录,
$(shell mkdir -p $(LINK_OBJ_DIR))
$(shell mkdir -p $(DEP_DIR))

# 我要把目标文件生成到上述文件目录中去, 利用函数 addprefix 增加个前缀,
# 处理后形如 "/mnt/hgfs/linux/nginx/app/link_obj/nginx_signal2.o"   "/mnt/hgfs/linux/nginx/app/link_obj/nginx_signal.o"
# := 在解析阶段直接复制常量字符串[立即展开], 而 = 在运行节点, 实际使用变量时再展开进行求值[延迟展开], 
# 原来是  OBJS = nginx.o  ngx_conf.o , 现在是  OBJS = /home/zhangxi/MyWorkspace/LinuxArchitecture/cpp_ngx_code_dir/nginx/nginx.o  /home/zhangxi/MyWorkspace/LinuxArchitecture/cpp_ngx_code_dir/nginx/ngx_conf.o ,
OBJS := $(addprefix $(LINK_OBJ_DIR)/,$(OBJS))
DEPS := $(addprefix $(DEP_DIR)/,$(DEPS))

# 找到目录中的所有 .o 文件(编译出来的),  
LINK_OBJ = $(wildcard $(LINK_OBJ_DIR)/*.o)
# 因为构建依赖关系时 app 目录下这个 .o 文件还没有构建出来, 所以LIBK_OBJ 是确实这个 .o 文件的, 我要把这个 .o 文件加进来,
LINK_OBJ += $(OBJS)

#--------------------------------------------------------------------------
# make 找第一个目标开始执行[每个目标就是我要生成的东西], 其实都是定义一种依赖关系, 目标的格式为:
# 目标: 目标依赖(可以省略)
#	要执行的命令(可以省略)
# 如下这行会是开始执行的入口, 执行就找到依赖项 $(BIN) 去执行了, 同时, 这里也依赖了 $(DEPS), 这样就会生成很多,
all: $(DEPS) $(OBJS) $(BIN)

# 这里是诸多 .d 文件被包含进来, 每个 .d 文件里都记录着一个 .o 文件所依赖哪些.c .h 文件内容诸如 nginx.o: nginx.c  nginx.h,
# 我做这个最终目的说白了是, 即便 .h 被修改了, 也要让make 重新编译我的工程, 否则, 你修改了 .h, make不会重新编译, 
# 有必要先判断这些文件是否存在 不然 make 可能会报一些 .d 文件找不到, 
ifneq ("$(wildcard $(DEPS))","") # 如果不为空, $(wildcard $(DEPS)) 是函数(获取匹配模式文件名), 这里用于比较是否为 "" 空串,
include $(DEPS)
endif 

#------------------------------------ 1 begin --------------------------------------
# $(BIN):$(OBJS)
$(BIN):$(LINK_OBJ)
	@echo "----------------------- build $(VERSION) mode -----------------------!!!"

# 一些变量, $@: 目标,   $^:所有目标依赖, 
# gcc -o 生成可执行文件
	$(CC) -o $@ $^ -lpthread 

#------------------------------------ 1 end --------------------------------------


#------------------------------------ 2 begin --------------------------------------
# %.o:%.c,   $@ 代表目标 "$(LINK_OBJ_DIR)/%.o" ,
$(LINK_OBJ_DIR)/%.o:%.cxx
# gcc -c 是生成 .o 目标文件   -L 可以指定头文件的路径,
# 如下不排除有其他字符串 所以从其中专门把 .c 过滤出来,
# $(CC) -o $@ -c $^
	$(CC) -I$(INCLUDE_PATH) -o $@ -c $(filter %.cxx,$^) 
#------------------------------------ 2 end --------------------------------------



#------------------------------------ 3 begin --------------------------------------
# 我现在希望当修改一个 .h 时, 也能够让 make 自动重新变成我的项目, 所以我需要指明让 .o 依赖于 .h 文件,
# 那么一个 .o 依赖于哪些 .h文件，我可以用"gcc -MM c程序文件名", 来获得这些依赖信息, 并重定向保存到 .d 文件中,
# .d 文件中的内容可能形如 : nginx.o: nginx.c nginx.h
# %.d:%.c
$(DEP_DIR)/%.d:%.cxx
# gcc -MM $^ > $@ ,  $@ 表示目标 "$(DEP_DIR)/%.d"
# .d文件中的内容形如 nginx.o nginx.c ngx_func.c ngx_func.h ../signal/ngx_signal.h,但现在问题是我的 .o 文件已经, 
# 所以我要正确知名.o 文件路径这样, 对应的 .h .c修改后, make时才能发现, 这里需要用到sed文本处理工具和一些正确,
# gcc -MM $^ | sed 's,\(.*\)\.o[ :]*,$(LINK_OBJ_DIR)/\1.o:,g' > $@
# echo 中 -n 表示 后续追加不换行,
	echo -n $(LINK_OBJ_DIR)/ > $@
# 	gcc -MM $^ | sed 's/^/$(LINK_OBJ_DIR)&/g' > $@
#   >> 表示追加,  ,
# gcc -I$(INCLUDE_PATH) -MM $^ >> $@
	$(CC) -I$(INCLUDE_PATH) -MM $^ >> $@

# 上行处理后,  .d 文件中内容就应该如 : "/mnt/hgfs/linux/nginx/app/link_obj/nginx.o : nginx.c nginx_func.h ../singnal/ngx_signal.h",
# $ gcc -I ~/MyWorkspace/LinuxArchitecture/cpp_ngx_code_dir/nginx/_include -MM ./nginx.c  #==> 显示如下,
# nginx.o: nginx.c \
#  /home/zhangxi/MyWorkspace/LinuxArchitecture/cpp_ngx_code_dir/nginx/_include/ngix_signal.h \
#  /home/zhangxi/MyWorkspace/LinuxArchitecture/cpp_ngx_code_dir/nginx/_include/ngix_signal2.h \
#  /home/zhangxi/MyWorkspace/LinuxArchitecture/cpp_ngx_code_dir/nginx/_include/ngx_func.h

#------------------------------------ 3 end --------------------------------------

#------------------------------------ 4 begin --------------------------------------
# clean:
# rm 的 -f 参数是不提示强制删除,
# 可能 gcc  产生 .gcb 这个优化编译速度文件,
# 	rm -rf $(BIN) $(OBJS) $(DEPS) *.gch
#------------------------------------ n end --------------------------------------


