/* 
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Bojun Wang, bw6@rice.edu
 * 
 */ 

#include "csapp.h"

#define NITEMS 10

static FILE *logfile;       /* Log file handler*/
int shared_buffer[NITEMS];  /* buffer for requests connected file descriptors*/
int shared_cnt;				/* Item count. */
pthread_mutex_t mutex;		/* pthread mutex. */ 
pthread_cond_t cond_empty;	/* pthread cond variable to wait on empty buffer. */
pthread_cond_t cond_full;	/* pthread cond variable to wait on full buffer. */
unsigned int prod_index = 0; /* Producer index into shared buffer. */ 
unsigned int cons_index = 0; /* Consumer index into shared buffer. */
/*
 * Function prototypes
 */
int	parse_uri(char *uri, char *target_addr, char *path, int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
	    char *uri, int size);
int open_listen(int port);
void* doit(void *argp);
int open_clientfd_ts(char *hostname, int port, struct sockaddr_in *sockaddr);
void sigint_handler(int sig);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);


/* 
 * main - Main routine for the proxy program 
 * Requires: Nothing.
 * 
 * Effects: 
 *          It first open a listening socket for clients to connect to it.
 * Then it stores all connected file descriptor returned by accept into a buffer.
 * It acts as a producer and the threads it creates and detaches act like a
 * consumer. Accept blocks until the buffer is not full so that the returned fd
 * could be put into the buffer. It also opens and closes log file handler.
 *
 */
int
main(int argc, char **argv)
{
	pthread_t cons_tid1, cons_tid2, cons_tid3, cons_tid4, cons_tid5, cons_tid6;
	int listenfd, port;
	int* connfd;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
	
	/* Check the arguments. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}
	
	/* Initiate mutex and condition variables*/
	Pthread_mutex_init(&mutex, NULL);
	Pthread_cond_init(&cond_full, NULL);
	Pthread_cond_init(&cond_empty, NULL);
	
	logfile = Fopen("proxy.log", "w");
    port = atoi(argv[1]);
	/* Ignore all SIGPIPE*/
	Signal(SIGPIPE, SIG_IGN);
    listenfd = open_listen(port);
    if (listenfd < 0) {
        unix_error("open_listen error");
    }
	
	/*create a pool of threads and detach all*/
	Pthread_create(&cons_tid1, NULL, doit, NULL);
	Pthread_create(&cons_tid2, NULL, doit, NULL);
	Pthread_create(&cons_tid3, NULL, doit, NULL);
	Pthread_create(&cons_tid4, NULL, doit, NULL);
	Pthread_create(&cons_tid5, NULL, doit, NULL);
	Pthread_create(&cons_tid6, NULL, doit, NULL);
	Pthread_detach(cons_tid1);
	Pthread_detach(cons_tid2);
	Pthread_detach(cons_tid3);
	Pthread_detach(cons_tid4);
	Pthread_detach(cons_tid5);
	Pthread_detach(cons_tid6);
	/* producer loop*/
    while (1) { 
        clientlen = sizeof(clientaddr);
		connfd = malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		/* Acquire mutex lock. */
		Pthread_mutex_lock(&mutex);
		while (shared_cnt == NITEMS) {
			/* If buffer is full, wait until signalled. */
			Pthread_cond_wait(&cond_full, &mutex);
		}
		/* Store key in shared buffer. */
		shared_buffer[prod_index] = *connfd;
		free(connfd);
		/* Signal only if shared buffer was empty. */
		if (shared_cnt == 0)
			Pthread_cond_broadcast(&cond_empty);

		/* Update shared count variable. */
		shared_cnt++;

		/* Update producer index. */
		if (prod_index == NITEMS - 1)
			prod_index = 0;
		else
			prod_index++;
		
		/* Release mutex lock. */
		Pthread_mutex_unlock(&mutex); 
		
    }
	Close(listenfd);

	Fclose(logfile);
    exit(0);
	return (0);
}

/*
 *  Requires: 
 *          Log file is already open.
 *
 *  Effects:
 *          This is thread routine for handling requests. 
 *  It retrieves one connected file descriptor when the fd buffer is not 
 *  empty, and starts to read lines from the connected fd, parse the first 
 *  line, if it's not a GET, close the connection. Then open a client fd to
 *  the host in uri. Then read headers from the connfd and pass headers to 
 *  clientfd, the end server. If the header contains Connection it's replaced
 *  by Connection: close. Then read lines from end server and pass it back to
 *  client until EOF or got error during read/write sockets. Then close the 
 *  connection and get a new one from buffer. Before closing connections, it
 *  writes log entry for each successful response to log file.
 */
