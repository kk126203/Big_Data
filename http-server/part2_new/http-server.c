/*
 * http-server.c
 */

#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), bind(), and connect() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <time.h>       /* for time() */
#include <netdb.h>      /* for gethostbyname() */
#include <signal.h>     /* for signal() */
#include <sys/stat.h>   /* for stat() */
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/mman.h>

#define MAXPENDING 5    /* Maximum outstanding connection requests */

#define DISK_IO_BUF_SIZE 4096

struct table{
    int total;
    int five;
    int two;
    int three;
    int four;    
    sem_t sem;
};
struct table *t1;

typedef void sigfunc(int);
static sigfunc *
my_signal(int signo, sigfunc *func){

    struct sigaction act, oact;
    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if(signo==SIGALRM){
#ifdef SA_INTERRUPT
	act.sa_flags |= SA_INTERRUPT;
#endif 
    }else{
    	act.sa_flags |= SA_RESTART;
    }
    if(sigaction(signo, &act, &oact)<0)
	    return(SIG_ERR);
    return (oact.sa_handler);
}
int servSock;

static void int_handler(int signo){
    close(servSock);
    printf("Hi, let's stop~\n");
    exit(1);
}

static void clean(int signo){
    pid_t child;
    while((child = waitpid(-1, NULL, WNOHANG))>0){
    	printf("clean 1 zombie\n");
    }
}

static void die(const char *message)
{
    perror(message);
    exit(1); 
}

static void update(int status){
    sem_wait(&t1->sem);

    t1->total+=1;
    if(status/100==2)    t1->two+=1;
    else if(status/100==3)    t1->three+=1;
    else if(status/100==4)    t1->four+=1;
    else if(status/100==5)    t1->five+=1;
    else exit(1);
    sem_post(&t1->sem);

}

/*
 * Create a listening socket bound to the given port.
 */
