1. pthread_cond_signal() or pthread_cond_broadcast()?
=> The server behave correctly in both ways

2. How does it compare with part 6
=> Part6 has better performance, since it doesn't free and malloc queue node