void* doit(void *argp)
{
	char buf[MAXLINE], uri[MAXLINE], hostname[MAXLINE], pathname[MAXLINE],
	method[MAXLINE], version[MAXLINE], logstring[MAXLINE];
	int port;
	rio_t rio, rio2;
	int clientfd = -1;
	char *tempstr, *tempstrconnection;
	int len, response;
	struct sockaddr_in serveraddr;
	char *connectionword = "Connection";
	int connfd;
	
	/* Get rid of unused warning. */
	argp = argp;

	while(1){
		while(1){
		
			/*first get a connfd from buffer*/
			Pthread_mutex_lock(&mutex);

			/* If buffer is empty, wait until something is added. */
			while (shared_cnt == 0) {
				Pthread_cond_wait(&cond_empty, &mutex);
			}
			connfd = shared_buffer[cons_index];
			
			/* Signal only if buffer was full. */
			if (shared_cnt == NITEMS)
				Pthread_cond_signal(&cond_full);

			/* Update shared count variable. */
			shared_cnt--;

			/* Update consumer index. */
			if (cons_index == NITEMS - 1)
				cons_index = 0;
			else
				cons_index++;
		
			/* Release mutex lock. */
			Pthread_mutex_unlock(&mutex);
			
			tempstr = Malloc(sizeof(char)*MAXLINE);
			tempstrconnection = Malloc(sizeof(char)*MAXLINE);
			
			/* Read request line */
			Rio_readinitb(&rio, connfd);
			Rio_readlineb_w(&rio, buf, MAXLINE); //first line of http request, should look like GEt, uri, version
			sscanf(buf, "%s %s %s", method, uri, version);
			if (parse_uri(uri, hostname, pathname, &port) < 0){
				printf("error in parse_uri \n");
				Free(tempstr);
				Free(tempstrconnection);
				Close(connfd);
				break;
			}
			if (strcmp(method, "GET")) {
				printf("only support GET for now\n");
				Free(tempstr);
				Free(tempstrconnection);
				Close(connfd);
				break;
			}
			/* open a connection to end server*/
			if ((clientfd = open_clientfd_ts(hostname, port, &serveraddr)) < 0) {
				printf("error when connecting to end server\n");
				if (clientfd > 0) {
					Close(clientfd);
				}
				Free(tempstr);
				Free(tempstrconnection);
				Close(connfd);
				break;
			}
			/* Build a new GET http request line and write it to end server*/
			*tempstr = '\0';
			strcat(tempstr, "GET ");
			strcat(tempstr, pathname);
			if(strstr(buf, "HTTP/1.1") != NULL){
				strcat(tempstr, " HTTP/1.1\r\n");
			} else {
				strcat(tempstr, " HTTP/1.0\r\n");
			}
			Rio_writen_w(clientfd, tempstr, strlen(tempstr));
			Free(tempstr);
			*tempstrconnection = '\0';
			strcat(tempstrconnection, "Connection: close\r\n");
			while(strcmp(buf, "\r\n")){
				Rio_readlineb_w(&rio, buf, MAXLINE);
				/* If buf contains connection, write Connection: close to server*/
				if (strstr(buf, connectionword) != NULL){
					Rio_writen_w(clientfd, tempstrconnection, strlen(tempstrconnection));
				}else{
					Rio_writen_w(clientfd, buf, strlen(buf));
				}
			}
			Free(tempstrconnection);
			/* Start to read from server and pass response back to client. */
			Rio_readinitb(&rio2, clientfd);
			response = 0;
			len = Rio_readlineb_w(&rio2, buf, MAXLINE); 
			response += len;
			Rio_writen_w(connfd, buf, strlen(buf));
			while(len > 0){
				len = rio_readlineb(&rio2, buf, MAXLINE);
				/* On eof, just break while loop. */
				if(len > 0){
					response += len;
					Rio_writen_w(connfd, buf, len);
				}else{
					break;
				}
			}
			/* format a log entry and write it to logfile. */
			format_log_entry(logstring, &serveraddr, uri, response);
			Pthread_mutex_lock(&mutex);
			fputs(logstring, logfile);
			fputs("\n", logfile);
			fflush(logfile);
			Pthread_mutex_unlock(&mutex);
			
			Close(clientfd);
			Close(connfd);
			
		}
	}
	return NULL;
}

/*
 * Requires: 
 *         ptr must points to an allocated struct of sockaddr_in.
 * Effects:
 * 		open_clientfd_ts - open connection to server at <hostname, port>
 *   and return a socket descriptor ready for reading and writing.
 *   Fills in the struct pointed by ptr with a sockaddr_in data.
 *   Returns -1 on getaddrinfo error and connect error.
 */
