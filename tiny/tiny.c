/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
                 
// listenfd = 3
// connfd = 4
// srcfd = 5

/* 
 * tiny main routine - main 함수는 서버 측에서 listenfd(소켓)를 생성하고 커맨드 라인에 입력한 포트로(./tiny 5000이라고 입력했다면 포트는 5000) 클라이언트의 connect 요청이 들어오길 기다리는 함수이다.
 * tiny 서버는 동시성 서버가 아니다. 따라서 하나의 통신 혹은 서비스 밖에 처리할 수 없다.(라고 이해함)
 */
// ./tiny 5000처럼 입력 시 argc = 2, argv[0] = tiny, argv[1] = 5000이 된다.
// port 번호를 인자로 받는다. 
int main(int argc, char **argv) {
  int listenfd, connfd;                                                               // 서버 측에서 생성하는 listenfd, connfd 소켓
  char hostname[MAXLINE], port[MAXLINE];                                              // 클라이언트의 hostname(IP 주소)과 port 번호
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;                                                 // 클라이언트에서 connect 요청을 보내면, 서버 측에서 알 수 있게 되는 클라이언트 소켓의 주소이다.
  
  // 클라이언트 입력의 argument(인자) 갯수가 2가 아니면 에러를 출력한다. 서버 측에 주소랑 포트 번호를 알려줘야, 서버는 거기에 맞는 프로세스를 찾아서 클라이언트 측으로 넘겨줄 수 있으니까!
  // int fprintf(FILE* stream, const char* format, ...) : 해당 파일을 열어 format을 작성한다.
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);                                   // 인자 2개를 입력한 것이 아니면 usage: ./tiny <port>라는 문구가 보일 것이다. 이는 ./tiny <port> 형식으로 쓰라는 의미이다.
    exit(1);                                                                          // 해당 파일 자체가 종료되므로 서버 종료
  }
  
  listenfd = Open_listenfd(argv[1]);                                                  // 해당 포트 번호에 해당하는 listen 소켓 디스크립터를 열어준다.

  // 서버는 listenfd 소켓을 생성해놓은 상태에서, 클라이언트가 connect 요청을 하여 서버 자신이 accept할 때까지 계속 기다리게 된다.
  while (1) {
    clientlen = sizeof(clientaddr);                                                     
    
    // 서버가 생성한 listenfd 소켓에 클라이언트의 connect 요청(클라이언트의 IP 주소와 클라이언트의 port 번호를 보내는 것으로 생각하자)이 맞물린다고 이해하자.
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                         // 클라이언트가 connect 요청을 하면 서버 측은 accept하게 된다. 그 이후 서로 데이터를 읽고 쓸 수 있는 공간(소켓)인 connfd가 생성된다.
                                                                                      // 이후 네트워크 통신에서 사용하기 좋은 RIO 패키지를 이용하여 이 connfd에 입출력을 하게 된다.
                                                                                      // 클라이언트가 자신의 IP 주소와, 클라이언트 프로세스를 유일하게 식별할 수 있는 포트를 보낸다. 이 때 포트 번호는 클라이언트의 포트 번호인데, 임의로 설정될 것이다.
    
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);   // 서버 측에서 클라이언트의 hostname과 클라이언트의 port 정보를 가져온다.
    printf("Accepted connection from (%s %s)\n", hostname, port);    
    
    doit(connfd);                                                                     // 서버 측에서 서비스를 실행하는 부분이다.
    
    Close(connfd);                                                                    // 클라이언트로부터의 요청을 통해 생성된 connfd로 서비스(doit)를 처리하고 나면 해당 connfd 소켓은 닫는다. 더 이상 필요하지 않고, tiny는 동시성 서버가 아니니까
  }
}

/*
 * doit - HTTP 트랜잭션, 즉 클라이언트의 요구를 서버가 그에 맞게 서비스하는 함수이다.
 */
