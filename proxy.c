#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 사용자 에이전트 헤더 상수 */
static const char *user_agent_hdr = 
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int connfd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, char *path, int *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void forward_request(int clientfd, char *hostname, char *path, int port);

/* main 함수: 프록시 서버 시작 */
int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];

    /* 명령줄 인자 확인 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* 듣기 소켓을 열고 클라이언트 요청을 순차적으로 처리 */
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        
        /* 클라이언트 요청 처리 */
        doit(connfd);
        Close(connfd);
    }

    return 0;
}

/* 클라이언트 요청을 처리하는 함수 */
void doit(int clientfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t rio;
    char hostname[MAXLINE], path[MAXLINE];
    int port;

    /* 요청 라인과 헤더 읽기 */
    Rio_readinitb(&rio, clientfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    /* GET 메서드만 지원 */
    if (strcasecmp(method, "GET")) {
        clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    /* 요청 헤더 읽기 */
    read_requesthdrs(&rio);

    /* URI 파싱 */
    if (parse_uri(uri, hostname, path, &port) < 0) {
        clienterror(clientfd, uri, "400", "Bad Request", "Invalid URI");
        return;
    }

    /* Tiny 서버에 요청 전달 */
    forward_request(clientfd, hostname, path, port);
}

/* 요청 헤더를 읽고 무시하는 함수 */
void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
    }
}

/* URI를 호스트 이름, 경로, 포트로 파싱하는 함수 */
int parse_uri(char *uri, char *hostname, char *path, int *port) {
    *port = 80; // 기본 포트 설정
    char *hostbegin = strstr(uri, "http://");
    if (hostbegin) {
        hostbegin += 7;
    } else {
        hostbegin = uri;
    }
    
    char *pathbegin = strstr(hostbegin, "/");
    if (pathbegin) {
        strcpy(path, pathbegin);
        *pathbegin = '\0';
    } else {
        strcpy(path, "/");
    }
    
    char *portpos = strstr(hostbegin, ":");
    if (portpos) {
        *portpos = '\0';
        sscanf(portpos + 1, "%d", port);
    }
    
    strcpy(hostname, hostbegin);
    return 0;
}

/* 클라이언트에게 오류 메시지 전송 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Server</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

/* Tiny 서버에 요청을 전달하고 응답을 클라이언트로 전달 */
void forward_request(int clientfd, char *hostname, char *path, int port) {
    int serverfd;
    char buf[MAXLINE];
    rio_t server_rio;

    /* Tiny 서버와의 연결 설정 */
    char port_str[10];
    sprintf(port_str, "%d", port);
    serverfd = Open_clientfd(hostname, port_str);
    if (serverfd < 0) {
        clienterror(clientfd, hostname, "404", "Not Found", "Failed to connect to server");
        return;
    }

    /* Tiny 서버로 요청 전달 */
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, buf, strlen(buf));
    sprintf(buf, "Host: %s\r\n", hostname);
    Rio_writen(serverfd, buf, strlen(buf));
    sprintf(buf, "%s", user_agent_hdr);
    Rio_writen(serverfd, buf, strlen(buf));
    sprintf(buf, "Connection: close\r\n");
    Rio_writen(serverfd, buf, strlen(buf));
    sprintf(buf, "Proxy-Connection: close\r\n\r\n");
    Rio_writen(serverfd, buf, strlen(buf));

    /* Tiny 서버 응답을 읽고 클라이언트에게 전달 */
    Rio_readinitb(&server_rio, serverfd);
    size_t n;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        Rio_writen(clientfd, buf, n);
    }

    /* Tiny 서버와의 연결 종료 */
    Close(serverfd);
}