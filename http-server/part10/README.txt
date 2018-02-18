#Task 3 explanation:
	- How to send SIGUSR1 to child process when it's waiting HTTP request:
		@client side:
			telnet localhost 80 (This will make the server accept a connection and fork a child)
			ps aux (Get pid of child process)
			kill -SIGUSR1 [pid]

	- What will happen?
		There will be statistics printed out on stderr since child process has inherited the signal handler.

		1. If we register SIGUSR1 with SA_RESTART:
			The fgets() function will restart and waiting for HTTP request

		2. If we didn't resigter SIGUSR1 with SA_RESTART:
			There will be an Interrupted system call error
			and the process dies, further HTTP request will not be served. 

