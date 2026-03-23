#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstring>

using boost::asio::ip::tcp;

#define max_input_len 15000
#define max_session_num 5
#define file_dir "test_case/"
#define socks4a_request_len 100

struct client_infor {
    char host[100];
    char port[10];
    char file_name[20];
    char html_id[5];
    client_infor() {
        memset(host, '\0', sizeof(host));
        memset(port, '\0', sizeof(port));
        memset(file_name, '\0', sizeof(file_name));
        memset(html_id, '\0', sizeof(html_id));
    }
};

struct socks_server_infor {
    char host[100];
    char port[10];
    bool connect_proxy;
    socks_server_infor() {
        memset(host, '\0', sizeof(host));
        memset(port, '\0', sizeof(port));
        connect_proxy = false;
    }
};

class client {
public:
    client(boost::asio::io_context& io_context)
      : stopped_(false),
        socket_(io_context) {}

    void start(tcp::resolver::results_type endpoints, char *h_id, char *file_name) {
        endpoints_ = endpoints;
        strcpy(html_id, h_id);
        file.open(file_name, std::ios::in);
        start_connect(endpoints_.begin());
    }

    void start_proxy(tcp::resolver::results_type endpoints_p, struct client_infor client) {
        proxy_dest_domain_name = client.host;
        connect_proxy = true;
        endpoints_proxy = endpoints_p;
        proxy_dest_port = client.port;
        strcpy(html_id, client.html_id);
        file.open(client.file_name, std::ios::in);
        start_connect(endpoints_proxy.begin());
    }

    void stop() {
        stopped_ = true;
        boost::system::error_code ignored_ec;
        socket_.close(ignored_ec);
    }

private:
    void start_connect(tcp::resolver::results_type::iterator endpoint_iter) {
        if (endpoint_iter != endpoints_.end()) {
            socket_.async_connect(endpoint_iter->endpoint(),
                boost::bind(&client::handle_connect,
                  this, _1, endpoint_iter));
        }
        else {
            stop();
        }
    }

    void handle_connect(const boost::system::error_code& ec,
        tcp::resolver::results_type::iterator endpoint_iter) {
        if (stopped_)
            return;

        if (!socket_.is_open()) {
            start_connect(++endpoint_iter);
        }

        else if (ec) {
            socket_.close();
            start_connect(++endpoint_iter);
        }

        else {
            if (connect_proxy) {
                send_socks4a_request();
            }
            else {
                start_read();
            }
        }
    }

    void send_socks4a_request() {
        std::size_t request_len = make_socks4a_request();
        boost::asio::async_write(socket_, boost::asio::buffer(socks4a_request, request_len),
            boost::bind(&client::handle_send_socks4a, this, _1));
    }

    void handle_send_socks4a(const boost::system::error_code& ec) {
        if (stopped_)
            return;

        if (!ec) {
            receive_socks4_reply();
        }
        else {
            stop();
        }
    }

    void receive_socks4_reply() {
        memset(socks4_reply, (char)0, max_length);
        socket_.async_read_some(boost::asio::buffer(socks4_reply, max_length),
                                boost::bind(&client::handle_receive_socks4, this, _1, _2));
    }

    void handle_receive_socks4(const boost::system::error_code& ec, std::size_t n) {
        if (stopped_)
            return;

        if (!ec) {
            if (socks4_reply[1] == (char)90) {
                start_read();
            }
            else {
                stop();
            }
        }
        else {
            stop();
        }
    }

