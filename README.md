# net_server

#### introduce

cpp_net_server

#### Software Architecture Description

1.net_server/nginx/ 是服务器代码，基于C++11开发； net_server/test_utils/ 是测试工具，基于MFC开发。 
2.配置文件 net_server/nginx/nginx.conf。 
3.构建多进程版TCP服务器，可以灵活配置 worker 进程数, 这里解决了惊群问题。 
4.线程池中处理消息、心跳超时服务器断开连接 都做了处理。 5.flood恶意攻击也都做了相应处理。

#### Install the tutorial

1.  cd net_server/nginx/
2.  make

#### Directions for use

1.  需要 root 权限, cd net_server/nginx/; sudo ./nginx
2.  测试工具 net_server/test_utils/ 目录下，需要安装mfc, 然后编译，我的环境是vs2019

