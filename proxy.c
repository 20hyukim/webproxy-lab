#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1024000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 9999
// Least Recently Used
// LRU: 가장 오랫동안 참조되지 않은 페이지를 교체하는 기법

#define CACHE_OBJS_COUNT 10

/* User agent header */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *requestline_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

void *thread(void *vargsp);
void doit(int connfd);
int parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

// Cache functions
void cache_init();
int cache_find(char *url);
void cache_uri(char *uri, char *buf);

void readerPre(int i);
void readerAfter(int i);
void writePre(int i);
void writeAfter(int i);

void cache_LRU(int index);
int cache_eviction();

typedef struct {
    char cache_obj[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];
    int LRU;  // Least recently used
    int isEmpty; // If block is empty

    int readCnt;  // Count of readers
    sem_t wmutex;  // Protects access to cache
    sem_t rdcntmutex;  // Protects access to readCnt
} cache_block;

typedef struct {
    cache_block cacheobjs[CACHE_OBJS_COUNT];  // Ten cache blocks
    int cache_num;
} Cache;

Cache cache;

int main(int argc, char **argv) {
    int listenfd, *connfdp;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;
    struct sockaddr_storage clientaddr;

    cache_init();

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);

        Pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}

void *thread(void *vargp) {
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int connfd) {
    int end_serverfd;

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int port;
  
    rio_t rio, server_rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method");
        return;
    }
  
    char url_store[100];
    strcpy(url_store, uri);

    int cache_index;
    if ((cache_index = cache_find(url_store)) != -1) {
        readerPre(cache_index);
        Rio_writen(connfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj));
        readerAfter(cache_index);
        return;
    }

    parse_uri(uri, hostname, path, &port);

    build_http_header(endserver_http_header, hostname, path, port, &rio);

    end_serverfd = connect_endServer(hostname, port, endserver_http_header);
    if (end_serverfd < 0) {
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio, end_serverfd);

    Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

    char cachebuf[MAX_OBJECT_SIZE];
    int sizebuf = 0;
    size_t n;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        sizebuf += n;
        if (sizebuf < MAX_OBJECT_SIZE)
            strcat(cachebuf, buf);
        Rio_writen(connfd, buf, n);
    }
    Close(end_serverfd);

    if (sizebuf < MAX_OBJECT_SIZE)
        cache_uri(url_store, cachebuf);
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
  
    sprintf(request_hdr, requestline_hdr_format, path);

    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, endof_hdr) == 0)
            break;
        
        if (!strncasecmp(buf, host_key, strlen(host_key))) {
            strcpy(host_hdr, buf);
            continue;
        }

        if (!strncasecmp(buf, connection_key, strlen(connection_key)) &&
            !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) &&
            !strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
            strcat(other_hdr, buf);
        }
    }
    if (strlen(host_hdr) == 0)
        sprintf(host_hdr, host_hdr_format, hostname);

    sprintf(http_header, "%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);
}

int connect_endServer(char *hostname, int port, char *http_header) {
    char portStr[100];
    sprintf(portStr, "%d", port);
    return Open_clientfd(hostname, portStr);
}

void cache_init() {
    cache.cache_num = 0;
    for (int i = 0; i < CACHE_OBJS_COUNT; i++) {
        cache.cacheobjs[i].LRU = 0;
        cache.cacheobjs[i].isEmpty = 1;
        cache.cacheobjs[i].readCnt = 0;
        Sem_init(&cache.cacheobjs[i].wmutex, 0, 1);
        Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1);
    }
}

void readerPre(int i) {
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt++;
    if (cache.cacheobjs[i].readCnt == 1)
        P(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

void readerAfter(int i) {
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt--;
    if (cache.cacheobjs[i].readCnt == 0)
        V(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

int cache_find(char *url) {
    for (int i = 0; i < CACHE_OBJS_COUNT; i++) {
        readerPre(i);
        if (cache.cacheobjs[i].isEmpty == 0 && strcmp(url, cache.cacheobjs[i].cache_url) == 0) {
            readerAfter(i);
            return i;
        }
        readerAfter(i);
    }
    return -1;
}

void writePre(int i) {
    P(&cache.cacheobjs[i].wmutex);
}

void writeAfter(int i) {
    V(&cache.cacheobjs[i].wmutex);
}

void cache_uri(char *uri, char *buf) {
    int i = cache_eviction();
    writePre(i);

    strcpy(cache.cacheobjs[i].cache_obj, buf);
    strcpy(cache.cacheobjs[i].cache_url, uri);
    cache.cacheobjs[i].isEmpty = 0;
    cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER;
    cache_LRU(i);

    writeAfter(i);
}

int cache_eviction() {
    int min = LRU_MAGIC_NUMBER, minindex = 0;
    for (int i = 0; i < CACHE_OBJS_COUNT; i++) {
        readerPre(i);
        if (cache.cacheobjs[i].isEmpty == 1) {
            minindex = i;
            readerAfter(i);
            break;
        }
        if (cache.cacheobjs[i].LRU < min) {
            min = cache.cacheobjs[i].LRU;
            minindex = i;
        }
        readerAfter(i);
    }
    return minindex;
}

void cache_LRU(int index) {
    for (int i = 0; i < CACHE_OBJS_COUNT; i++) {
        if (i == index) continue;
        writePre(i);
        if (cache.cacheobjs[i].isEmpty == 0) {
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
}

int parse_uri(char *uri, char *hostname, char *path, int *port) {
    *port = 80; // 기본 포트 설정

    // URI에서 "http://" 부분을 제거하고 시작 위치를 찾습니다.
    char *hostbegin = strstr(uri, "http://");
    if (hostbegin) {
        hostbegin += 7;
    } else {
        hostbegin = uri;
    }

    // 호스트 이름 뒤에 오는 경로를 구분하기 위해 '/' 위치를 찾습니다.
    char *pathbegin = strstr(hostbegin, "/");
    if (pathbegin) {
        strcpy(path, pathbegin);
        *pathbegin = '\0';  // 호스트 이름과 경로를 분리
    } else {
        strcpy(path, "/"); // 경로가 없으면 기본 경로로 설정
    }

    // 호스트 이름 뒤에 포트가 있는 경우 ":" 위치를 찾습니다.
    char *portpos = strstr(hostbegin, ":");
    if (portpos) {
        *portpos = '\0';
        sscanf(portpos + 1, "%d", port);  // 포트를 숫자로 파싱
    }

    // 남은 호스트 이름을 복사
    strcpy(hostname, hostbegin);
    return 0;
}