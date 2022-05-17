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
                 
                 // 3 서버
                 // 4 클라이언트
                 // 5 home.html

// ex. ./tiny 5000과 같이 입력 : argc = 2, argv[0] = tiny, argv[1] = 5000
// port 번호를 인자로 받는다. 
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;                                                 // 클라이언트에서 connect 요청을 보내면, 서버 측에서 알 수 있게 되는 클라이언트 소켓의 주소이다.
  
  // 클라이언트 입력의 argument(인자) 갯수가 2가 아니면 에러를 출력한다. 서버 측에 주소랑 포트 번호를 알려줘야, 서버는 거기에 맞는 프로세스를 찾아서 클라이언트 측으로 넘겨줄 수 있으니까!
  // int fprintf(FILE* stream, const char* format, ...) : 해당 파일을 열어 문구를 작성한다.
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);                                   // 인자 2개를 입력한 것이 아니면 usage: ./tiny <port>라는 문구가 보일 것이다. 이는 ./tiny <port> 형식으로 쓰라는 의미이다.
    exit(1);                                                                          // 해당 파일 자체가 종료되므로 서버 종료
  }
  
  listenfd = Open_listenfd(argv[1]);                                                  // 해당 포트 번호에 해당하는 listen 소켓 디스크립터를 열어준다.
  


  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                         // 
    printf("client가 connect 요청을 하여 accept가 됨 clientaddr:%p\n", clientaddr);
    
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);   // 
    printf("Accepted connection from (%s %s)\n", hostname, port);                     // hostname, port는 클라이언트다.
    
    doit(connfd);
    
    Close(connfd);              
  }
}

// 한 개의 HTTP 트랜잭션을 처리한다.
// 하나의 트랜잭션이 끝나고 나서 다른 트랜잭션을 처리할 수 있기에 동시성 서버가 아니라고 말할 수 있다.
// 클라이언트 요청을 확인하여 정적 컨텐츠인지 동적 컨텐츠인지 구분한다.
void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];                 // 클라이언트에게서 받은 요청(클라이언트의 request header)을 바탕으로 해당 변수들을 채우게 된다.
  char filename[MAXLINE], cgiargs[MAXLINE];                                          
  rio_t rio;
  
  // 클라이언트가 보낸 request header를 RIO 패키지로 읽고 분석한다.
  Rio_readinitb(&rio, fd);                                                            // connfd를 rio에 위치한 rio_t 타입의 읽기 버퍼(그냥 RIO I/O를 쓸 수 있는 공간이라고 이해하자)와 연결한다. 네트워크 통신에 적합한(short count를 자동으로 처리하는) RIO I/O로 connfd를 통해 데이터를 읽고 쓸 수 있게 만든다.
  Rio_readlineb(&rio, buf, MAXLINE);                                                  // rio에 있는, 즉 connfd에 있는 문자열 한 줄을 읽어와서 buf로 옮긴다. 그리고 그 문자열 한 줄을 NULL로 바꾼다.
  printf("Request headers:\n");
  printf("%s", buf);                                                                  // 요청 라인 buf = "GET /godzilla.gif HTTP/1.1\0"을 표준 출력만 해줌!
  sscanf(buf, "%s %s %s", method, uri, version);                                      // buf에서 문자열 3개를 가져와서 method, uri, version이라는 문자열에 저장한다.
  
  // 클라이언트의 request header의 method가 GET이 아니면 doit 함수는 끝나고 main으로 돌아가서 Close가 된다. 하지만 while(1)이므로 서버는 계속 연결 요청을 기다리게 된다.
  if (strcasecmp(method, "GET")) {                                                    // 우리가 만드는 Tiny 서버는 GET만 지원하므로 GET이 아닐 경우 connfd에 에러 메세지를 띄운다. strcasecmp는 대문자 상관없이 인자를 비교하고 같으면 0을 반환한다.
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  
  // request line을 뺀 나머지 request header 부분을 무시한다. 그냥 읽어서 프린트할 뿐 실제로 무언가를 하지 않는다.
  read_requesthdrs(&rio);
  
  // Parse URI from GET request
  // 정적이면 1
  // 동적이면 0
  is_static = parse_uri(uri, filename, cgiargs);                                      // request가 정적 컨텐츠를 위한 것인지, 동적 컨텐츠를 위한 것인지 flag를 설정한다.
  
  // 요청한 파일이 서버의 디스크 상에 있지 않다면 connfd에 에러 메세지를 띄운다(클라이언트에게 에러 메세지를 띄운다). 그리고 main으로 돌아가서 서버는 또 연결 요청을 기다린다.
  if (stat(filename, &sbuf) < 0) {                                                    // connfd라는 파일의 메타데이터를 추출한다.
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;                                                                           
  }
  
  // 정적 컨텐츠라면
  if (is_static) {
    // 일반 파일(regular file)인지, 읽기 권한이 있는지 확인한다.
    // 일반 파일이 아니거나 읽기 권한이 없다면 에러 메세지를 띄우고 이 또한 doit을 종료해 main으로 돌아가 연결 요청을 기다리게 한다.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");   
      return;
    }
    
    // 컨텐츠의 길이 출력
    serve_static(fd, filename, sbuf.st_size);
  }
  
  // 동적 컨텐츠라면
  else {
    // 일반 파일인지, 실행 권한이 있는지 확인한다.
    // 일반 파일이 아니거나 실행 권한이 없다면 에러 메세지를 띄우고 이 또한 doit을 종료해 main으로 돌아가 연결 요청을 기다리게 한다.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    
    serve_dynamic(fd, filename, cgiargs);
  }
}