    void start_read() {
        memset(data_, '\0', max_length);
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
                                boost::bind(&client::handle_read, this, _1, _2));
    }

    void handle_read(const boost::system::error_code& ec, std::size_t n) {
        if (stopped_)
            return;

        if (!ec) {
            bool include_mod = false;
            if (strchr(data_, '%')) {
                include_mod = true;
            }
            print_output(data_);
            if (include_mod)
                start_write();
            else
                start_read();
        }
        else {
            stop();
        }
    }

    void start_write() {
        if (stopped_)
            return;
        char cmd[max_input_len] = "";
        file.getline(cmd, max_input_len);
        strtok(cmd, "\r\n");
        print_command(cmd);
        strcat(cmd, "\n");
        boost::asio::async_write(socket_, boost::asio::buffer(cmd, strlen(cmd)),
            boost::bind(&client::handle_write, this, _1));
    }

    void handle_write(const boost::system::error_code& ec) {
        if (stopped_)
            return;

        if (!ec) {
            start_read();
        }
        else {
            stop();
        }
    }

    std::size_t make_socks4a_request() {
        int port = std::stoi(proxy_dest_port);
        int port1 = port / 256, port2 = port % 256;
        memset(socks4a_request, (char)0, socks4a_request_len);
        socks4a_request[0] = 4;
        socks4a_request[1] = 1;
        socks4a_request[2] = port1;
        socks4a_request[3] = port2;
        socks4a_request[4] = 0;
        socks4a_request[5] = 0;
        socks4a_request[6] = 0;
        socks4a_request[7] = 1;
        socks4a_request[8] = 0;
        for (std::size_t i = 0; i < proxy_dest_domain_name.length(); i++) {
            socks4a_request[i+9] = proxy_dest_domain_name[i];
        }
        return proxy_dest_domain_name.length()+9;
    }

    void print_output(const char *data) {
        std::string buf = data;
        change_to_html_format(buf, '\n', "NewLine");
        change_to_html_format(buf, '\'', "#x27");
        change_to_html_format(buf, '<', "lt");
        change_to_html_format(buf, '>', "gt");
        std::cout << "<script>document.getElementById('" << html_id << "').innerHTML += '" << buf << "';</script>" << std::endl;
    }

    void print_command(const char *cmd) {
        char buf[max_input_len] = "";
        strcpy(buf, cmd);
        std::cout << "<script>document.getElementById('" << html_id << "').innerHTML += '<cmd>" << buf << "&NewLine;</cmd>';</script>" << std::endl;
    }

    void change_to_html_format(std::string &buf, char replace_target, std::string replace_string) {
        std::size_t index;
        std::string start_symbol = "&";
        replace_string += ";";
        while ((index = buf.find(replace_target)) != std::string::npos) {
            buf.replace(index, start_symbol.length(), start_symbol);
            buf.insert(index+1, replace_string);
        }
    }

private:
    bool stopped_;
    bool connect_proxy = false;
    enum { max_length = 1024 };
    char data_[max_length];
    char html_id[5] = "";
    std::string proxy_dest_domain_name;
    std::string proxy_dest_port;
    unsigned char socks4a_request[socks4a_request_len];
    unsigned char socks4_reply[max_length];
    std::fstream file;
    tcp::resolver::results_type endpoints_;
    tcp::resolver::results_type endpoints_proxy;
    tcp::socket socket_;
};

int parse_query_string(struct client_infor *list, struct socks_server_infor *socks_server, char *query_string) {
    char html_id[5][5] = {"s0", "s1", "s2", "s3", "s4"};
    char *buf = strtok(query_string, "&"), *pos = nullptr;
    int session_count = 0, count = 1;
    while (buf) {
            pos = strchr(buf, '=');
            if (count < 6) {
                if (*(pos+1) == '\0') {
                        for (int i = 0; i < 3; i++) {
                                buf = strtok(NULL, "&");
                        }
                }
                else {
                        strcpy(list[session_count].host, pos+1);
                        buf = strtok(NULL, "&");
                        pos = strchr(buf, '=');
                        strcpy(list[session_count].port, pos+1);
                        buf = strtok(NULL, "&");
                        pos = strchr(buf, '=');
                        strcat(list[session_count].file_name, file_dir);
                        strcat(list[session_count].file_name, pos+1);
                        buf = strtok(NULL, "&");
                        strcpy(list[session_count].html_id, html_id[session_count]);
                        session_count++;
                }
                count++;
            }
            else {
                if (*(pos+1) == '\0') {
                        for (int i = 0; i < 2; i++) {
                                buf = strtok(NULL, "&");
                        }
                }
                else {
                    strcpy(socks_server->host, pos+1);
                    buf = strtok(NULL, "&");
                    pos = strchr(buf, '=');
                    strcpy(socks_server->port, pos+1);
                    buf = strtok(NULL, "&");
                    socks_server->connect_proxy = true;
                }
            }
    }
    return session_count;
}

