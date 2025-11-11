#pragma once

#include "stream_controller.hpp"
#include "http_server.hpp"
#include <memory>
#include <string>

namespace onestopradio {

class StreamControllerAPI {
public:
    explicit StreamControllerAPI(int port = 8083);
    ~StreamControllerAPI();

    bool Initialize(const std::string& config_file = "");
    void Run();
    void Stop();

private:
    // HTTP route handlers
    std::string HandleCreateStream(const HttpRequest& request);
    std::string HandleActivateStream(const HttpRequest& request);
    std::string HandleDeactivateStream(const HttpRequest& request);
    std::string HandleDeleteStream(const HttpRequest& request);
    std::string HandleUpdateStream(const HttpRequest& request);
    std::string HandleGetStreamStatus(const HttpRequest& request);
    std::string HandleGetAllStreams(const HttpRequest& request);
    std::string HandleUpdateMetadata(const HttpRequest& request);
    std::string HandleHealthCheck(const HttpRequest& request);
    std::string HandleReloadConfig(const HttpRequest& request);

    // Utility methods
    std::string CreateErrorResponse(const std::string& error, int code = 400);
    std::string CreateSuccessResponse(const std::string& message, const std::string& data = "");
    std::string StreamConfigToJson(const StreamConfig& config);
    std::string StreamStatsToJson(const StreamStats& stats);
    StreamConfig JsonToStreamConfig(const std::string& json);
    std::string ExtractStreamId(const std::string& path);

    std::unique_ptr<StreamController> stream_controller_;
    std::unique_ptr<HttpServer> http_server_;
    int port_;
    bool running_;
};

} // namespace onestopradio