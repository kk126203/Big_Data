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
#include <pthread.h>	/*  */
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define MAXPENDING 512    /* Maximum outstanding connection requests */

#define DISK_IO_BUF_SIZE 4096
#define N_THREADS 4

/*
 * A message in a blocking queue
 */
struct message {
    int sock; // Payload, in our case a new client connection
    struct message *next; // Next message on the list
};

/*
 * This structure implements a blocking queue. 
 * If a thread attempts to pop an item from an empty queue 
 * it is blocked until another thread appends a new item.
 */
struct queue {
    pthread_mutex_t mutex; // mutex used to protect the queue
    pthread_cond_t cond;   // condition variable for threads to sleep on
    struct message *first; // first message in the queue
    struct message *last;  // last message in the queue
    unsigned int length;   // number of elements on the queue
};

static pthread_t thread_pool[N_THREADS];
int servSock;
struct queue *q;
pthread_mutex_t mutex;
int ctrlC = 0;

// initializes the members of struct queue
void queue_init(void){
    q = (struct queue *)malloc(sizeof(struct queue));
    if(pthread_mutex_init(&mutex, NULL))
        fprintf(stderr, "mutex init fail\n");
    if(pthread_mutex_init(&q->mutex, NULL))
        fprintf(stderr, "mutex init fail\n");
    if(pthread_cond_init(&(q->cond), NULL))
        fprintf(stderr, "cond init fail\n");
    q->first = NULL;
    q->last = NULL;
    q->length = 0;
}

// deallocate and destroy everything in the queue
void queue_destroy(struct queue *q){
    struct message *curr = NULL;
    struct message *next = NULL;
    
    if(q != NULL){
        curr = q->first;
    }

    while(curr != NULL){
        next = curr->next;
        free(curr);
        curr = next;
    }
}

// put a message into the queue and wake up workers if necessary
void queue_put(struct queue *q, int sock){
    struct message *m;
    
    pthread_mutex_lock(&q->mutex);
 
    m = (struct message *)malloc(sizeof(struct message));
    m->sock = sock;
    m->next = NULL;
    if(q->last != NULL){
        q->last->next = m;
    }
    q->last = m;
    q->length = q->length + 1;

    if(q->first == NULL){
        q->first = m;
    }

    pthread_mutex_unlock(&q->mutex);
}

