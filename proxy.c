#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CacheNode {
  char url[MAXLINE];
  char data[MAX_OBJECT_SIZE];
  int size;
  struct CacheNode *prev;
  struct CacheNode *next;
} CacheNode;

typedef struct {
  CacheNode *head;
  CacheNode *tail;
  int total_size;
  pthread_rwlock_t lock;
} CacheList;

CacheList cache;

/* 사용자 에이전트 헤더 상수 */
static const char *user_agent_hdr = 
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void *thread(void *vargp);
void doit(int connfd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, char *path, int *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void forward_request(int clientfd, char *hostname, char *path, int port, char *uri);
void cache_init();
void cache_store(char *url, char *data, int size);
CacheNode* cache_find(char *url);
void cache_evict();
void move_to_front(CacheNode *node);
void normalize_url(char *url, char *normalized_url);
void print_cache_state();


/* main 함수: 프록시 서버 시작 */
int main(int argc, char **argv) {
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    cache_init();

    /* 명령줄 인자 확인 */
    if (argc != 2) {
        FILE *log_file = fopen("proxy_debug_log.txt", "a");
        fprintf(log_file, "usage: %s <port>\n", argv[0]);
        fclose(log_file);
        exit(1);
    }

    /* 듣기 소켓을 열고 클라이언트 요청을 순차적으로 처리 */
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}

void print_cache_state() {
    FILE *log_file = fopen("proxy_debug_log.txt", "a");
    fprintf(log_file, "Current cache state:\n");
    CacheNode *current = cache.head;
    while (current != NULL) {
        fprintf(log_file, "URL: %s, Size: %d\n", current->url, current->size);
        current = current->next;
    }
    fprintf(log_file, "Total cache size: %d\n", cache.total_size);
    fclose(log_file);
}

void cache_init() {
  cache.head = NULL;
  cache.tail = NULL;
  cache.total_size = 0;
  pthread_rwlock_init(&cache.lock, NULL);
}

void normalize_url(char *url, char *normalized_url) {
  char *path_start = strstr(url, "http://");
  if (path_start) {
    path_start += 7;
  } else {
    path_start = url;
  }

  path_start = strchr(path_start, '/');
  if (path_start)  {
    strcpy(normalized_url, path_start);
  } else {
    strcpy(normalized_url, "/");
  }
}

CacheNode* cache_find(char *url) {
    char normalized_url[MAXLINE];
    normalize_url(url, normalized_url);

    FILE *log_file = fopen("proxy_debug_log.txt", "a");
    fprintf(log_file, "Searching cache for URL: %s\n", normalized_url);
    pthread_rwlock_rdlock(&cache.lock);
    CacheNode *current = cache.head;
    while (current != NULL) {
        if (strcmp(current->url, normalized_url) == 0) {
            fprintf(log_file, "Cache hit for URL: %s\n", normalized_url);
            move_to_front(current);
            pthread_rwlock_unlock(&cache.lock);
            fclose(log_file);
            print_cache_state();
            return current;
        }
        current = current->next;
    }
    fprintf(log_file, "Cache miss for URL: %s\n", normalized_url);
    fclose(log_file);
    pthread_rwlock_unlock(&cache.lock);
    print_cache_state();
    return NULL;
}

void cache_store(char *url, char *data, int size) {
    char normalized_url[MAXLINE];
    normalize_url(url, normalized_url);

    FILE *log_file = fopen("proxy_debug_log.txt", "a");
    if (size > MAX_OBJECT_SIZE) {
        fprintf(log_file, "Object size exceeds MAX_OBJECT_SIZE. Not caching URL: %s\n", normalized_url);
        fclose(log_file);
        print_cache_state();
        return;
    }

    pthread_rwlock_wrlock(&cache.lock);
    fprintf(stderr, "Storing URL: %s with size: %d in cache\n", normalized_url, size);

    while (cache.total_size + size > MAX_CACHE_SIZE) {
        cache_evict();
    }

    CacheNode *new_node = Malloc(sizeof(CacheNode));
    strcpy(new_node->url, normalized_url);
    memcpy(new_node->data, data, size);
    new_node->size = size;
    new_node->prev = NULL;
    new_node->next = cache.head;

    if (cache.head != NULL) {
        cache.head->prev = new_node;
    }
    cache.head = new_node;
    if (cache.tail == NULL) {
        cache.tail = new_node;
    }
    cache.total_size += size;

    pthread_rwlock_unlock(&cache.lock);
    fprintf(log_file, "Cache total size after storing: %d\n", cache.total_size);
    fclose(log_file);
    print_cache_state();
}

void cache_evict() {
  if (cache.head == NULL) {
    return;
  }

  CacheNode *node = cache.tail;

  if (node -> prev != NULL) {
    node -> prev -> next = NULL;
  } else {
    cache.head = NULL;
  }
  cache.tail = node -> prev;

  cache.total_size -= node -> size;
  Free(node);
}

void move_to_front(CacheNode *node) {
    if (node == cache.head) {
        return;
    }

    if (node->prev != NULL) {
        node->prev->next = node->next;
    }
    if (node->next != NULL) {
        node->next->prev = node->prev;
    }
    if (node == cache.tail) {
        cache.tail = node->prev;
    }

    node->next = cache.head;
    node->prev = NULL;
    if (cache.head != NULL) {
        cache.head->prev = node;
    }
    cache.head = node;
    if (cache.tail == NULL) {
        cache.tail = node;
    }
}

void *thread(void *vargp) {
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
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

    CacheNode *cache_node = cache_find(uri);
    if (cache_node != NULL) {
      Rio_writen(clientfd, cache_node -> data, cache_node -> size);
      return;
    }

    /* URI 파싱 */
    if (parse_uri(uri, hostname, path, &port) < 0) {
        clienterror(clientfd, uri, "400", "Bad Request", "Invalid URI");
        return;
    }

    /* Tiny 서버에 요청 전달 */
    forward_request(clientfd, hostname, path, port, uri);
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
void forward_request(int clientfd, char *hostname, char *path, int port, char *uri) {
    int serverfd;
    char buf[MAXLINE], cache_buf[MAX_OBJECT_SIZE];
    rio_t server_rio;
    int total_size = 0;

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
        if (total_size + n < MAX_OBJECT_SIZE) {
          memcpy(cache_buf + total_size, buf, n);
        }
        total_size += n;
    }

    if (total_size <= MAX_OBJECT_SIZE) {
      cache_store(uri, cache_buf, total_size);
    }

    /* Tiny 서버와의 연결 종료 */
    Close(serverfd);
}