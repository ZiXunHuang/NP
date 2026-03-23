# Network Programming Projects (NP)
A collection of high-performance network services focusing on Asynchronous I/O, Multi-process Architecture, and Inter-Process Communication (IPC) using C/C++ and POSIX standards.

# Remote Working Ground Server
**A multi-user interactive environment supporting real-time communication and complex I/O redirection between users.**
  * Multi-process Synchronization: Leveraged System V Shared Memory and Semaphores to maintain consistent global user states and prevent race conditions across concurrent processes.
  * Asynchronous Notification System: Developed a real-time messaging engine using Unix Signals (SIGUSR1) and Named Pipes (FIFO) for low-latency inter-process data streaming.
  * Inter-user Pipe: Implemented cross-user I/O redirection, allowing the output of one user's process to be piped directly to another user's input.

# Remote Batch System
**A high-concurrency web-based terminal that executes commands on remote servers and streams output back to a browser.**
  * Event-driven Architecture: Built a high-concurrency HTTP server using the Boost.Asio event loop and Non-blocking I/O, capable of managing hundreds of simultaneous client sessions with minimal CPU overhead.
  * CGI Gateway & I/O Redirection: Engineered a robust CGI environment using fork() and dup2() for dynamic redirection, enabling interactive web-based terminal updates in real-time.

# SOCKS4/4A Proxy Server
**An industrial-grade proxy server implementing the SOCKS protocol for secure and transparent data forwarding.**
  * Protocol Support: Engineered an asynchronous proxy supporting both CONNECT and BIND operations, including domain name resolution (SOCKS4A).
  * Full-Duplex Relay Engine: Implemented an asynchronous bidirectional data forwarding engine, ensuring efficient traffic flow between internal and external networks.
  * Traffic Control: Integrated a custom Wildcard Firewall for granular access control, allowing or denying connections based on IP patterns and destination rules.
