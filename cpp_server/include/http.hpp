#pragma once

#include <functional>
#include <map>
#include <string>

namespace chat {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::map<std::string, std::string> headers;
    std::string body;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest &)>;

class HttpServer {
public:
    explicit HttpServer(HttpHandler handler);
    bool listen(const std::string &host, int port);

private:
    void handle_client(int client_fd);

    HttpHandler handler_;
};

std::map<std::string, std::string> parse_query(const std::string &query);
std::string url_decode(const std::string &value);
std::string http_date();
std::string status_text(int status);

} // namespace chat
