#include "../../include/http/http.h"

map<string, string> httpd::resource;

void httpd_handler(struct evhttp_request *req, void *arg);
void signal_handler(int sig);

httpd::httpd(string _listen, uint16_t _port, bool _daemon) : http_listen(_listen), http_port(_port), httpd_option_daemon(daemon), http_version("1.0.0"), httpd_name("myhttp"), http_timeout(60)
{
    resource.insert(pair<string, string>("/", "../src/resource/index.html"));
}
httpd::~httpd()
{
    evhttp_free(myhttp);
}
void httpd::SetHttpListen(int _listen)
{
    http_listen = _listen;
}
void httpd::SetHttpOptionDaemon(bool _daemon)
{
    httpd_option_daemon = _daemon;
}
void httpd::SetHttpPort(uint16_t _port)
{
    http_port = _port;
}
void httpd::SetTimeout(int _timeout)
{
    http_timeout = _timeout;
}

void httpd::HttpInit()
{
    event_init();
    myhttp = evhttp_start(http_listen.c_str(), http_port);
    evhttp_set_timeout(myhttp, http_timeout);
    evhttp_set_gencb(myhttp, httpd_handler, NULL);

    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
}
void httpd::Start()
{
    event_dispatch();
}

void httpd_handler(struct evhttp_request *req, void *arg)
{

    char output[2048] = "\0";
    char tmp[1024];
    //获取客户端请求的URI(使用evhttp_request_uri或直接req->uri)
    const char *uri;
    uri = evhttp_request_uri(req);
    cout<<"request uri is"<<uri<<endl;
    auto item = httpd::resource.find(string(uri));
    if (item != httpd::resource.end())
    {
        cout << item->second << endl;
        evhttp_add_header(req->output_headers, "Server", MYHTTPD_SIGNATURE);
        evhttp_add_header(req->output_headers, "Content-Type", "text/html; charset=UTF-8");
        evhttp_add_header(req->output_headers, "Connection", "keep-alive");
        int fd = open(item->second.c_str(), O_RDONLY);
        if (fd == -1)
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "NOT FOUND", NULL);
            return ;
        }
        //成功打开文件
        struct stat statbuff;
        fstat(fd,&statbuff);
        read(fd,output,statbuff.st_size);
        //
        struct evbuffer* buff=evbuffer_new();
        evbuffer_add_printf(buff,"%s",output);
        evhttp_send_reply(req,HTTP_OK,"OK",buff);
        evbuffer_free(buff);
    }
    else
    {
        cout << "can't find resource!" << endl;
        evhttp_add_header(req->output_headers, "Server", MYHTTPD_SIGNATURE);
        evhttp_add_header(req->output_headers, "Content-Type", "text/html; charset=UTF-8");
        evhttp_add_header(req->output_headers, "Connection", "close");
        evhttp_send_reply(req,HTTP_BADREQUEST,"NOT FOUND",NULL);
    }

    /* 输出到客户端 */
    // HTTP header

    //输出的内容
}
void signal_handler(int sig)
{
    switch (sig)
    {
    case SIGTERM:
    case SIGHUP:
    case SIGQUIT:
    case SIGINT:
        event_loopbreak(); //终止侦听event_dispatch()的事件侦听循环，执行之后的代码
        break;
    }
}