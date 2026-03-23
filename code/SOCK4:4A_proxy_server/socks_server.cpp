#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include "connect_to_dest.hpp"

//using boost::asio::ip::tcp;

#define firewall_file "socks.conf"

boost::asio::io_context global_io_context;
tcp::resolver *r = nullptr;
client *c = nullptr;

void child_handler(int signo) {
    while (waitpid(-1, NULL, WNOHANG) > 0){}
}

void connect_operation(tcp::socket socket, std::string dest_ip, std::string dest_port) {
    r = new tcp::resolver(global_io_context);
    c = new client(global_io_context, std::move(socket));
    c->op_connect_start(r->resolve(dest_ip, dest_port));
}

void bind_operation(tcp::socket socket_source, tcp::socket socket_dest) {
    c = new client(std::move(socket_source), std::move(socket_dest));
    c->op_bind_start();
}

class session : public std::enable_shared_from_this<session> {
public:
    session(std::string s_ip, short unsigned int s_port, tcp::socket socket)
      : socket_(std::move(socket)),
        source_ip(s_ip),
        source_port(std::to_string(s_port)) {}

    void start() {
        memset(socks4_reply, (char)0, socks4_reply_len);
        do_read();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(socks4_request, max_length),   //-----------------
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    get_dest_port();
                    get_dest_ip();
                    std::cout << "<S_IP>: " << source_ip << "\n";
                    std::cout << "<S_PORT>: " << source_port <<'\n';
                    std::cout << "<D_IP>: " << dest_ip << '\n';
                    std::cout << "<D_PORT>: " << dest_port <<'\n';
                    if ((int)socks4_request[1] == 1) {
                        std::cout << "<Command>: CONNECT\n";
                    }
                    else {
                        std::cout << "<Command>: BIND\n";
                    }
                    if (check_dest_ip(socks4_request[1])) {
                        if ((int)socks4_request[1] == 1) {
                            connect_operation(std::move(socket_), dest_ip, dest_port);
                        }
                        else {
                            open_new_port();
                            socks4_reply[1] = 90;
                            socks4_reply[2] = (local_port / 256);
                            socks4_reply[3] = (local_port % 256);
                            reply_to_client();
                        }
                    }
                    else {
                        socks4_reply[1] = 91;
                        reply_to_client();
                    }
                }
            });
    }

    void open_new_port() {
        auto self(shared_from_this());
        acceptor_ = new tcp::acceptor(global_io_context);
        tcp::endpoint endpoint_(tcp::v4(), 0);
        acceptor_->open(endpoint_.protocol());
        boost::asio::socket_base::reuse_address option(true);
        acceptor_->set_option(option);
        acceptor_->bind(endpoint_);
        acceptor_->listen();
        local_port = acceptor_->local_endpoint().port();
        acceptor_->async_accept(
            [this, self](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    bind_operation(std::move(socket_), std::move(socket));
                    delete acceptor_;
                }
                else {
                    //std::cout << "Aceppt destination failed\n";
                }
            });
    }

    void reply_to_client() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(socks4_reply, socks4_reply_len),
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    //std::cout << "bind reply successfully\n";  //----------------
                }
            });
    }

    void get_dest_port() {
        dest_port = std::to_string((int)socks4_request[2] * 256 + (int)socks4_request[3]);
    }

    void get_dest_ip() {
        if (!(int)socks4_request[4] && !(int)socks4_request[5] && !(int)socks4_request[6]) {
            get_domain_name();
        }
        else {
            dest_ip = std::to_string((int)socks4_request[4]);
            dest_ip += ".";
            dest_ip += std::to_string((int)socks4_request[5]);
            dest_ip += ".";
            dest_ip += std::to_string((int)socks4_request[6]);
            dest_ip += ".";
            dest_ip += std::to_string((int)socks4_request[7]);
        }
    }

    void get_domain_name() {
        int count = 8;
        while((int)socks4_request[count]) {
            count++;
        }
        count++;
        while((int)socks4_request[count]) {
            domain_name += socks4_request[count];
            count++;
        }

        tcp::resolver resolver(global_io_context);
        tcp::resolver::query query(domain_name, "");
        tcp::resolver::iterator iterator = resolver.resolve(query);
        tcp::resolver::iterator end;
        while (iterator != end) {
            tcp::endpoint endpoint_ = *iterator++;
            if (endpoint_.address().is_v4()) {
                dest_ip = endpoint_.address().to_string();
                break;
            }
        }
    }

    bool check_dest_ip(int operation) {
        char ignore[10], op_type[2], ip[50];
        FILE *fp = fopen(firewall_file, "r");
        while (fscanf(fp, "%s %s %s", ignore, op_type, ip) > 0) {
            bool same_type = false;
            if (operation == 1 && op_type[0] == 'c') {
                same_type = true;
            }
            else if (operation == 2 && op_type[0] == 'b') {
                same_type = true;
            }
            if (same_type) {
                bool same_str = true;
                int count = 0, count2 = 0;
                while (dest_ip[count] != '\0' && ip[count2] != '\0') {
                    if (dest_ip[count] != ip[count2]) {
                        if (ip[count2] == '*') {
                            while (dest_ip[count] != '\0' && dest_ip[count] != '.') {
                                count++;
                            }
                        }
                        else {
                            same_str = false;
                            break;
                        }
                    }
                    else {
                        count++;
                    }
                    count2++;
                }
                if (same_str) {
                    if (dest_ip[count] == ip[count2]) {
                        std::cout << "<Relpy>: Accept\n" << std::endl;
                        return true;
                    }
                }
            }
        }
        std::cout << "<Relpy>: Reject\n" << std::endl;
        fclose(fp);
        return false;
    }

    tcp::socket socket_;
    enum { max_length = 1024 };
    unsigned char socks4_request[max_length];
    unsigned char socks4_reply[socks4_reply_len];
    std::string source_ip;
    std::string source_port;
    std::string dest_ip;
    std::string dest_port;
    std::string domain_name = "";
    tcp::acceptor *acceptor_;
    short unsigned int local_port;
};

class server {
public:
    server(short port)
      : acceptor_(global_io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
      acceptor_.async_accept(
          [this](boost::system::error_code ec, tcp::socket socket) {
              if (!ec) {
                  global_io_context.notify_fork(boost::asio::io_service::fork_prepare);
                  pid_t pid;
                  while ((pid = fork()) < 0) {
                      waitpid(-1, NULL, WNOHANG);
                  }
                  if (!pid) {
                      global_io_context.notify_fork(boost::asio::io_service::fork_child);
                      std::make_shared<session>(socket.remote_endpoint().address().to_string(),
                                                socket.remote_endpoint().port(),
                                                std::move(socket))->start();
                  }
                  else {
                      global_io_context.notify_fork(boost::asio::io_service::fork_parent);
                      socket.close();
                      do_accept();
                  }
              }
              else {
                  socket.close();
                  do_accept();
              }
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

        server s(std::atoi(argv[1]));

        global_io_context.run();

        if (r != nullptr) {
            delete r;
        }
        if (c != nullptr) {
            delete c;
        }
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    //std::cout << "Child leave\n";
    return 0;
}