static int createServerSocket(unsigned short port)
{
    int servSock;
    struct sockaddr_in servAddr;

    /* Create socket for incoming connections */
    if ((servSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        die("socket() failed");
      
    /* Construct local address structure */
    memset(&servAddr, 0, sizeof(servAddr));       /* Zero out structure */
    servAddr.sin_family = AF_INET;                /* Internet address family */
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    servAddr.sin_port = htons(port);              /* Local port */

    /* Bind to the local address */
    if (bind(servSock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
        die("bind() failed");

    /* Mark the socket so it will listen for incoming connections */
    if (listen(servSock, MAXPENDING) < 0)
        die("listen() failed");

    return servSock;
}

/*
 * A wrapper around send() that does error checking and logging.
 * Returns -1 on failure.
 * 
 * This function assumes that buf is a null-terminated string, so
 * don't use this function to send binary data.
 */
ssize_t Send(int sock, const char *buf)
{
    size_t len = strlen(buf);
    ssize_t res = send(sock, buf, len, 0);
    if (res != len) {
        perror("send() failed");
        return -1;
    }
    else 
        return res;
}

/*
 * HTTP/1.0 status codes and the corresponding reason phrases.
 */

static struct {
    int status;
    char *reason;
} HTTP_StatusCodes[] = {
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 204, "No Content" },
    { 301, "Moved Permanently" },
    { 302, "Moved Temporarily" },
    { 304, "Not Modified" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 0, NULL } // marks the end of the list
};

static inline const char *getReasonPhrase(int statusCode)
{
    int i = 0;
    while (HTTP_StatusCodes[i].status > 0) {
        if (HTTP_StatusCodes[i].status == statusCode)
            return HTTP_StatusCodes[i].reason;
        i++;
    }
    return "Unknown Status Code";
}


/*
 * Send HTTP status line followed by a blank line.
 */
static void sendStatusLine(int clntSock, int statusCode)
{
    char buf[1000];
    const char *reasonPhrase = getReasonPhrase(statusCode);

    // print the status line into the buffer
    sprintf(buf, "HTTP/1.0 %d ", statusCode);
    strcat(buf, reasonPhrase);
    strcat(buf, "\r\n");

    // We don't send any HTTP header in this simple server.
    // We need to send a blank line to signal the end of headers.
    strcat(buf, "\r\n");

    // For non-200 status, format the status line as an HTML content
    // so that browers can display it.
    if (statusCode != 200) {
        char body[1000];
        sprintf(body, 
                "<html><body>\n"
                "<h1>%d %s</h1>\n"
                "</body></html>\n",
                statusCode, reasonPhrase);
        strcat(buf, body);
    }

    // send the buffer to the browser
    Send(clntSock, buf);
}

static void sendStatistics(int clntSock)
{
    printf("here1\n");
    char buf[1000];
    int code = 200;
    const char *reasonPhrase = getReasonPhrase(code);
    sprintf(buf, "HTTP/1.0 %d ", code);
    strcat(buf, reasonPhrase);
    strcat(buf, "\r\n");
    strcat(buf, "\r\n");
    char body[1000];
    sprintf(body, 
            "<html><body>\n"
            "<h1>Server Statistics</h1><br>\n"
	    "<pre>Request: %d</pre>\n"
	    "<pre>2xx:    %d</pre>\n"
	    "<pre>3xx:    %d</pre>\n"
	    "<pre>4xx:    %d</pre>\n"
	    "<pre>5xx:    %d</pre>\n"
            "</body></html>\n",
            t1->total, t1->two, t1->three, t1->four, t1->five);
    strcat(buf, body);
    printf("here2\n");
    // send the buffer to the browser
    Send(clntSock, buf);
}

/*
 * Handle static file requests.
 * Returns the HTTP status code that was sent to the browser.
 */
static int handleFileRequest(
        const char *webRoot, const char *requestURI, int clntSock)
{
    int statusCode;
    FILE *fp = NULL;

    // Compose the file path from webRoot and requestURI.
    // If requestURI ends with '/', append "index.html".
    
    char *file = (char *)malloc(strlen(webRoot) + strlen(requestURI) + 100);
    if (file == NULL)
        die("malloc failed");
    strcpy(file, webRoot);
    strcat(file, requestURI);
    if (file[strlen(file)-1] == '/') {
        strcat(file, "index.html");
    }

    // See if the requested file is a directory.
    // Our server does not support directory listing.

    struct stat st;
    if (stat(file, &st) == 0 && S_ISDIR(st.st_mode)) {
        statusCode = 403; // "Forbidden"
	update(statusCode);
        sendStatusLine(clntSock, statusCode);
        goto func_end;
    }

    
    if(strcmp(requestURI, "/statistics")==0){
	update(200);
    	sendStatistics(clntSock);
	goto func_end;
    }else{
    	fp = fopen(file, "rb");
    	if (fp == NULL) {
       		statusCode = 404; // "Not Found"
		update(statusCode);
       		sendStatusLine(clntSock, statusCode);
        	goto func_end;
    	}
    }
    // Otherwise, send "200 OK" followed by the file content.

    statusCode = 200; // "OK"
    update(statusCode);
    sendStatusLine(clntSock, statusCode);

    // send the file 
    size_t n;
    char buf[DISK_IO_BUF_SIZE];
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (send(clntSock, buf, n, 0) != n) {
            // send() failed.
            // We log the failure, break out of the loop,
            // and let the server continue on with the next request.
            perror("send() failed\n");
            break;
        }
    }
    // fread() returns 0 both on EOF and on error.
    // Let's check if there was an error.
    if (ferror(fp))
        perror("fread failed");

func_end:

    // clean up
    free(file);
    if (fp)
        fclose(fp);

    return statusCode;
}


static void helper(int clntSock, const char *webRoot, struct sockaddr_in clntAddr){
	
     
     char line[1000];
     char requestLine[1000];
     int statusCode;
     pid_t pid;

     pid = fork();
     if(pid>0){
	 close(clntSock);
	 return;

     }else if(pid<0){
         perror("fork error\n");
	 return;

     }else{
 
     close(servSock);
     if (clntSock < 0)
            die("accept() failed");

        FILE *clntFp = fdopen(clntSock, "r");
        if (clntFp == NULL)
            die("fdopen failed");

        char *method      = "";
        char *requestURI  = "";
        char *httpVersion = "";

        if (fgets(requestLine, sizeof(requestLine), clntFp) == NULL) {
	    statusCode = 400; // "Bad Request"
	    update(statusCode);
            goto loop_end;
        }

        char *token_separators = "\t \r\n"; // tab, space, new line
        method = strtok(requestLine, token_separators);
        requestURI = strtok(NULL, token_separators);
        httpVersion = strtok(NULL, token_separators);
        char *extraThingsOnRequestLine = strtok(NULL, token_separators);

        if (!method || !requestURI || !httpVersion || 
                extraThingsOnRequestLine) {
            statusCode = 501; // "Not Implemented"
	    update(statusCode);
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }

        if (strcmp(method, "GET") != 0) {
            statusCode = 501; // "Not Implemented"
	    update(statusCode);
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }

        if (strcmp(httpVersion, "HTTP/1.0") != 0 && 
            strcmp(httpVersion, "HTTP/1.1") != 0) {
            statusCode = 501; // "Not Implemented"
	    update(statusCode);
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }
        
        if (!requestURI || (*requestURI != '/' && strcmp(requestURI, "/statistics")!=0)) {
            statusCode = 400; // "Bad Request"
	    update(statusCode);
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }

        int len = strlen(requestURI);
	printf("%s\n",requestURI);
        if (len >= 3) {
            char *tail = requestURI + (len - 3);
            if (strcmp(tail, "/..") == 0 || 
                    strstr(requestURI, "/../") != NULL)
            {
                statusCode = 400; // "Bad Request"
		update(statusCode);
                sendStatusLine(clntSock, statusCode);
                goto loop_end;

            }
        }


        while (1) {
            if (fgets(line, sizeof(line), clntFp) == NULL) {
                statusCode = 400; // "Bad Request"
		update(statusCode);
                goto loop_end;
            }
            if (strcmp("\r\n", line) == 0 || strcmp("\n", line) == 0) {
                break;
            }
        }

        statusCode = handleFileRequest(webRoot, requestURI, clntSock);

loop_end:

        fprintf(stderr, "%s \"%s %s %s\" %d %s\n",
                inet_ntoa(clntAddr.sin_addr),
                method,
                requestURI,
                httpVersion,
                statusCode,
                getReasonPhrase(statusCode));

        // close the client socket 
        fclose(clntFp);
	close(clntSock);
	exit(0);

     	}// child process

}



int main(int argc, char *argv[])
{
    // Ignore SIGPIPE so that we don't terminate when we call
    // send() on a disconnected socket.
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        die("signal() failed");

    if(my_signal(SIGCHLD, clean) == SIG_ERR)
	die("signal() failed");

    if(my_signal(SIGINT, int_handler) == SIG_ERR)
	die("signal() failed");

    if (argc != 3) {
        fprintf(stderr, "usage: %s <server_port> <web_root>\n", argv[0]);
        exit(1);
    }

    unsigned short servPort = atoi(argv[1]);
    const char *webRoot = argv[2];

    servSock = createServerSocket(servPort);
    if((t1 = (struct table *)mmap(0, sizeof(struct table), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0))==MAP_FAILED)
	    die("mmap error");    
    sem_init(&t1->sem, 0, 1);

    struct sockaddr_in clntAddr;

    for (;;) {

        unsigned int clntLen = sizeof(clntAddr); 
        int clntSock = accept(servSock, (struct sockaddr *)&clntAddr, &clntLen);
	helper(clntSock, webRoot, clntAddr);


    } // for (;;)

    return 0;
}