// take a socket descriptor from the queue; block if necessary
int queue_get(struct queue *q){
    struct message *m;
    int sock;

    pthread_mutex_lock(&q->mutex);
    
    if(q->length == 0){
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    m = q->first;
    sock = m->sock;
    q->first = m->next;
    q->length = q->length - 1;
    free(m);

    if(q->length == 0){
        q->first = NULL;
        q->last = NULL;
    }

    pthread_mutex_unlock(&q->mutex);

    return sock;
}


static pthread_t thread_pool[N_THREADS];
int servSock;

static void sig_int(int signo)
{
	// close the server socket 
	close(servSock);
	printf("pressing ctrl-c!\n");
	for(int i = 0; i < N_THREADS; ++i){
		pthread_cancel(thread_pool[i]); 
	}
    ctrlC = 1;
//quick_exit(EXIT_SUCCESS); 
}

static void die(const char *message)
{
	perror(message);
	quick_exit(EXIT_SUCCESS); 
}

/* Reliable version of signal(), using POSIX sigaction().  */
typedef void sigfunc(int);
	static	sigfunc *
signal_register(int signo, sigfunc *func)
{
	struct sigaction    act, oact;
	act.sa_handler = func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if (signo == SIGALRM) {
#ifdef  SA_INTERRUPT
		act.sa_flags |= SA_INTERRUPT;
#endif
	} else {
		act.sa_flags |= SA_RESTART;
	}
	if (sigaction(signo, &act, &oact) < 0)
		return(SIG_ERR);
	return(oact.sa_handler);
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
		sendStatusLine(clntSock, statusCode);
		goto func_end;
	}

	// If unable to open the file, send "404 Not Found".

	fp = fopen(file, "rb");
	if (fp == NULL) {
		statusCode = 404; // "Not Found"
		sendStatusLine(clntSock, statusCode);
		goto func_end;
	}

	// Otherwise, send "200 OK" followed by the file content.

	statusCode = 200; // "OK"
	sendStatusLine(clntSock, statusCode);

	// send the file 
	size_t n;
	char buf[DISK_IO_BUF_SIZE];
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (send(clntSock, buf, n, 0) != n) {
			// send() failed.
			// We log the failure, break out of the loop,
			// and let the server continue on with the next request.
			perror("\nsend() failed");
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

struct thread_param{
	/*	const char *clntAddr_str;
		const char *method;
		const char *httpVersion;
		*/	const char *webRoot;
	//	const char *requestURI;
	int clntSock;
};

void parse_request(const char *webRoot, int clntSock){
	char line[1000];
	char requestLine[1000];
	int statusCode;
	struct sockaddr_in  tmp_addr;
	socklen_t addr_len; 

    for (;;) {
        do{
            clntSock = queue_get(q);
            if(clntSock < 0){
                pthread_cond_wait(&(q->cond), &mutex);
            }
        }while(clntSock < 0);

		FILE *clntFp = fdopen(clntSock, "r");
		if (clntFp == NULL)
			die("fdopen failed");
    
		/*
		 * Let's parse the request line.
		 */
    
		char *method      = "";
		char *requestURI  = "";
		char *httpVersion = "";
    
		if (fgets(requestLine, sizeof(requestLine), clntFp) == NULL) {
			// socket closed - there isn't much we can do
            printf("line:%d\n",__LINE__);
			statusCode = 400; // "Bad Request"
			goto loop_end;
		}
    
    		char *saveptr;
		char *token_separators = "\t \r\n"; // tab, space, new line
        method = strtok_r(requestLine, token_separators, &saveptr);
        requestURI = strtok_r(NULL, token_separators, &saveptr);
        httpVersion = strtok_r(NULL, token_separators, &saveptr);
		char *extraThingsOnRequestLine = strtok(NULL, token_separators);
    
		// check if we have 3 (and only 3) things in the request line
		if (!method || !requestURI || !httpVersion || 
				extraThingsOnRequestLine) {
			statusCode = 501; // "Not Implemented"
			sendStatusLine(clntSock, statusCode);
			goto loop_end;
		}
    
		// we only support GET method 
		if (strcmp(method, "GET") != 0) {
			statusCode = 501; // "Not Implemented"
			sendStatusLine(clntSock, statusCode);
			goto loop_end;
		}
    
		// we only support HTTP/1.0 and HTTP/1.1
		if (strcmp(httpVersion, "HTTP/1.0") != 0 && 
				strcmp(httpVersion, "HTTP/1.1") != 0) {
			statusCode = 501; // "Not Implemented"
			sendStatusLine(clntSock, statusCode);
			goto loop_end;
		}
    
		// requestURI must begin with "/"
		if (!requestURI || *requestURI != '/') {
            printf("line:%d\n",__LINE__);
			statusCode = 400; // "Bad Request"
			sendStatusLine(clntSock, statusCode);
			goto loop_end;
		}
    
		// make sure that the requestURI does not contain "/../" and 
		// does not end with "/..", which would be a big security hole!
		int len = strlen(requestURI);
		if (len >= 3) {
			char *tail = requestURI + (len - 3);
			if (strcmp(tail, "/..") == 0 || 
					strstr(requestURI, "/../") != NULL)
			{
                printf("line:%d\n",__LINE__);
				statusCode = 400; // "Bad Request"
				sendStatusLine(clntSock, statusCode);
				goto loop_end;
			}
		}
    
		/*
		 * Now let's skip all headers.
		 */
    
		while (1) {
			if (fgets(line, sizeof(line), clntFp) == NULL) {
				// socket closed prematurely - there isn't much we can do
                printf("line:%d\n",__LINE__);
				statusCode = 400; // "Bad Request"
				goto loop_end;
			}
			if (strcmp("\r\n", line) == 0 || strcmp("\n", line) == 0) {
				// This marks the end of headers.  
				// Break out of the while loop.
				break;
			}
		}
    
		/*
		 * At this point, we have a well-formed HTTP GET request.
		 * Let's handle it.
		 */
    
		statusCode = handleFileRequest(webRoot, requestURI, clntSock);
    loop_end:
    
		/*
		 * Done with client request.
		 * Log it, close the client socket, and go back to accepting
		 * connection.
		 */
		addr_len = sizeof(tmp_addr);
		getpeername(clntSock, (struct sockaddr*)&tmp_addr, &addr_len);
    
		fprintf(stderr, "%s \"%s %s %s\" %d %s\n",
				inet_ntoa(tmp_addr.sin_addr),
				method,
				requestURI,
				httpVersion,
				statusCode,
				getReasonPhrase(statusCode));
    
		// close the client socket 
		fclose(clntFp);
    } // for (;;)

}

//for the cancellation point
static void cleanup_handler(void *plock) {
    pthread_mutex_unlock(plock);
}

void thread_entry(void* webRoot){
	//struct thread_param *params = (struct thread_param*) para;

    pthread_cleanup_push(cleanup_handler, &mutex);
	struct sockaddr_in clntAddr;
	unsigned int clntLen = sizeof(clntAddr); 
	for(;;){
	int clntSock = accept(servSock, (struct sockaddr *)&clntAddr, &clntLen);
	if (clntSock < 0)
		die("accept() failed");


	parse_request(webRoot, clntSock);
	}
    pthread_cleanup_pop(0);

}

void handleFileRequest_pthread(const char *webRoot){
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

    queue_init();

	for(int i = 0; i < N_THREADS;i++){
	    /* create a second thread which executes inc_x(&x) */
	    pthread_create(&thread_pool[i], NULL, (void*) &thread_entry, (void*)webRoot);
	}
	
    // Accept sigint on main thread
	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
    
    for (;;) {

        /*
         * wait for a client to connect
         */
        if(ctrlC == 1){
            break;
        }

        // initialize the in-out parameter
        struct sockaddr_in clntAddr;
        unsigned int clntLen = sizeof(clntAddr); 
        int clntSock = accept(servSock, (struct sockaddr *)&clntAddr, &clntLen);
        if (clntSock < 0)
            continue;

        // push a socket into the queue
        queue_put(q, clntSock);

        // wake up a thread to service this socket
        pthread_cond_signal(&(q->cond));
    }

	for(int i = 0; i < N_THREADS; ++i){
		pthread_join(thread_pool[i], NULL);
	}
    
    // free the memory occupied by this queue
    queue_destroy(q);
    free(q);

}

int main(int argc, char *argv[])
{
	// Ignore SIGPIPE so that we don't terminate when we call
	// send() on a disconnected socket.
	if (signal_register(SIGPIPE, SIG_IGN) == SIG_ERR)
		die("signal() failed");

	if (signal(SIGINT, sig_int) == SIG_ERR)
		die("signal() failed");

	if (argc != 3) {
		fprintf(stderr, "usage: %s <server_port> <web_root>\n", argv[0]);
	    quick_exit(EXIT_SUCCESS); 
	}

	unsigned short servPort = atoi(argv[1]);
	const char *webRoot = argv[2];

	servSock = createServerSocket(servPort);


	//for (;;) {

		/*
		 * wait for a client to connect
		 */

		// initialize the in-out parameter
				handleFileRequest_pthread(webRoot);

	//} // for (;;)



	return 0;
}