void set_html_format(struct client_infor *list, int session_count) {
    std::cout << "Content-type: text/html\r\n\r\n";
    std::cout << "<!DOCTYPE html>\n";
    std::cout << "<html lang=\"en\">\n";
    std::cout << "  <head>\n";
    std::cout << "    <meta charset=\"UTF-8\" />\n";
    std::cout << "    <title>NP Project 3 Sample Console</title>\n";
    std::cout << "    <link\n";
    std::cout << "      rel=\"stylesheet\"\n";
    std::cout << "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n";
    std::cout << "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n";
    std::cout << "      crossorigin=\"anonymous\"\n";
    std::cout << "    />\n";
    std::cout << "    <link\n";
    std::cout << "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n";
    std::cout << "      rel=\"stylesheet\"\n";
    std::cout << "    />\n";
    std::cout << "    <link\n";
    std::cout << "      rel=\"icon\"\n";
    std::cout << "      type=\"image/png\"\n";
    std::cout << "      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n";
    std::cout << "    />\n";
    std::cout << "    <style>\n";
    std::cout << "      * {\n";
    std::cout << "        font-family: 'Source Code Pro', monospace;\n";
    std::cout << "        font-size: 1rem !important;\n";
    std::cout << "      }\n";
    std::cout << "     body {\n";
    std::cout << "       background-color: #212529;\n";
    std::cout << "     }\n";
    std::cout << "     pre {\n";
    std::cout << "       color: #cccccc;\n";;
    std::cout << "     }\n";
    std::cout << "     cmd {\n";
    std::cout << "       color: #01b468;\n";
    std::cout << "      }\n";
    std::cout << "    </style>\n";
    std::cout << "  </head>\n";
    std::cout << "  <body>\n";
    std::cout << "    <table class=\"table table-dark table-bordered\">\n";
    std::cout << "      <thead>\n";
    std::cout << "        <tr>\n";
    for (int i = 0; i < session_count; i++) {
        std::cout << "          <th scope=\"col\">" << list[i].host << ":" << list[i].port << "</th>\n";
    }
    std::cout << "        </tr>" << std::endl;
    std::cout << "      </thead>\n";
    std::cout << "      <tbody>\n";
    std::cout << "        <tr>\n";
    for (int i = 0; i < session_count; i++) {
        std::cout << "          <td><pre id=\"" << list[i].html_id << "\" class=\"mb-0\"></pre></td>\n";
    }
    std::cout << "        </tr>\n";
    std::cout << "      </tbody>\n";
    std::cout << "    </table>\n";
    std::cout << "  </body>\n";
    std::cout << "</html>" << std::endl;
}

int main() {
    try {
        struct client_infor list[5];
        struct socks_server_infor socks_server;
        int session_count = parse_query_string(list, &socks_server, getenv("QUERY_STRING"));
        if (!session_count) {
            return 1;
        }
        set_html_format(list, session_count);
        boost::asio::io_context io_context;
        tcp::resolver *r[max_session_num];
        tcp::resolver *r_p[max_session_num]; //-------------
        client *c[max_session_num];

        for (int i = 0; i < session_count; i++) {
            r[i] = new tcp::resolver(io_context);
            r_p[i] = new tcp::resolver(io_context);
            c[i] = new client(io_context);
            if (socks_server.connect_proxy) {
                c[i]->start_proxy(r_p[i]->resolve(socks_server.host, socks_server.port), list[i]);
            }
            else {
                c[i]->start(r[i]->resolve(list[i].host, list[i].port), list[i].html_id, list[i].file_name);
            }
        }

        io_context.run();

        for (int i = 0; i < session_count; i++) {
            delete r[i];
            delete c[i];
            delete r_p[i];
        }
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