// 한 개의 HTTP 트랜잭션을 처리한다.
// 이 때 fd는 connfd이다.
void doit(int fd) {
  int is_static;                                                                      // 클라이언트가 보낸 요청이 정적 컨텐츠를 요구하는 것인지, 혹은 동적 컨텐츠를 요구하는 것인지 판단할 flag이다. 후에 parse_uri에서 더 자세히 설명된다.
  struct stat sbuf;                                                                   // server buffer?
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];                 // 클라이언트에게서 받은 요청(클라이언트의 request header)을 바탕으로 해당 변수들을 채우게 된다.
  char filename[MAXLINE], cgiargs[MAXLINE];                                           // 클라이언트가 요구한 filename과, (동적 컨텐츠를 요구했다면 존재할) 인자들. 나중에 parse_uri를 통하여 채워지게 된다.
  rio_t rio;                                                                          // 네트워크 통신에 효과적인 RIO 패키지 구조체
  
  // 클라이언트가 보낸 request header를 RIO 패키지로 읽고 분석한다.
  Rio_readinitb(&rio, fd);                                                            // connfd를 rio에 위치한 rio_t 타입의 읽기 버퍼(그냥 RIO I/O를 쓸 수 있는 공간이라고 이해하자)와 연결한다. 네트워크 통신에 적합한(short count를 자동으로 처리하는) RIO I/O로 connfd를 통해 데이터를 읽고 쓸 수 있게 만든다.
  Rio_readlineb(&rio, buf, MAXLINE);                                                  // rio에 있는, 즉 connfd에 있는 문자열 한 줄을 읽어와서 buf로 옮긴다. 그리고 그 문자열 한 줄을 NULL로 바꾼다.
  printf("Request headers:\n");
  printf("%s", buf);                                                                  // 위 Rio_readlineb에서 한 줄을 읽었다. buf = "GET /godzilla.gif HTTP/1.1\0"를 출력해준다. 아마 connfd의 첫째 줄에는 클라이언트가 보낸 저 문구가 있지 않았을까?
  sscanf(buf, "%s %s %s", method, uri, version);                                      // buf에서 문자열 3개를 가져와서 method, uri, version이라는 문자열에 저장한다. 즉 위 예시대로라면 GET, godzilla.gif, HTTP/1.1 정도가 저장될 것이다.
  
  // 클라이언트의 request header의 method가 GET이 아니면 doit 함수는 끝나고 main으로 돌아가서 Close가 된다. 하지만 while(1)이므로 서버는 계속 연결 요청을 기다리게 된다.
  if (strcasecmp(method, "GET")) {                                                    // 우리가 만드는 Tiny 서버는 GET만 지원하므로 GET이 아닐 경우 connfd에 에러 메세지를 띄운다. strcasecmp는 대문자 상관없이 인자를 비교하고 같으면 0을 반환한다.
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  
  // request line을 뺀 나머지 request header 부분을 무시한다. 그냥 읽어서 프린트할 뿐 실제로 무언가를 하지 않는다.
  read_requesthdrs(&rio);
  
  // 클라이언트 요청을 분석해서 클라이언트가 요구한 것이 무엇인지(정적 혹은 동적 컨텐츠인지, 요구한 filename은 무엇인지(추출), 요구한 인자는 무엇인지(추출)) 클라이언트가 보낸 uri를 통해 분석한다. 아마 open_clientfd로 다 보내지 않았을까? accept에서 받았을 것이다.
  is_static = parse_uri(uri, filename, cgiargs);                                      // request가 정적 컨텐츠를 위한 것인지, 동적 컨텐츠를 위한 것인지 flag를 설정한다.
                                                                                      // 이 때 클라이언트가 보낸 connect 요청을 분석해서 (클라이언트가 서버 측에 요구한)filename과 인자를 채운다.
                                                                                      
  // 요청한 파일이 서버의 디스크 상에 있지 않다면 connfd에 에러 메세지를 띄운다(클라이언트에게 에러 메세지를 띄운다). 그리고 main으로 돌아가서 서버는 또 연결 요청을 기다린다.
  // stat은 서버 측에 filename과 일치하는 file이 있으면 그에 대한 정보를 sbuf(filename에 대한 정보를 저장하는 stat 구조체)에 업데이트 시킨다.
  // 즉 클라이언트가 요구한 filename에 대해, 서버는 자신한테 그 파일이 있는지 확인하고 있다면 그 파일에 대한 정보를, 따로 구조체를 만들어 저장 시킨다.
  // stat은 성공 시 0을 리턴하고, 실패 시 -1을 리턴한다.
  if (stat(filename, &sbuf) < 0) {                                                    
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");    // connfd
    return;                                                                           
  }
  
  // 정적 컨텐츠라면
  if (is_static) {
    // 동적 컨텐츠라면, 이번에는 클라이언트가 요구한 서버의 파일에 대해 검증한다.
    // 일반 파일(regular file)인지, 읽기 권한이 있는지 확인한다.
    // 일반 파일이 아니거나 읽기 권한이 없다면 에러 메세지를 띄우고 이 또한 doit을 종료해 main으로 돌아가 연결 요청을 기다리게 한다.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");   
      return;
    }
    
    // 정적 컨텐츠를 클라이언트로 보내주는 작업을 수행한다.
    // 정적 컨텐츠라서 인자가 필요하지 않으니 따로 인자를 넘겨줄 필요는 없다.
    serve_static(fd, filename, sbuf.st_size);
  }
  
  // 동적 컨텐츠라면
  else {
    // 동적 컨텐츠라면, 이번에는 클라이언트가 요구한 서버의 파일에 대해 검증한다.
    // 일반 파일인지, 실행 권한이 있는지 확인한다.
    // 일반 파일이 아니거나 읽기 권한이 없다면 에러 메세지를 띄우고 이 또한 doit을 종료해 main으로 돌아가 연결 요청을 기다리게 한다.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    
    // 동적 컨텐츠를 클라이언트로 보내주는 작업을 수행한다.
    // 동적 컨텐츠이니 클라이언트가 요구한 인자도 같이 넘겨준다.
    serve_dynamic(fd, filename, cgiargs);
  }
}

