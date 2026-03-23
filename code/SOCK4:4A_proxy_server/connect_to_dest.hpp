#ifndef _CONNECT_TO_DEST_HPP_
#define _CONNECT_TO_DEST_HPP_

#include <boost/bind.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using boost::asio::ip::tcp;

#define socks4_reply_len 8

class client {
public:
    client(boost::asio::io_context& io_context, tcp::socket socket_sour)
      : stopped_(false),
        socket_dest(io_context),
        socket_source(std::move(socket_sour)) {}

    client(tcp::socket socket_sour, tcp::socket socket_des)
      : stopped_(false),
        socket_dest(std::move(socket_des)),
        socket_source(std::move(socket_sour)) {}

    void op_connect_start(tcp::resolver::results_type endpoints) {
        endpoints_ = endpoints;
        start_connect(endpoints_.begin());
    }

    void op_bind_start() {
        reply_source();
    }

    void stop() {
        stopped_ = true;
        boost::system::error_code ignored_ec;
        socket_dest.close(ignored_ec);
        socket_source.close(ignored_ec);
        //std::cout << "socket close\n";
        //exit(0);
    }

private:
    void start_connect(tcp::resolver::results_type::iterator endpoint_iter) {
        if (endpoint_iter != endpoints_.end()) {
            socket_dest.async_connect(endpoint_iter->endpoint(),
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

        if (!socket_dest.is_open()) {
            start_connect(++endpoint_iter);
        }

        else if (ec) {
            socket_dest.close();
            start_connect(++endpoint_iter);
        }

        else {
            //std::cout << "Connect successfully\n";
            reply_source();
        }
    }

    void reply_source() {
        memset(socks4_reply, (char)0, socks4_reply_len);
        socks4_reply[1] = 90;
        boost::asio::async_write(socket_source, boost::asio::buffer(socks4_reply, socks4_reply_len),
                              boost::bind(&client::handle_reply_to_source, this, _1));
    }

    void handle_reply_to_source(const boost::system::error_code& ec) {
        if (stopped_)
            return;

        if (!ec) {
            start_read();
        }
        else {
            stop();
        }
    }

    void start_read() {
        memset(data_dest, '\0', max_length);
        memset(data_source, '\0', max_length);
        socket_source.async_read_some(boost::asio::buffer(data_source, max_length),
                                boost::bind(&client::handle_source_read, this, _1, _2));
        socket_dest.async_read_some(boost::asio::buffer(data_dest, max_length),
                                boost::bind(&client::handle_dest_read, this, _1, _2));
    }

    void handle_source_read(const boost::system::error_code& ec, std::size_t length) {
        if (stopped_)
            return;

        if (!ec) {
            data_source_len = length;
            dest_write(length);
        }
        else {
            stop();
        }
    }

    void handle_dest_read(const boost::system::error_code& ec, std::size_t length) {
        if (stopped_)
            return;

        if (!ec) {
            data_dest_len = length;
            source_write(length);
        }
        else {
            stop();
        }
    }

    void dest_write(std::size_t length) {
        boost::asio::async_write(socket_dest, boost::asio::buffer(data_source, length),
            boost::bind(&client::handle_dest_write, this, _1));
    }

    void handle_dest_write(const boost::system::error_code& ec) {
        if (stopped_)
            return;

        if (!ec) {
            memset(data_source, '\0', data_source_len);
            socket_source.async_read_some(boost::asio::buffer(data_source, max_length),
                                    boost::bind(&client::handle_source_read, this, _1, _2));
        }
        else {
            stop();
        }
    }

    void source_write(std::size_t length) {
        boost::asio::async_write(socket_source, boost::asio::buffer(data_dest, length),
            boost::bind(&client::handle_source_write, this, _1));
    }

    void handle_source_write(const boost::system::error_code& ec) {
        if (stopped_)
            return;

        if (!ec) {
            memset(data_dest, '\0', data_dest_len);
            socket_dest.async_read_some(boost::asio::buffer(data_dest, max_length),
                                    boost::bind(&client::handle_dest_read, this, _1, _2));
        }
        else {
            stop();
        }
    }


private:
    bool stopped_;
    enum { max_length = 32768 };
    char data_dest[max_length];
    char data_source[max_length];
    std::size_t data_dest_len;
    std::size_t data_source_len;
    tcp::resolver::results_type endpoints_;
    tcp::socket socket_dest;
    tcp::socket socket_source;
    unsigned char socks4_reply[socks4_reply_len];
};

#endif
