#include "http.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace chat {

namespace {

std::string trim(const std::string &value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool read_request(int client_fd, std::string &raw) {
    char buffer[4096];
    size_t header_end = std::string::npos;
    size_t content_length = 0;

    while (true) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) return false;
        raw.append(buffer, static_cast<size_t>(n));
        header_end = raw.find("\r\n\r\n");
        if (header_end == std::string::npos) continue;

        std::string headers = raw.substr(0, header_end);
        std::istringstream in(headers);
        std::string line;
        std::getline(in, line);
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = lower(trim(line.substr(0, colon)));
            std::string value = trim(line.substr(colon + 1));
            if (name == "content-length") {
                content_length = static_cast<size_t>(std::stoul(value));
            }
        }
        size_t total = header_end + 4 + content_length;
        while (raw.size() < total) {
            n = recv(client_fd, buffer, sizeof(buffer), 0);
            if (n <= 0) return false;
            raw.append(buffer, static_cast<size_t>(n));
        }
        return true;
    }
}

HttpRequest parse_request(const std::string &raw) {
    HttpRequest request;
    size_t header_end = raw.find("\r\n\r\n");
    std::string header_block = raw.substr(0, header_end);
    request.body = header_end == std::string::npos ? "" : raw.substr(header_end + 4);

    std::istringstream in(header_block);
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream first(line);
    std::string target;
    first >> request.method >> target;
    auto question = target.find('?');
    request.path = question == std::string::npos ? target : target.substr(0, question);
    request.query = question == std::string::npos ? "" : target.substr(question + 1);

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        request.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return request;
}

void send_all(int fd, const std::string &data) {
    const char *ptr = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (sent <= 0) return;
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
}

} // namespace

HttpServer::HttpServer(HttpHandler handler) : handler_(std::move(handler)) {}

bool HttpServer::listen(const std::string &host, int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket() failed\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid host: " << host << "\n";
        close(server_fd);
        return false;
    }

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        close(server_fd);
        return false;
    }
    if (::listen(server_fd, 128) < 0) {
        std::cerr << "listen() failed\n";
        close(server_fd);
        return false;
    }

    std::cout << "C++ chat server listening on http://" << host << ":" << port << "\n";
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;
        std::thread(&HttpServer::handle_client, this, client_fd).detach();
    }
}

void HttpServer::handle_client(int client_fd) {
    std::string raw;
    HttpResponse response;
    try {
        if (!read_request(client_fd, raw)) {
            close(client_fd);
            return;
        }
        HttpRequest request = parse_request(raw);
        if (request.method == "OPTIONS") {
            response.status = 204;
        } else {
            response = handler_(request);
        }
    } catch (const std::exception &exc) {
        response.status = 500;
        response.body = std::string("{\"detail\":\"") + exc.what() + "\"}";
    }

    response.headers["Access-Control-Allow-Origin"] = "*";
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, PATCH, DELETE, OPTIONS";
    response.headers["Connection"] = "close";
    response.headers["Date"] = http_date();
    response.headers["Content-Type"] = response.content_type;
    response.headers["Content-Length"] = std::to_string(response.body.size());

    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << ' ' << status_text(response.status) << "\r\n";
    for (const auto &[key, value] : response.headers) {
        out << key << ": " << value << "\r\n";
    }
    out << "\r\n" << response.body;
    send_all(client_fd, out.str());
    close(client_fd);
}

std::map<std::string, std::string> parse_query(const std::string &query) {
    std::map<std::string, std::string> values;
    size_t start = 0;
    while (start <= query.size()) {
        size_t amp = query.find('&', start);
        std::string part = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!part.empty()) {
            auto eq = part.find('=');
            std::string key = url_decode(eq == std::string::npos ? part : part.substr(0, eq));
            std::string value = eq == std::string::npos ? "" : url_decode(part.substr(eq + 1));
            values[key] = value;
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return values;
}

std::string url_decode(const std::string &value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            std::string hex = value.substr(i + 1, 2);
            char *end = nullptr;
            long ch = std::strtol(hex.c_str(), &end, 16);
            if (end == hex.c_str() + 2) {
                out.push_back(static_cast<char>(ch));
                i += 2;
            } else {
                out.push_back(value[i]);
            }
        } else if (value[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string http_date() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    return out.str();
}

std::string status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

} // namespace chat
