#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_NUMBER 10
#define MAX_LRU 100

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *host_hdr = "Host: %s\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *request_hdr = "GET %s HTTP/1.0\r\n";
static const char *user_agent_macro = "User-Agent";
static const char *host_macro = "Host";
static const char *connection_macro = "Connection";
static const char *proxy_connection_macro = "Proxy-Connection";

void cacheInit();
void doit(int connfd);
int cacheFind(char *url);
void parseURI(char *uri, char *hostName, char *path, int *port);
void buildHttpHdr(char *httpHdr, char *hostName, char *path, int port, rio_t *clientRio);
void beforeRead(int i);
void afterRead(int i);
void beforeWrite(int i);
void afterWrite(int i);
int cacheEvict();
void cacheLRU(int index);
void cacheURI(char *uri, char *buf);

typedef struct {
	char obj[MAX_OBJECT_SIZE];
	char url[MAXLINE];
	int LRU;
	int isEmpty;
	int rCnt;
	sem_t rMutex;
	sem_t wMutex; 
} cacheBlock;

typedef struct {
	cacheBlock objs[MAX_CACHE_NUMBER]; 
	int num;
} cacheSet;

cacheSet cache;

void *thread(void *vargp) {
	int connfd = (int)vargp;
	Pthread_detach(pthread_self());
	doit(connfd);
	Close(connfd);
}

int main(int argc, char **argv)
{
	int listenfd, connfd, clientlen;
	struct sockaddr_storage clientaddr;	
	char hostName[MAXLINE], port[MAXLINE];
	pthread_t tid;
	
	cacheInit();
	
	Signal(SIGPIPE, SIG_IGN);
	
	listenfd = Open_listenfd(argv[1]);
	
	while(1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Pthread_create(&tid, NULL, thread, (void *)connfd);
	}
	
	return 0;
}

void cacheInit() {
	int i;
	cache.num = 0;
	
	for(i = 0; i < MAX_CACHE_NUMBER; i++) {
		cache.objs[i].LRU = 0;
		cache.objs[i].isEmpty = 1;
		cache.objs[i].rCnt = 0;
		Sem_init(&cache.objs[i].rMutex, 0, 1);
		Sem_init(&cache.objs[i].wMutex, 0, 1);
	}
}

void doit(int connfd) {
	int endServerfd;
	char buf[MAXLINE], function[MAXLINE], uri[MAXLINE], version[MAXLINE], endServerHttpHdr[MAXLINE], hostName[MAXLINE], path[MAXLINE], url[100], portStr[100];
	int port;
	int cacheIndex;
	rio_t clientRio, endServerRio;

	Rio_readinitb(&clientRio, connfd);
	Rio_readlineb(&clientRio, buf, MAXLINE);
	sscanf(buf, "%s %s %s", function, uri, version); 	
	strcpy(url, uri);

	cacheIndex = cacheFind(url);

	if(cacheIndex != -1) {
		beforeRead(cacheIndex);
		Rio_writen(connfd, cache.objs[cacheIndex].obj, strlen(cache.objs[cacheIndex].obj));
		afterRead(cacheIndex);
		return;
	}

	parseURI(uri, hostName, path, &port);
	buildHttpHdr(endServerHttpHdr, hostName, path, port, &clientRio);
	sprintf(portStr, "%d", port);
	endServerfd = Open_clientfd(hostName, portStr);

	if(endServerfd < 0) return;

	Rio_readinitb(&endServerRio, endServerfd);
	Rio_writen(endServerfd, endServerHttpHdr, strlen(endServerHttpHdr));


	char objBuf[MAX_OBJECT_SIZE];
	int bufSize = 0;
	size_t s;

	while((s = Rio_readlineb(&endServerRio, buf, MAXLINE)) != 0) {
		bufSize += s;
		if(bufSize < MAX_OBJECT_SIZE) strcat(objBuf, buf);
		Rio_writen(connfd, buf, s);
	}

	Close(endServerfd);
	if(bufSize < MAX_OBJECT_SIZE) cacheURI(url, objBuf);
}

