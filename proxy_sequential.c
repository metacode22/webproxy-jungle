#include <stdio.h>
#include "csapp.h"

// 주로 추천되어지는 캐시와 캐시 오브젝트의 최대 사이즈
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv;10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0 \r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key = "User_Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    
    struct sockaddr_storage clientaddr;
    
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    
    listenfd = Open_listenfd(argv[1]);
    while(1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s). \n", hostname, port);
        
        doit(connfd);
        
        Close(connfd);
    }
}

void doit(int connfd) {
    int end_serverfd;                                                                       // end_server를 가리키는 파일 디스크립터
    
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    
    char hostname[MAXLINE], path[MAXLINE];
    int port;
    
    rio_t rio, server_rio;                                                                  // 이 때 그냥 rio는 client의 rio이고, server_rio는 end_server의 rio이다.
    
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    
    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method");
        return;
    }
    
    // client가 보낸 uri를 분석하여 hostname, path, port 값을 갱신한다.
    parse_uri(uri, hostname, path, &port);
    
    // proxy는, end_server로 보낼 HTTP header를 구성한다.
    // build_http_header함수를 거쳐 완성될 end_server의 응답 헤더, end_server의 hostname(IP 주소), path?, 서버 port 번호, client와 proxy간의 connfd
    build_http_header(endserver_http_header, hostname, path, port, &rio);
    
    // proxy를 end_server와 연결시킨다.
    // 여기서 end_serverfd는, tiny에서 connfd와 역할이 같다.
    end_serverfd = connect_endServer(hostname, port, endserver_http_header);
    
    // 연결 실패 시 에러 문구 띄우고 end_server와 연결할 수 없다는 의미이므로 main 함수가 종료된다. 즉 proxy 서버 또한 종료된다.
    // hostname이나 port에, 즉 client가 end_server에 잘못된 주소로 요청을 할 때, 정확히는 존재하지 않는 주소 및 포트 번호로 요청할 때 이러한 에러가 발생할 수 있다.(뇌피셜)
    if (end_serverfd < 0) {
        printf("connection failed\n");
        return;
    }
    
    // end_serverfd, 즉 proxy와 end_server간의 데이터를 쓰고 읽는 공간에 RIO 패키지를 연결해, RIO 방식으로 proxy와 end_server간 RIO 패키지를 이용해 데이터를 읽고 쓸 수 있도록 만든다.
    Rio_readinitb(&server_rio, end_serverfd);
    Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));         // endserver_http_header에서 end_serverfd로 endserver_http_header만큼의 바이트를 전송한다.
    
    size_t n;
    while((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {                            // end_server의 응답 한줄 한줄을 buf에 저장한다. 한줄이 0바이트이면, 즉 다 읽었으면 while문을 빠져나간다. end_server의 응답을 proxy가 받은 셈이다.
        printf("proxy received %d bytes, then send\n", n);                                  // 한줄 한줄 얼마만큼의 바이트를 읽었는지 출력시킨다.
        Rio_writen(connfd, buf, n);                                                         // proxy는 end_server로부터 받은 응답 한줄인 buf를, client와 proxy가 연결된 공간(소켓)인 connfd을 통해 client에게 데이터를 전송한다.
    }
    
    Close(end_serverfd);                                                                    // end_server로부터 원하는 데이터를 다 받았으니 proxy와 end_server간의 공간인 end_serverfd를 닫는다.
}

// doit 함수에서 build_http_header를 사용하는 부분 : build_http_header(endserver_http_header, hostname, path, port, &rio);
// requestlint_hdr_format은 static const char *requestlint_hdr_format = "GET %s HTTP/1.0 \r\n"으로 선언되었다.
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
    
    sprintf(request_hdr, requestlint_hdr_format, path);                                     // requestlint_hdr_format의 %s에 path가 들어가고, 그렇게 완성된 requestlint_hdr_format이 reqeust_hdr이 된다.
    printf(request_hdr, requestlint_hdr_format, path);
    
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {                                   // 여기서 client_rio는, client와 proxy 사이의 공간인 connfd와 연결되어있다. 즉, 클라이언트가 보낸 데이터 한줄 한줄을 buf에 담게 된다.
        
        // 응답 헤더를 다 읽으면 읽는 동작 break
        if (strcmp(buf, endof_hdr) == 0) {                                                  // endof_hdr = "\r\n"이다. 즉 buf가 "\r\n"이면 client의 요청 헤더 끝줄이라는 뜻이므로 읽는 것을 그만하도록 한다.
            break;
        }
        
        // host만 있는 host header를 따로 저장
        if (!strncasecmp(buf, host_key, strlen(host_key))) {                                // host_key = "Host"였다. 대소문자 구분 없이 n만큼 buf와 host_key를 비교한다. Host: ~~~~라는 부분을 만났을 때, 앞이 Host가 맞으면 buf의 문자열을 host_hdr에 복사한다.
            strcpy(host_hdr, buf);                                                          // ex. Host : 123.123.123.123에서 앞 글자가 Host(host, HoSt라도 상관없다)이니 해당 if 조건을 만족한다.
            continue;                                                                       // continue이므로 아래 코드를 읽는 것이 아니라 다시 while문으로 간다.
        }
        
        // 다른 헤더들 저장
        if(strncasecmp(buf,connection_key,strlen(connection_key)) &&strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key)) &&strncasecmp(buf,user_agent_key,strlen(user_agent_key))) {
            strcat(other_hdr, buf);
        }        
    }
    
    // 만약 client가 따로 host header를 날리지 않았다면 인가?
    // static const char *host_hdr_format = "Host: %s\r\n";
    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, host_hdr_format, hostname);
    }
    
    // http_header는 doit 함수에서 endserver_http_header였다. 즉, endserver_http_header를 구성한다.
    sprintf(http_header,"%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);
            
    return;
}

// proxy와 end_server를 연결한다.
inline int connect_endServer(char *hostname, int port, char *http_header) {
    char portStr[100];
    sprintf(portStr, "%d", port);
    return Open_clientfd(hostname, portStr);
}

void parse_uri(char *uri, char *hostname, char *path, int *port) {
    *port = 80;                                                                             // HTTP 기본 포트 설정 값
    char* pos = strstr(uri, "//");
    
    pos = ((pos != NULL) ? (pos + 2) : (uri));                                              // pos가 NULL이 아니라면 pos는 // 바로 뒤를 가리키게 된다. NULL이면 uri의 처음을 가리키게 된다.
    
    char *pos2 = strstr(pos, ':');
    
    if (pos2 != NULL) {
        *pos2 = '\0';
        sscanf(pos, "%s", hostname);
        sscanf(pos2 + 1, "%d%s", port, path);
    } else {
        pos2 = strstr(pos, "/");
        if(pos2 != NULL)
        {
            *pos2 = '\0';
            sscanf(pos, "%s", hostname);
            *pos2 = '/';
            sscanf(pos2, "%s", path);
        }
        else
        {
            sscanf(pos, "%s", hostname);
        }
    }
    
    return;
}