// 에러메세지와 응답 본체를 connfd를 통해 클라이언트에 보낸다.
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  
  // HTTP 응답 body를 구성한다.
  // 즉 에러가 발생했을 때 클라이언트 측에 보여줄 에러 HTML 파일을 구성하는 것이다.
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // HTTP 응답 메세지를 작성한다.
  // 이부분은 굳이 왜이렇게 하는지 모르겠다. sprintf로 한번에 저장해뒀다가 Rio_writen으로 보내주면 안될까?
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));

  // HTTP 응답 body와 HTTP 응답 메세지를 connfd에게 보내준다.
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);

  /* 버퍼 rp의 마지막 끝을 만날 때까지("Content-length: %d\r\n\r\n에서 마지막 \r\n) */
  /* 계속 출력해줘서 없앤다. */
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
 * parse_uri - GET Request URI form을 분석한다.
 */
// URI를 받아 요청 받은 파일의 이름(filename)과 요청인자(cgiargs)를 채운다.
// 정적 컨텐츠 URI 예시 : http://54.180.101.138:5000/home.html, uri = /home.tml
// 동적 컨텐츠 URI 예시 : http://54.180.101.138:5000/cgi-bin/adder?123&123, uri = /cgi-bin/adder?123&123
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  /* uri에 cgi-bin이 없다면, 즉 정적 컨텐츠를 요청한다면 1을 리턴한다.*/
  // 예시 : GET /godzilla.jpg HTTP/1.1 -> uri에 cgi-bin이 없다
  
  // URI에 cgi-bin이 없다면, 즉 클라이언트가 정적 컨텐츠를 요청했다면
  // strstr은 해당 문자열이 있으면 그 문자열을 가르키는 포인터를, 없으면 NULL을 반환한다.
  if (!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");                                                              // 정적이니까 cgiargs는 필요가 없다. 있지도 않을 것이다. 아마
    strcpy(filename, ".");                                                            // 상대 경로에서, ./이란 현재 폴더, 즉 현재 경로를 말한다. filename은 .이 된다.
    strcat(filename, uri);                                                            // filename에 URI 문자열을 이어붙인다. 이 때 URI는 아마 home.html과 같을 것이다. 따라서 ./home.html이 될 것이다.
    
    // 만약 클라이언트가 http://54.180.101.138:5000/과 같이 접속한다면, 알아서 http://54.180.101.138:5000/home.html이 되도록 한다.
    // 그래서 http://54.180.101.138:5000/home.html/으로 접속하면 http://54.180.101.138:5000/home.html/home.html이 돼서 에러 메세지가 뜰 수도 있다.
    // 어쨋든, URI 뒤의 맨 마지막이 /이라면 그냥 home.html이라는 정적 컨텐츠가 제공되도록 한다.
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    
    // 정적 컨텐츠면 parse_uri는 1 리턴
    return 1;
    
    // uri = /home.html에서
    // cgiargs = ""
    // filename = ./home.html로 된다.
  }
  
  // URI에 cgi-bin이 있다면, 즉 클라이언트가 동적 컨텐츠를 요청했다면
  else {
    ptr = index(uri, '?');                                                            // uri에서 ?을 가리키는 포인터를 만든다. QUERY_STRING, 즉 클라이언트가 보낸 인자를 추출해내기 위해서이다.

    // '?'가 있으면 cgiargs를 '?' 뒤 인자들과 값으로 채워주고 ?를 NULL로 만든다.
    if (ptr) { 
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    
    // '?'가 없으면 그냥 아무것도 안 넣어준다.
    else {
      strcpy(cgiargs, "");
    }
    
    strcpy(filename, ".");  // 현재 폴더에서 시작
    strcat(filename, uri);  // uri를 붙여준다.
                            // 동적 컨텐츠일 때 인자가 존재할 경우인 if를 만났었더라도 ptr이 가리키는 ?을 \0으로 바꿨으니 뒤의 인자는 날아간 형태로 붙게 된다.
                            
    // 동적 컨텐츠면 parse_uri는 0 리턴
    return 0;
    
    // uri = /cgi-bin/adder?123&123에서
    // cgiargs = 123&123
    // filename = ./cgi-bin/adder이 된다.
  }
}