int open_clientfd_ts(char *hostname, int port, struct sockaddr_in *ptr) 
{
    int clientfd, error;
    struct addrinfo *ai;
    struct sockaddr_in serveraddr;
    
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* check errno for cause of error */
    
    error = getaddrinfo(hostname, NULL, NULL, &ai);
    
    if (error != 0) {
        /* check gai_strerr for cause of error */
        fprintf(stderr, "ERROR: %s", gai_strerror(error));
        return -1; 
    }
    
    /* Fill in the server's IP address and port */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy(ai->ai_addr, 
      (struct sockaddr *)&serveraddr, ai->ai_addrlen);
    serveraddr.sin_port = htons(port);
    
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0){
        freeaddrinfo(ai);
		return -1;
	}
    
    freeaddrinfo(ai);
	bcopy(&serveraddr, ptr, sizeof(struct sockaddr_in));
    return clientfd;
}

/*
 * Requires: 
 *     rp points to an rio_t structure.
 * Effects:
 *     read maxlen bytes from rp and put them in usrbuf.
 *     If there is an error when rio_readlineb, return 0 and print error msg.
 *     Otherwise, return the bytes read.
 *
 */
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
	ssize_t n;
	if((n = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
		printf("rio readlineb error %s\n", strerror(errno));
		return (0);
	} else {
		return n;
	}
}

/*
 * Requires: 
 *     fd is an open sockets.
 * Effects:
 *     write bytes to fd from usrbuf.
 *     Return 0 on error or the number of bytes written.
 *
 */
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n)
{
	ssize_t m;
	if((m = rio_writen(fd, usrbuf, n)) < 0){
		//printf("rio writen error %s\n", strerror(errno));
		return (0);
	} else {
		return m;
	}
}


/*  
 * Requires: Nothing
 *
 * Effects: 
 *     open and return a listening socket on port
 *     Returns -1 and sets errno on Unix error.
 */
int open_listen(int port) 
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) 
	return -1;
 
    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval , sizeof(int)) < 0) 
	return -1;

    /* Listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0) 
		return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0) 
        return -1;

    return listenfd;
}

/*
 * parse_uri - URI parser
 * 
 * Requires: 
 *   The memory for hostname and pathname must already be allocated
 *   and should be at least MAXLINE bytes.  Port must point to a
 *   single integer that has already been allocated.
 *
 * Effects:
 *   Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 *   the host name, path name, and port.  Return -1 if there are any
 *   problems and 0 otherwise.
 */
int 
parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
	char *hostbegin;
	char *hostend;
	int len, i, j;
	
	if (strncasecmp(uri, "http://", 7) != 0) {
		hostname[0] = '\0';
		return (-1);
	}
	   
	/* Extract the host name. */
	hostbegin = uri + 7;
	hostend = strpbrk(hostbegin, " :/\r\n");
	if (hostend == NULL)
		hostend = hostbegin + strlen(hostbegin);
	len = hostend - hostbegin;
	strncpy(hostname, hostbegin, len);
	hostname[len] = '\0';
	
	/* Look for a port number.  If none is found, use port 80. */
	*port = 80;
	if (*hostend == ':')
		*port = atoi(hostend + 1);
	
	/* Extract the path. */
	for (i = 0; hostbegin[i] != '/'; i++) {
		if (hostbegin[i] == ' ') 
			break;
	}
	if (hostbegin[i] == ' ')
		strcpy(pathname, "/");
	else {
		for (j = 0; hostbegin[i] != ' '; j++, i++) 
			pathname[j] = hostbegin[i];
		pathname[j] = '\0';
	}

	return (0);
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 *
 * Requires:
 *   The memory for logstring must already be allocated and should be
 *   at least MAXLINE bytes.  Sockaddr must point to an allocated
 *   sockaddr_in structure.  Uri must point to a properly terminated
 *   string.
 *
 * Effects:
 *   A properly formatted log entry is stored in logstring using the
 *   socket address of the requesting client (sockaddr), the URI from
 *   the request (uri), and the size in bytes of the response from the
 *   server (size).
 */
void
format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri,
    int size)
{
	time_t now;
	char time_str[MAXLINE];
	unsigned long host;
	unsigned char a, b, c, d;

	/* Get a formatted time string. */
	now = time(NULL);
	strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z",
	    localtime(&now));

	/*
	 * Convert the IP address in network byte order to dotted decimal
	 * form.  Note that we could have used inet_ntoa, but chose not to
	 * because inet_ntoa is a Class 3 thread unsafe function that
	 * returns a pointer to a static variable (Ch 13, CS:APP).
	 */
	host = ntohl(sockaddr->sin_addr.s_addr);
	a = host >> 24;
	b = (host >> 16) & 0xff;
	c = (host >> 8) & 0xff;
	d = host & 0xff;

	/* Return the formatted log entry string */
	sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri,
	    size);
}

/*
 * The last lines of this file configures the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
