#include "../../include/http/http.h"

// void show_help() {
//     char *help = "http://localhost:8080\n"
//         "-l <ip_addr> interface to listen on, default is 0.0.0.0\n"
//         "-p <num>     port number to listen on, default is 1984\n"
//         "-d           run as a deamon\n"
//         "-t <second>  timeout for a http request, default is 120 seconds\n"
//         "-h           print this help and exit\n"
//         "\n";
//     fprintf(stderr,"%s",help);
// }

int main(int argc, char *argv[]) {
    // int c;
    // while ((c = getopt(argc, argv, "l:p:dt:h")) != -1) {
    //     switch (c) {
    //         case 'l' :
    //             httpd_option_listen = optarg;
    //             break;
    //         case 'p' :
    //             httpd_option_port = atoi(optarg);
    //             break;
    //         case 'd' :
    //             httpd_option_daemon = 1;
    //             break;
    //         case 't' :
    //             httpd_option_timeout = atoi(optarg);
    //             break;
    //         case 'h' :
    //         default :
    //             show_help();
    //             exit(EXIT_SUCCESS);
    //     }
    // }
    //判断是否设置了-d，以daemon运行
    // if (httpd_option_daemon) {
    //     pid_t pid;
    //     pid = fork();
    //     if (pid < 0) {
    //         perror("fork failed");
    //         exit(EXIT_FAILURE);
    //     }
    //     if (pid > 0) {
    //         //生成子进程成功，退出父进程
    //         exit(EXIT_SUCCESS);
    //     }
    // }
    /* 使用libevent创建HTTP Server */
    //初始化event API
    httpd httpserver;
    httpserver.HttpInit();
    httpserver.Start();
    return 0;
}