/*
 * serve_static - 응답 메세지를 구성하고 정적 컨텐츠를 처리(connfd로)한다.
 */   
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // 응답 메세지를 구성한다.
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");    
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  
  /* 응답 메세제지를 클라이언트에게 보냄 */
  Rio_writen(fd, buf, strlen(buf));                                                   // connfd를 통해 클라이언트에게 보낸다.
  printf("Response headers:\n");                                                      // 서버 측에서도 출력한다.
  printf("%s", buf);                                                                  

  // Mmap, Munmap 이용 시
  // srcfd는 home.html을 가리키는 식별자
  // 이 떄 이 srcfd를 가상 메모리에 할당한다.
  // 이 가상 메모리를 다시 connfd로 옮긴다.
  // 그리고 이 srcfd를 프리시킨다.
  // srcfd = Open(filename, O_RDONLY, 0);                                                // filename의 이름을 갖는 파일을 읽기 권한으로 불러온다.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);                         // 메모리에 파일 내용을 동적할당한다.
  // Close(srcfd);                                                                       // 파일을 닫는다. 
  // Rio_writen(fd, srcp, filesize);                                                     // 동적 할당을 받아 메모리에 복사한, 즉 srcp가 가리키는 메모리에 있는 파일 내용들을 fd에 보낸다.
  // Munmap(srcp, filesize);                                                             // 할당 받은 것을 해제시킨다.
  
  // Malloc, free 이용시
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = (char *)Malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Free(srcp);
}

/*
 * get_filetype - 클라이언트가 서버 측에 요구한 filename에서 파일의 유형(html, png, jpg, mp4 등등)을 파악하고 filetype에 저장한다.
 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else if (strstr(filename, ".mpeg"))
    strcpy(filetype, "video/mpeg");
  else
    strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - 응답 메세지를 구성하고 동적 컨텐츠를 처리(connfd로)한다.
 */   
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  // 응답 메세지를 구성한다.
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // 자식 프로세스를 생성한다. fork시, 자식 프로세스의 반환 값은 0이다. 부모는 0이 아닌 다른 값이다. 즉 자식 프로세스일 경우 Fork()는 0이라서 if문에 들어가게 된다.
  // fork()하면 파일 테이블도 같이 복사된다. 다른 것은 fork 반환 값인 프로세스 id인 것으로 알고 있다.
  if (Fork() == 0) {
    // 환경 변수에서 QUERY_STRING에 클라이언트가 요구한 인자들을 등록한다.
    // 여기서 QUERY_STRING은 URI에서 클라이언트가 보낸 인자인 id=HTML&name=egoing와 같은 부분이다.
    // 이를 통해서 우리의 동적 컨텐츠 처리 애플리케이션(응용)인 adder.c가 이 환경변수의 QUERY_STRING으로 동적인 처리를 할 수 있게 된다. 여기서 동적인 처리는 그냥 더하기이다. 단순히 인자를 통해서 새로운 결과물을 낼 수 있기 때문에 동적 처리라고 할 수 있고 우리는 그 예로 아주 간단한 더하기를 사용하였다.
    setenv("QUERY_STRING", cgiargs, 1);

    // 클라이언트의 표준 출력을 CGI 프로그램의 표준 출력과 연결한다.
    // 이제 CGI 프로그램에서 printf하면 클라이언트에서 출력된다.
    Dup2(fd, STDOUT_FILENO);                                                          // 표준 출력을 connfd로 항하게 한다. 이렇게 하면 Execve를 통해 adder.c가 실행되어 출력되는 값이 connfd에 출력되게 할 수 있는 느낌이다.
    Execve(filename, emptylist, environ);                                             // adder.c가 실행된다. 현 코드 영역을 모두 지우고 adder.c의 코드로 채워지게 된다.
                                                                                      // Execve는 프로그램을 실행시키는 함수이다.
                                                                                      
    // Execve는 현 영역의 코드를 모두 지우고, 실행할 파일의 코드로 코드 영역을 채운다. 그렇기 때문에 부모 프로세스로만 하면, adder.c의 코드로 채워져서 더 이상 서버 역할을 할 수 없게 된다. 서버 측의 코드가 모두 지워져 버렸으니까
    // 따라서 자식 프로세스를 만들고, 자식 프로세스만 Execve를 하게 해 adder.c 파일을 수행하도록 한다.
    // 부모는 if문을 피하여 Wait(NULL)을 만나, 자식 프로세스의 동작이 끝나 자식 프로세스의 status가 null이 될 때까지 기다리게 된다.
    // 자식 프로세스의 status가 NULL이 되면 부모 프로세스는 다시 동작하게 되고, serve_dynamic 함수를 빠져나가 또 main 함수의 while 문을 돌게 되어 클라이언트의 새로운 connect 요청을 기다리게 된다.
  }
  Wait(NULL); /* Parent waits for and reaps child */
}