// 에러메세지와 응답 본체를 connfd를 통해 클라이언트에 보낸다.
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));

  // 에러메세지와 응답 본체를 서버 소켓을 통해 클라이언트에 보낸다. 
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

// GET Request URI form을 분석한다.
// URI를 받아 요청 받은 파일의 이름(filename)과 요청인자(cgiargs)를 채운다.
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  /* uri에 cgi-bin이 없다면, 즉 정적 컨텐츠를 요청한다면 1을 리턴한다.*/
  // 예시 : GET /godzilla.jpg HTTP/1.1 -> uri에 cgi-bin이 없다
  if (!strstr(uri, "cgi-bin")) { /* Static content, uri안에 "cgi-bin"과 일치하는 문자열이 있는지. */
    strcpy(cgiargs, "");    // 정적이니까 cgiargs는 아무것도 없다.
    strcpy(filename, ".");  // 현재경로에서부터 시작 ./path ~~
    strcat(filename, uri);  // filename 스트링에 uri 스트링을 이어붙인다.

    // 만약 uri뒤에 '/'이 있다면 그 뒤에 home.html을 붙인다.
    // 내가 브라우저에 http://localhost:8000만 입력하면 바로 뒤에 '/'이 생기는데,
    // '/' 뒤에 home.html을 붙여 해당 위치 해당 이름의 정적 컨텐츠가 출력된다.
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");

    /* 예시
      uri : /godzilla.jpg
      ->
      cgiargs : 
      filename : ./godzilla.jpg
    */
    
    // 정적 컨텐츠면 1 리턴
    return 1;
  }
  else { /* Dynamic content */
    ptr = index(uri, '?');

    // '?'가 있으면 cgiargs를 '?' 뒤 인자들과 값으로 채워주고 ?를 NULL로 만든다.
    if (ptr) { 
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else // '?'가 없으면 그냥 아무것도 안 넣어준다.
      strcpy(cgiargs, "");

    strcpy(filename, ".");  // 현재 디렉토리에서 시작
    strcat(filename, uri);  // uri 넣어준다.

    /* 예시
      uri : /cgi-bin/adder?123&123
      ->
      cgiargs : 123&123
      filename : ./cgi-bin/adder
    */

    return 0;
  }
}
     
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client 클라이언트에게 응답 헤더 보내기*/
  /* 응답 라인과 헤더 작성 */
  get_filetype(filename, filetype);  // 파일 타입 찾아오기 
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  // 응답 라인 작성
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);  // 응답 헤더 작성
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  
  /* 응답 라인과 헤더를 클라이언트에게 보냄 */
  Rio_writen(fd, buf, strlen(buf));  // connfd를 통해 clientfd에게 보냄, buf를 fd에 출력시킨다.
  printf("Response headers:\n");
  printf("%s", buf);  // 서버 측에서도 출력한다.

  /* Send response body to client */
  // srcfd는 home.html을 가리키는 식별자
  // 이 떄 이 srcfd를 가상 메모리에 할당한다.
  // 이 가상 메모리를 다시 connfd로 옮긴다.
  // 그리고 이 srcfd를 프리시킨다.
  srcfd = Open(filename, O_RDONLY, 0); // filename의 이름을 갖는 파일을 읽기 권한으로 불러온다. 이 때 srcfd는 home.html을 가리킨다.
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 메모리에 파일 내용을 동적할당한다.
  Close(srcfd); // 파일을 닫는다.
  Rio_writen(fd, srcp, filesize);  // 해당 메모리에 있는 파일 내용들을 fd에 보낸다(읽는다).
  Munmap(srcp, filesize);
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))  // filename 스트링에 ".html" 
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)       // 이 때 fd는 connfd이다.
{
  char buf[MAXLINE], *emptylist[] = { NULL };
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { /* Child */
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1);  //                     // serve_dynamic를 통해 쿼리스트링에 cgiargs를 등록하고 이를 adder.c에서 사용할 수 있게 된다.

    // 클라이언트의 표준 출력을 CGI 프로그램의 표준 출력과 연결한다.
    // 이제 CGI 프로그램에서 printf하면 클라이언트에서 출력됨
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */    // fd는 connfd이다. 출력을 우리 모니터가 아니라 클라이언트 측에 출력시키겠다는 뜻이다.
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child */
}