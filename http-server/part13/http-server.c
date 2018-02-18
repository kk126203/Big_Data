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
#include <sys/wait.h>   /* for waitpid() */
#include <sys/mman.h>   /* for shared memory */
#include <semaphore.h>  /* for semaphore */
#include <pthread.h>	/* for pthread*/
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define MAXPENDING 512    /* Maximum outstanding connection requests */

#define DISK_IO_BUF_SIZE 4096
#define N_PROCESSES 8 

int servSock;

static void sig_int(int signo)
{
	// close the server socket 
	close(servSock);
	printf("pressing ctrl-c!\n");
    quick_exit(EXIT_SUCCESS); 
}

static void die(const char *message)
{
	perror(message);
    quick_exit(EXIT_SUCCESS); 
}

/* struct for statistics */
struct reqstat{
	sem_t sem;
	unsigned int requests;
	unsigned int two;
	unsigned int three;
	unsigned int four;
	unsigned int five;
};

struct reqstat *shared_stat; 	/* The address of shared memory for statistics */ 


void update_stat(int statusCode){

	/* acquire the lock*/
	sem_wait(&shared_stat->sem);

	/* Update statistic */
	shared_stat->requests += 1;
	switch(statusCode/100){
		case 2:
			shared_stat->two += 1;	
			break;
		case 3:
			shared_stat->three += 1;	
			break;
		case 4:
			shared_stat->four += 1;	
			break;
		case 5:
			shared_stat->five += 1;	
			break;
		default:
			die("unknown status code");

	}

	/* release the lock*/
	sem_post(&shared_stat->sem);


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

	update_stat(statusCode);
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

void print_stat_thread(){
	/* acquire the lock*/
	sem_wait(&shared_stat->sem);

	fprintf(stderr ,"Server Statistics\n\nRequests:%d\n\n2xx:%d\n3xx:%d\n4xx:%d\n5xx:%d\n"
			,shared_stat->requests
			,shared_stat->two
			,shared_stat->three
			,shared_stat->four
			,shared_stat->five);
	
	/* release the lock*/
	sem_post(&shared_stat->sem);

	pthread_exit(0);
}

void print_stat(int signo){
	pthread_t new_thread;
	/* create a second thread which executes inc_x(&x) */
	if(pthread_create(&new_thread, NULL, (void*) &print_stat_thread, NULL)) {
		return;
	}

	pthread_detach(new_thread);	

}

// Send clntSock through sock.
// sock is a UNIX domain socket.

static void sendConnection(int clntSock, int sock)
{
	struct msghdr msg;
	struct iovec iov[1];

	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof(int))];
	} ctrl_un;
	struct cmsghdr *cmptr;

	msg.msg_control = ctrl_un.control;
	msg.msg_controllen = sizeof(ctrl_un.control);

	cmptr = CMSG_FIRSTHDR(&msg);
	cmptr->cmsg_len = CMSG_LEN(sizeof(int));
	cmptr->cmsg_level = SOL_SOCKET;
	cmptr->cmsg_type = SCM_RIGHTS;
	*((int *) CMSG_DATA(cmptr)) = clntSock;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	iov[0].iov_base = "FD";
	iov[0].iov_len = 2;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	if (sendmsg(sock, &msg, 0) != 2)
		die("Failed to send connection to child");
}

// Returns an open file descriptor received through sock.
// sock is a UNIX domain socket.

static int recvConnection(int sock)
{
	struct msghdr msg;
	struct iovec iov[1];
	ssize_t n;
	char buf[64];

	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof(int))];
	} ctrl_un;
	struct cmsghdr *cmptr;

	msg.msg_control = ctrl_un.control;
	msg.msg_controllen = sizeof(ctrl_un.control);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(buf);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	for (;;) {
		n = recvmsg(sock, &msg, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			printf("sock: %d\n", sock);
			die("Error in recvmsg");

		}
		// Messages with client connections are always sent with 
		// "FD" as the message. Silently skip unsupported messages.
		if (n != 2 || buf[0] != 'F' || buf[1] != 'D')
			continue;

		if ((cmptr = CMSG_FIRSTHDR(&msg)) != NULL
				&& cmptr->cmsg_len == CMSG_LEN(sizeof(int))
				&& cmptr->cmsg_level == SOL_SOCKET
				&& cmptr->cmsg_type == SCM_RIGHTS)
			return *((int *) CMSG_DATA(cmptr));
	}
}



int main(int argc, char *argv[])
{
	// Ignore SIGPIPE so that we don't terminate when we call
	// send() on a disconnected socket.
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		die("signal() failed");

	if (argc != 3) {
		fprintf(stderr, "usage: %s <server_port> <web_root>\n", argv[0]);
        quick_exit(EXIT_SUCCESS); 
	}

	unsigned short servPort = atoi(argv[1]);
	const char *webRoot = argv[2];

	servSock = createServerSocket(servPort);

	char line[1000];
	char requestLine[1000];
	int statusCode;
	struct sockaddr_in clntAddr, tmp_addr;
	socklen_t addr_len;
    char *saveptr;

	/* Setup shared memory for statistics */
	if ((shared_stat = (struct reqstat*)mmap(0, sizeof(struct reqstat), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0)) == MAP_FAILED)
		die("mmap error");

	/* Init the semaphore to protect statistic data */
	sem_init(&shared_stat->sem, 1, 1);
	if (signal_register(SIGUSR1, SIG_IGN) == SIG_ERR)
		die("signal() failed");
	pid_t child_pid;
	int sockets[N_PROCESSES];
	for(int i = 0 ; i < N_PROCESSES;i++){
		/* Creat Unix Domain socket pair */
		int pair[2];
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair))
			die("socketpair error");
		sockets[i] = pair[1];
		child_pid = fork();

		if(child_pid >= 0){
			if(child_pid == 0){  // Child1

					close(servSock);
					close(pair[1]);
				for (;;) {
					int clntSock = recvConnection(pair[0]);
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
						statusCode = 400; // "Bad Request"
						// printf("socket closed\n");
						update_stat(statusCode);
						goto loop_end;
					}
					char *token_separators = "\t \r\n"; // tab, space, new line
                    method = strtok_r(requestLine, token_separators, &saveptr);
                    requestURI = strtok_r(NULL, token_separators, &saveptr);
                    httpVersion = strtok_r(NULL, token_separators, &saveptr);
                    char *extraThingsOnRequestLine = strtok_r(NULL, token_separators, &saveptr);

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
							statusCode = 400; // "Bad Request"
							update_stat(statusCode);
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

                
                quick_exit(EXIT_SUCCESS); 
			}
			else{ // Parent
				close(pair[0]);
			}

		}
		else{
			perror("fork failed\n");
			return 0;
		}
	}
	if (signal_register(SIGUSR1, print_stat) == SIG_ERR)
		die("signal() failed");

	if (signal_register(SIGINT, sig_int) == SIG_ERR)
		die("signal() failed");

	unsigned int clntLen = sizeof(clntAddr); 
	/*
	 * wait for a client to connect
	 */
	int serial = 0; /* the serial no. of worker process to be selected */
	for(;;){
		int clntSock = accept(servSock, (struct sockaddr *)&clntAddr, &clntLen);
		if (clntSock < 0)
			die("accept() failed");


		/* Send new clntSock to worker processes through Unix Domain Socket */
		/* sockets[serial]: the Unix Domain Socket of the related process */	
		sendConnection(clntSock, sockets[serial]); 
		close(clntSock);
		serial++;
		serial = serial%N_PROCESSES;
	}
	/* Wait for all child to be finished */
	wait(NULL);

	return 0;

}

