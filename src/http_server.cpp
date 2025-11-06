#include "http_server.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/config.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer::Impl {
public:
    Impl(int port) : acceptor_(ioc_), socket_(ioc_), port_(port) {}
    
    void add_route(const std::string& path, RouteHandler handler) {
        routes_[path] = std::move(handler);
    }
    
    void run() {
        try {
            auto const address = net::ip::make_address("0.0.0.0");
            
            // Open the acceptor
            acceptor_.open(tcp::v4());
            acceptor_.set_option(net::socket_base::reuse_address(true));
            acceptor_.bind({address, static_cast<unsigned short>(port_)});
            acceptor_.listen(net::socket_base::max_listen_connections);
            
            // Start accepting connections
            do_accept();
            
            // Run the I/O service
            ioc_.run();
        } catch (std::exception const& e) {
            std::cerr << "HTTP Server Error: " << e.what() << std::endl;
        }
    }
    
    void stop() {
        ioc_.stop();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            socket_,
            [this](beast::error_code ec) {
                if (!ec) {
                    std::make_shared<HttpSession>(std::move(socket_), routes_)->run();
                }
                do_accept();
            });
    }
    
    class HttpSession : public std::enable_shared_from_this<HttpSession> {
    public:
        HttpSession(tcp::socket&& socket, std::map<std::string, RouteHandler>& routes)
            : stream_(std::move(socket)), routes_(routes) {}
        
        void run() {
            do_read();
        }
        
    private:
        void do_read() {
            req_ = {};
            stream_.expires_after(std::chrono::seconds(30));
            
            http::async_read(stream_, buffer_, req_,
                [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
                    boost::ignore_unused(bytes_transferred);
                    if (!ec) {
                        self->handle_request();
                    }
                });
        }
        
        void handle_request() {
            // CORS headers
            auto const cors_headers = [](auto& response) {
                response.set(http::field::access_control_allow_origin, "*");
                response.set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
                response.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
            };
            
            // Handle OPTIONS (CORS preflight)
            if (req_.method() == http::verb::options) {
                http::response<http::string_body> res{http::status::ok, req_.version()};
                cors_headers(res);
                res.set(http::field::content_type, "text/plain");
                res.prepare_payload();
                send_response(std::move(res));
                return;
            }
            
            // Find route handler
            std::string path = std::string(req_.target());
            auto it = routes_.find(path);
            
            if (it != routes_.end()) {
                // Build request object
                HttpRequest request;
                request.method = std::string(req_.method_string());
                request.path = path;
                request.body = req_.body();
                
                // Call handler
                std::string response_body = it->second(request);
                
                // Create response
                http::response<http::string_body> res{http::status::ok, req_.version()};
                cors_headers(res);
                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(http::field::content_type, "application/json");
                res.body() = response_body;
                res.prepare_payload();
                
                send_response(std::move(res));
            } else {
                // 404 Not Found
                http::response<http::string_body> res{http::status::not_found, req_.version()};
                cors_headers(res);
                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(http::field::content_type, "application/json");
                res.body() = R"({"error":"Not Found"})";
                res.prepare_payload();
                
                send_response(std::move(res));
            }
        }
        
        void send_response(http::response<http::string_body> response) {
            auto sp = std::make_shared<http::response<http::string_body>>(std::move(response));
            res_ = sp;
            
            http::async_write(stream_, *sp,
                [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
                    boost::ignore_unused(bytes_transferred);
                    self->stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
                });
        }
        
        beast::tcp_stream stream_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> req_;
        std::shared_ptr<http::response<http::string_body>> res_;
        std::map<std::string, RouteHandler>& routes_;
    };
    
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
    int port_;
    std::map<std::string, RouteHandler> routes_;
};

HttpServer::HttpServer(int port) : impl_(std::make_unique<Impl>(port)) {}

HttpServer::~HttpServer() = default;

void HttpServer::add_route(const std::string& path, RouteHandler handler) {
    impl_->add_route(path, std::move(handler));
}

void HttpServer::run() {
    impl_->run();
}

void HttpServer::stop() {
    impl_->stop();
}