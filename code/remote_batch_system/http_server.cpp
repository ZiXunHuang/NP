#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <vector>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>

using boost::asio::ip::tcp;

boost::asio::io_context global_io_context;

void child_handler(int signo) {
    while (waitpid(-1, NULL, WNOHANG) > 0){}
}

class session
  : public std::enable_shared_from_this<session> {
public:
    session(tcp::socket socket)
      : socket_(std::move(socket)) {}

    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(request, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                  std::string http_request = request;
                  std::vector<std::string> http_env;
                  boost::split(http_env, http_request, boost::is_any_of(" \n"));
                  setenv("REQUEST_METHOD", http_env[0].c_str(), 1);
                  setenv("REQUEST_URI", http_env[1].c_str(), 1);
                  strcpy(REQUEST_URI, http_env[1].c_str());
                  setenv("SERVER_PROTOCOL", http_env[2].c_str(), 1);
                  setenv("HTTP_HOST", http_env[4].c_str(), 1);
                  do_write(length);
                }
            });
    }

    void do_write(std::size_t length) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(status_str, strlen(status_str)),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    strcpy(SERVER_ADDR, socket_.local_endpoint().address().to_string().c_str());
                    sprintf(SERVER_PORT, "%u", socket_.local_endpoint().port());
                    strcpy(REMOTE_ADDR, socket_.remote_endpoint().address().to_string().c_str());
                    sprintf(REMOTE_PORT, "%u", socket_.remote_endpoint().port());

                    strcat(exec_file, strtok(REQUEST_URI, "?"));

                    setenv("QUERY_STRING", strtok(NULL, "?"), 1);
                    setenv("SERVER_ADDR", SERVER_ADDR, 1);
                    setenv("SERVER_PORT", SERVER_PORT, 1);
                    setenv("REMOTE_ADDR", REMOTE_ADDR, 1);
                    setenv("REMOTE_PORT", REMOTE_PORT, 1);

                    global_io_context.notify_fork(boost::asio::io_service::fork_prepare);
                    pid_t pid = fork();
                    if (!pid) {
                        global_io_context.notify_fork(boost::asio::io_service::fork_child);
                        int sock_fd = socket_.native_handle();
                        dup2(sock_fd, STDERR_FILENO);
                        dup2(sock_fd, STDIN_FILENO);
                        dup2(sock_fd, STDOUT_FILENO);
                        socket_.close();
                        if (execlp(exec_file, exec_file, NULL) < 0) {
                            std::cout << "Content-type:text/html\r\n\r\n<h1>FAIL</h1>";
                            exit(0);
                        }
                    }
                    else {
                        global_io_context.notify_fork(boost::asio::io_service::fork_parent);
                        socket_.close();
                    }
                }
            });
    }

    tcp::socket socket_;
    enum { max_length = 1024 };
    char request[max_length] = "";
    char status_str[200] = "HTTP/1.1 200 OK\n";
    char REQUEST_URI[1000] = "";
    char SERVER_ADDR[100] = "";
    char SERVER_PORT[10]= "";
    char REMOTE_ADDR[100] = "";
    char REMOTE_PORT[10] = "";
    char exec_file[100] = ".";
};

class server {
public:
    server(boost::asio::io_context& io_context, short port)
      : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
      acceptor_.async_accept(
          [this](boost::system::error_code ec, tcp::socket socket) {
              if (!ec) {
                  std::make_shared<session>(std::move(socket))->start();
              }

              do_accept();
          });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }
        signal(SIGCHLD, child_handler);
        server s(global_io_context, std::atoi(argv[1]));

        global_io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
