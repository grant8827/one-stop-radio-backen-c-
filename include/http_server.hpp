#pragma once
#include <string>
#include <functional>
#include <map>
#include <memory>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> params;
};

using RouteHandler = std::function<std::string(const HttpRequest&)>;

class HttpServer {
public:
    explicit HttpServer(int port);
    ~HttpServer();
    
    void add_route(const std::string& path, RouteHandler handler);
    void run();
    void stop();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};