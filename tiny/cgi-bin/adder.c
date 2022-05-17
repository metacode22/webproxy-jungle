/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

/* 서버 프로세스가 만든 QUERY_STRING 환경 변수를 getenv로 가져와 buf에 넣는다.*/
// url 파라미터로, ? 뒤에 우리가 넣어주는 파라미터들을 의미한다.
// ex. www.naver.com/index.html?id=HTML&name=egoing에서 ? 뒤를 QUERY_STRING 환경 변수라고 한다.
// getenv, 환경 변수 목록 중에 원하는 변수값을 구한다.
int main() {
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;
  
  // QUERY_STRING에서 두 개의 인자를 추출한다.
  // 여기서 QUERY_STRING은 URI에서 클라이언트가 보낸 인자인 id=HTML&name=egoing 부분이다.
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');                                                               // buf 문자열에서 '&'를 가리키는 포인터를 반환한다.
    *p = '\0';                                                                          // buf 문자열에서 '&'를 '\0'으로 바꾼다.
    strcpy(arg1, buf);                                                                  // buf 문자열에서 \0 전까지의 문자열을 arg1에 넣는다.
    strcpy(arg2, p + 1);                                                                // buf 문자열에서 \0 뒤로의 문자열을 arg2에 넣는다.
    n1 = atoi(arg1);                                                                    // arg1에서 오로지 숫자만 뽑아서 int형으로 바꾼다.
    n2 = atoi(arg2);                                                                    // arg2에서 오로지 숫자만 뽑아서 int형으로 바꾼다.
  }
  
  // int sprinf(char* str, const char* format, ...) : str에 foramt을 저장한다. 출력할 값을 문자열에 저장하는 함수라고 생각하면 된다.
  // content라는 string에 응답 본체를 담는다.
  sprintf(content, "QUERY_STRING=%s", buf);                                             // content에 QUERY_STRING=buf를 저장한다. 덮어씌워지는 건데 왜 하지?
  sprintf(content, "Welcome to add.com: [Dynamic Content(adder.c)] ");
  sprintf(content, "%sTHE Internet addition portal. \r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);   // 인자를 처리해줬다. 동적으로 반응해줬다!
  sprintf(content, "%sThanks for visiting!\r\n", content);
  
  // 응답을 출력해주는 부분이다.
  // 헤더 부분
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));                               // content의 길이를 출력한다.
  printf("Content-type: text/html\r\n\r\n");
  
  // 응답 본체를 클라이언트에 출력한다.
  printf("%s", content);                                                                
  fflush(stdout);                                                                       // 출력 버퍼를 지운다
  
  exit(0);
}
/* $end adder */
