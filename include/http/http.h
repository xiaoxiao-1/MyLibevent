#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include <vector>
#include <unistd.h> //for getopt, fork
// for struct evkeyvalq
#include <sys/queue.h>
#include <event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/http_compat.h>
#include <event2/util.h>
#include <signal.h>
#include <map>
#include <algorithm>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <assert.h>
#define MYHTTPD_SIGNATURE "myhttpd v 0.0.1"
using namespace std;
class httpd
{
private:
    uint16_t http_port;
    string http_listen;
    bool httpd_option_daemon;
    int http_timeout;
    const string http_version;
    const string httpd_name;
    struct evhttp *myhttp;

public:
    static map<string, string> resource;
    httpd(string _listen = "127.0.0.1", uint16_t _port = 8080, bool _daemon = false);
    ~httpd();

    void SetHttpPort(uint16_t _port);
    void SetHttpListen(int _listen);
    void SetHttpOptionDaemon(bool _daemon);
    void SetTimeout(int _timeout);
    void HttpInit();
    void Start();
};