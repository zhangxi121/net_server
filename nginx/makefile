
include config.mk
all:
# -C 是指定目录,
# make -c signal

# 可以执行文件应该放最后,
# make -C app

# 用shell命令和for搞, shell里边的变量用两个$,
	@for dir in $(BUILD_DIR); \
	do \
		make -C $$dir; \
	done 

clean:
# rm -rf 删除文件夹,
	rm -rf app/link_obj app/dep nginx
	rm -rf signal/*.gch  app/*.gch 
	





