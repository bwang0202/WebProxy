/* 
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Bojun Wang, bw6@rice.edu
 * 
 */ 

#include "csapp.h"


static FILE *fromclient_file;
static FILE *toserver_file;
static FILE *logfile;
/*
 * Function prototypes
 */
int	parse_uri(char *uri, char *target_addr, char *path, int *port);
void	format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
	    char *uri, int size);
int open_listen(int port);
int doit(int connfd);
int open_clientfd_ts(char *hostname, int port, struct sockaddr_in *sockaddr);
void sigint_handler(int sig);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);


/* 
 * main - Main routine for the proxy program 
 */
int
main(int argc, char **argv)
{

	int listenfd, connfd, port;
	//int error;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    // char host_name[NI_MAXHOST];
    // char haddrp[INET_ADDRSTRLEN];
	
	/* Check the arguments. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}
	logfile = Fopen("proxy.log", "w");
    port = atoi(argv[1]);

	Signal(SIGPIPE, SIG_IGN);
    listenfd = open_listen(port);
    if (listenfd < 0) {
        unix_error("open_listen error");
    }

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        /* determine the domain name and IP address of the client */
        // error = getnameinfo((struct sockaddr *)&clientaddr, sizeof(clientaddr), 
           // host_name, sizeof(host_name), NULL, 0, 0);
        // if (error != 0) {
                // fprintf(stderr, "ERROR: %s\n", gai_strerror(error));
                // Close(connfd);
                // continue;
        // }
		//
		//inet_ntop(AF_INET, &clientaddr.sin_addr, haddrp, INET_ADDRSTRLEN);
		//printf("proxy connected to client %s (%s)\n", host_name, haddrp);
		printf("returned from accept\n");
        doit(connfd);
		printf("returned from doit\n");
        Close(connfd);
		
    }
	Close(listenfd);

	fclose(logfile);
    exit(0);
	return (0);
}

/*
 * read the request, parse open a connection to end server, forward the request, recieve reply forward reply to browser
 */
int doit(int connfd)
{
	char buf[MAXLINE], uri[MAXLINE], hostname[MAXLINE], pathname[MAXLINE], method[MAXLINE], version[MAXLINE], logstring[MAXLINE];
	int port;
	rio_t rio;
	rio_t rio2;
	int clientfd;
	char *tempstr;
	char *tempstrconnection;
	int len;
	char *fromclient_filename = "fromclient.txt";
	char *toserver_filename = "toserver.txt";
	struct sockaddr_in serveraddr;
	int response;
	char *connectionword = "Connection";
	char *connheadersign = ":";

	
	fromclient_file = Fopen(fromclient_filename, "w");
	toserver_file = Fopen(toserver_filename, "w");
	tempstr = malloc(sizeof(char)*MAXLINE);
	tempstrconnection = malloc(sizeof(char)*MAXLINE);
	connheadersign = connheadersign;
	
	/* Read request line */
	Rio_readinitb(&rio, connfd);
	printf("got here 1\n");
	Rio_readlineb_w(&rio, buf, MAXLINE); //first line of http request, should look like GEt, uri, version
	fputs(buf, fromclient_file);
	printf("got a line : %s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);
	printf("got here 2\n");
	if (parse_uri(uri, hostname, pathname, &port) < 0){
		printf("error in parse_uri \n");
		return (-1);
	}
	if (strcmp(method, "GET")) {
		printf("only support GET for now\n");
		return (-1);
	}
	/* open a connection to end server*/
	/* open a connection to end server*/
	if ((clientfd = open_clientfd_ts(hostname, port, &serveraddr)) < 0) {
		printf("error when connecting to end server\n");
		return (-1);
	}
	/*substitute the whole buf with pathname*//////////////////////////////////////////
	printf("got here 3\n");
	*tempstr = '\0';
	strcat(tempstr, "GET ");
	strcat(tempstr, pathname);
	if(strstr(buf, "HTTP/1.1") != NULL){
		strcat(tempstr, " HTTP/1.1\r\n");
	} else {
		strcat(tempstr, " HTTP/1.0\r\n");
	}
	Rio_writen_w(clientfd, tempstr, strlen(tempstr));
	fputs(tempstr, toserver_file);
	free(tempstr);
	*tempstrconnection = '\0';
	strcat(tempstrconnection, "Connection: close\r\n");
	while(strcmp(buf, "\r\n")){
		Rio_readlineb_w(&rio, buf, MAXLINE); //keeps reading in lines of http request
		fputs(buf, fromclient_file);
		/* If buf contains connection, write connection close to server*/
		if (strstr(buf, connectionword) != NULL){
			Rio_writen_w(clientfd, tempstrconnection, strlen(tempstrconnection)); //write Connection: close to server
			fputs(tempstrconnection, toserver_file);
		}else{
			Rio_writen_w(clientfd, buf, strlen(buf)); //writes whatever in the read buf to clientfd
			fputs(buf, toserver_file);
		}
	}
	free(tempstrconnection);
	fflush(fromclient_file);
	fflush(toserver_file);
	fclose(fromclient_file);
	fclose(toserver_file);
	printf("done writing to server, now waiting for response...\n");
	Rio_readinitb(&rio2, clientfd);
	response = 0;
	len = Rio_readlineb_w(&rio2, buf, MAXLINE); //read from end server
	response += len;
	//fputs(buf, toserver_file);
	Rio_writen_w(connfd, buf, strlen(buf));
	//fputs(buf, fromclient_file);
	while(len > 0){
		len = rio_readlineb(&rio2, buf, MAXLINE);
		
		if(len > 0){
			response += len;
			Rio_writen_w(connfd, buf, len);
			//fputs(buf, fromclient_file);
		}else{
			break;
		}
	}
	//zai connfd jia eof?
	format_log_entry(logstring, &serveraddr, uri, response);
	fputs(logstring, logfile);
	fputs("\n", logfile);
	fflush(logfile);

	
	Close(clientfd);
	return (0);
}

/*
 * open_clientfd_ts - open connection to server at <hostname, port> 
 *   and return a socket descriptor ready for reading and writing.
 *   Fills in the struct pointed by ptr with a sockaddr_in data.
 *   Returns -1 and sets errno on Unix error. 
 *   Returns -2 and sets h_errno on DNS (gethostbyname) error.
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
        freeaddrinfo(ai);
        return -1; 
    }
    
    /* Fill in the server's IP address and port */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy(ai->ai_addr, 
      (struct sockaddr *)&serveraddr, ai->ai_addrlen);
    serveraddr.sin_port = htons(port);
    
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    
    freeaddrinfo(ai);
	bcopy(&serveraddr, ptr, sizeof(struct sockaddr_in));
    return clientfd;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
	ssize_t n;
	if((n = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
		printf(strerror(errno));
		return (0);
	} else {
		return n;
	}
}
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n)
{
	ssize_t m;
	if((m = rio_writen(fd, usrbuf, n)) < 0){
		printf(strerror(errno));
		return (0);
	} else {
		return m;
	}
}


/*  
 * open_listenfd - open and return a listening socket on port
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