int cacheFind(char *url) {
	int i;
	
	for(i = 0; i < MAX_CACHE_NUMBER; i++) {
		beforeRead(i);
		if((cache.objs[i].isEmpty == 0) && (strcmp(url, cache.objs[i].url) == 0)) break;
		afterRead(i);
	}
	
	if(i >= MAX_CACHE_NUMBER) return -1;
	
	return i;
}

void parseURI(char *uri, char *hostName, char *path, int *port) {
	char *p = strstr(uri, "//");
	
	if(p == NULL) p = uri;
	else p = p + 2;
	*port = 80;
	
	char *q = strstr(p, ":");
	
	if(q != NULL) {
		*q = '\0';
		sscanf(p, "%s", hostName);
		sscanf(q + 1, "%d%s", port, path);
	}
	else {
		q = strstr(p, "/");
		if(q != NULL) {
			*q = '\0';
			sscanf(p, "%s", hostName);
			*q = '/';
			sscanf(q, "%s", path);
		}
		else {
			sscanf(p, "%s", hostName);
		}
	}
}

void buildHttpHdr(char *httpHdr, char *hostName,char *path, int port, rio_t *clientRio) {
	char buf[MAXLINE], hostHdr[MAXLINE], requestHdr[MAXLINE], etcHdr[MAXLINE];
	
	sprintf(requestHdr, request_hdr, path);
	
	while(Rio_readlineb(clientRio, buf, MAXLINE) > 0) {
		if(strcmp(buf, "\r\n") == 0) break;
		if(!strncasecmp(buf, host_macro, strlen(host_macro))) {
			strcpy(hostHdr, buf);
			continue;
		}
		if(!strncasecmp(buf, connection_macro, strlen(connection_macro)) && !strncasecmp(buf, proxy_connection_macro, strlen(proxy_connection_macro)) && !strncasecmp(buf, user_agent_macro, strlen(user_agent_macro))) {
			strcat(etcHdr, buf);
		}
	}
	if(strlen(hostHdr) == 0) {
		sprintf(hostHdr, host_hdr, hostName);
	}

	sprintf(httpHdr, "%s%s%s%s%s%s\r\n", requestHdr, hostHdr, connection_hdr, proxy_connection_hdr, user_agent_hdr, etcHdr);
	
	return ;
}

void beforeRead(int i) {
	P(&cache.objs[i].rMutex);
	(cache.objs[i].rCnt)++;
	if(cache.objs[i].rCnt == 1)	P(&cache.objs[i].wMutex);
	V(&cache.objs[i].rMutex);
}

void afterRead(int i) {
	P(&cache.objs[i].rMutex);
	(cache.objs[i].rCnt)--;
	if(cache.objs[i].rCnt == 0) V(&cache.objs[i].wMutex);
	V(&cache.objs[i].rMutex);
}

void beforeWrite(int i) {
	P(&cache.objs[i].wMutex);
}

void afterWrite(int i) {
	V(&cache.objs[i].wMutex);
}

int cacheEvict() {
	int evictIndex = 0;
	int i;
	
	for(i = 0; i < MAX_CACHE_NUMBER; i++) {
		beforeRead(i);
		if(cache.objs[i].isEmpty == 1){
			evictIndex = i;
			afterRead(i);
			break;
		}		
		if(cache.objs[i].LRU < MAX_LRU) { 
			evictIndex = i;
			afterRead(i);
			continue;
		}		
		afterRead(i);
	}
	
	return evictIndex;
}

void cacheLRU(int index) {
	int i;
	
	for(i = 0; i < MAX_CACHE_NUMBER; i++) {
		if(i == index) continue;
		beforeWrite(i);
		if(cache.objs[i].isEmpty == 0 && i != index) {
			(cache.objs[i].LRU)--;
		}
		afterWrite(i);
	}
}

void cacheURI(char *uri, char *buf) {
	int i = cacheEvict();
	
	beforeWrite(i);
	strcpy(cache.objs[i].obj, buf);
	strcpy(cache.objs[i].url, uri);
	cache.objs[i].isEmpty = 0;
	cache.objs[i].LRU = MAX_LRU;
	cacheLRU(i);
	afterWrite(i);
}

