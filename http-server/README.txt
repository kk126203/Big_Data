- Part5: 
	strtok and exit are not thread-safe
	a. strtok(http://man7.org/linux/man-pages/man3/strtok.3.html) => We replace "strtok" with the thread-safe version "strtok_r"
	b. exit(http://man7.org/linux/man-pages/man3/exit.3.html) => We replace "exit" with the thread-safe version "quick_exit"

- Part6:
	Perform benchmark testing. How does it compare with part 5?
		=> Since this approach only creates 16 thread to service "handleFileRequest", 
		the performance is still behind the one in part5 when there are more than 16 clients request file transfering.


- Part7:
	1. pthread_cond_signal() or pthread_cond_broadcast()?
		=> The server behave correctly in both ways

	2. How does it compare with part 6
		=> Part6 has better performance, since it doesn't free and malloc queue node

- Part10:
	#Task 3 explanation:
	- How to send SIGUSR1 to child process when it's waiting HTTP request:
		@client side:
			$ telnet localhost 80 (This will make the server accept a connection and fork a child)
			$ ps aux (Get pid of child process)
			$ kill -SIGUSR1 [pid]

	- What will happen?
		There will be statistics printed out on stderr since child process has inherited the signal handler.

		1. If we register SIGUSR1 with SA_RESTART:
			The fgets() function will restart and waiting for HTTP request

		2. If we didn't resigter SIGUSR1 with SA_RESTART:
			There will be an interrupted system call error (EINTR)
			and the process dies, further HTTP request will not be served. 

===
# Performance Testing:
	- Install:
		pacman -S apache
	- Test: 
		ab -n 500 -c 100 http://127.0.0.1:80/

# Citation:
	- Use a wrapper to register signal with sigaction
		- http://www.cs.columbia.edu/~jae/4118/L06-signal.html

