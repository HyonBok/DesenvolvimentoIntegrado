#include "image_utils.hpp"
#include "recon.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace {

struct RequestPayload {
    Vector g;
    Matrix h;
    std::map<std::string, double> params;
    int imageSize = 0;
    std::int64_t seed = 0;
};

struct ReconMeta {
    std::string algorithm;
    std::string method;
    std::string startIso;
    std::string endIso;
    std::int64_t startMs = 0;
    std::int64_t endMs = 0;
    std::int64_t elapsedMs = 0;
    int sizePixels = 0;
    int iterations = 0;
    double finalResNorm = 0.0;
    std::string imagePath;
};

struct SocketRuntime {
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void closeSocket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

std::int64_t nowMs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string isoNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string ensureOutputDir() {
    const std::string out = "output";
    std::filesystem::create_directories(out);
    return out;
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream out;
    for (const char ch : text) {
        switch (ch) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

class PayloadParser {
public:
    explicit PayloadParser(const std::string& input) : input_(input) {}

    RequestPayload parse() {
        RequestPayload req;
        consume('{');
        skipWhitespace();
        if (tryConsume('}')) {
            return req;
        }

        while (true) {
            const std::string key = parseString();
            consume(':');
            if (key == "g") {
                req.g = parseNumberArray();
            } else if (key == "H") {
                req.h = parseMatrix();
            } else if (key == "params") {
                req.params = parseParams();
            } else if (key == "image_size") {
                req.imageSize = static_cast<int>(parseNumber());
            } else if (key == "seed") {
                req.seed = static_cast<std::int64_t>(parseNumber());
            } else {
                skipValue();
            }

            skipWhitespace();
            if (tryConsume('}')) {
                break;
            }
            consume(',');
        }

        return req;
    }

private:
    const std::string& input_;
    std::size_t pos_ = 0;

    void skipWhitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    bool tryConsume(char expected) {
        skipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void consume(char expected) {
        if (!tryConsume(expected)) {
            throw std::runtime_error(std::string("JSON invalido: esperado '") + expected + "'");
        }
    }

    std::string parseString() {
        consume('"');
        std::string text;
        while (pos_ < input_.size()) {
            const char ch = input_[pos_++];
            if (ch == '"') {
                return text;
            }
            if (ch != '\\') {
                text.push_back(ch);
                continue;
            }
            if (pos_ >= input_.size()) {
                throw std::runtime_error("escape JSON incompleto");
            }
            const char esc = input_[pos_++];
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                text.push_back(esc);
                break;
            case 'b':
                text.push_back('\b');
                break;
            case 'f':
                text.push_back('\f');
                break;
            case 'n':
                text.push_back('\n');
                break;
            case 'r':
                text.push_back('\r');
                break;
            case 't':
                text.push_back('\t');
                break;
            case 'u':
                if (pos_ + 4 > input_.size()) {
                    throw std::runtime_error("escape unicode JSON incompleto");
                }
                text.push_back('?');
                pos_ += 4;
                break;
            default:
                throw std::runtime_error("escape JSON invalido");
            }
        }
        throw std::runtime_error("string JSON sem fechamento");
    }

    double parseNumber() {
        skipWhitespace();
        const char* begin = input_.c_str() + pos_;
        char* end = nullptr;
        const double number = std::strtod(begin, &end);
        if (end == begin) {
            throw std::runtime_error("numero JSON invalido");
        }
        pos_ = static_cast<std::size_t>(end - input_.c_str());
        return number;
    }

    Vector parseNumberArray() {
        Vector values;
        consume('[');
        if (tryConsume(']')) {
            return values;
        }
        while (true) {
            values.push_back(parseNumber());
            if (tryConsume(']')) {
                break;
            }
            consume(',');
        }
        return values;
    }

    Matrix parseMatrix() {
        Matrix matrix;
        consume('[');
        if (tryConsume(']')) {
            return matrix;
        }
        while (true) {
            matrix.push_back(parseNumberArray());
            if (tryConsume(']')) {
                break;
            }
            consume(',');
        }
        return matrix;
    }

    std::map<std::string, double> parseParams() {
        std::map<std::string, double> params;
        consume('{');
        if (tryConsume('}')) {
            return params;
        }
        while (true) {
            const std::string key = parseString();
            consume(':');
            params[key] = parseNumber();
            if (tryConsume('}')) {
                break;
            }
            consume(',');
        }
        return params;
    }

    void skipValue() {
        skipWhitespace();
        if (pos_ >= input_.size()) {
            throw std::runtime_error("valor JSON ausente");
        }
        const char ch = input_[pos_];
        if (ch == '"') {
            parseString();
            return;
        }
        if (ch == '{') {
            consume('{');
            if (tryConsume('}')) {
                return;
            }
            while (true) {
                parseString();
                consume(':');
                skipValue();
                if (tryConsume('}')) {
                    return;
                }
                consume(',');
            }
        }
        if (ch == '[') {
            consume('[');
            if (tryConsume(']')) {
                return;
            }
            while (true) {
                skipValue();
                if (tryConsume(']')) {
                    return;
                }
                consume(',');
            }
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-') {
            parseNumber();
            return;
        }
        if (input_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return;
        }
        if (input_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return;
        }
        if (input_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return;
        }
        throw std::runtime_error("valor JSON invalido");
    }
};

RequestPayload parsePayload(const std::string& body) {
    return PayloadParser(body).parse();
}

std::string metaToJson(const ReconMeta& meta) {
    std::ostringstream out;
    out << '{'
        << "\"algorithm\":\"" << jsonEscape(meta.algorithm) << "\","
        << "\"method\":\"" << jsonEscape(meta.method) << "\","
        << "\"start_iso\":\"" << jsonEscape(meta.startIso) << "\","
        << "\"end_iso\":\"" << jsonEscape(meta.endIso) << "\","
        << "\"start_ms\":" << meta.startMs << ','
        << "\"end_ms\":" << meta.endMs << ','
        << "\"elapsed_ms\":" << meta.elapsedMs << ','
        << "\"size_pixels\":" << meta.sizePixels << ','
        << "\"iterations\":" << meta.iterations << ','
        << "\"final_res_norm\":" << std::setprecision(17) << meta.finalResNorm << ','
        << "\"image_path\":\"" << jsonEscape(meta.imagePath) << "\""
        << '}';
    return out.str();
}

std::string metasToJson(const std::vector<ReconMeta>& metas) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < metas.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << metaToJson(metas[i]);
    }
    out << ']';
    return out.str();
}

void saveJson(const std::string& path, const ReconMeta& meta) {
    std::ofstream out(path);
    out << metaToJson(meta) << '\n';
}

ReconMeta runCpp(const RequestPayload& req, const std::string& method) {
    const std::string outDir = ensureOutputDir();
    const std::string startIso = isoNow();
    const std::int64_t startMs = nowMs();

    const ReconResult result = method == "CGNR"
                                   ? CGNR_Cpp(req.h, req.g, req.params)
                                   : CGNE_Cpp(req.h, req.g, req.params);

    const std::int64_t endMs = nowMs();
    const std::string endIso = isoNow();
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stamp).count();
    const std::string imagePath = outDir + "/img_cpp_" + method + "_" + std::to_string(ns) + ".png";

    if (!saveImageFromVector(result.f, imagePath, req.imageSize)) {
        std::cerr << "erro salvar imagem C++: " << imagePath << '\n';
    }

    ReconMeta meta;
    meta.algorithm = "cpp";
    meta.method = method;
    meta.startIso = startIso;
    meta.endIso = endIso;
    meta.startMs = startMs;
    meta.endMs = endMs;
    meta.elapsedMs = endMs - startMs;
    meta.sizePixels = req.imageSize;
    meta.iterations = result.iterations;
    meta.finalResNorm = result.finalResNorm;
    meta.imagePath = imagePath;
    saveJson(imagePath + ".json", meta);
    return meta;
}

void appendCsv(const std::string& path, const std::vector<ReconMeta>& metas) {
    const bool newFile = !std::filesystem::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        std::cerr << "erro abrir csv: " << path << '\n';
        return;
    }

    if (newFile) {
        out << "timestamp,algorithm,method,start_iso,end_iso,start_ms,end_ms,elapsed_ms,"
               "size_pixels,iterations,final_res_norm,image_path\n";
    }

    const auto ts = std::chrono::high_resolution_clock::now().time_since_epoch();
    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(ts).count();
    for (const ReconMeta& meta : metas) {
        out << timestamp << ','
            << meta.algorithm << ','
            << meta.method << ','
            << meta.startIso << ','
            << meta.endIso << ','
            << meta.startMs << ','
            << meta.endMs << ','
            << meta.elapsedMs << ','
            << meta.sizePixels << ','
            << meta.iterations << ','
            << std::setprecision(17) << meta.finalResNorm << ','
            << meta.imagePath << '\n';
    }
}

std::string reconstruct(const std::string& body) {
    const RequestPayload req = parsePayload(body);
    std::vector<ReconMeta> results;
    results.push_back(runCpp(req, "CGNE"));
    results.push_back(runCpp(req, "CGNR"));
    appendCsv(ensureOutputDir() + "/report_comparison.csv", results);
    return metasToJson(results);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::pair<std::string, std::string> splitHeadersAndBody(socket_t client) {
    std::string request;
    char buffer[8192];
    std::size_t headerEnd = std::string::npos;

    while (headerEnd == std::string::npos) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("conexao fechada antes dos headers");
        }
        request.append(buffer, buffer + received);
        headerEnd = request.find("\r\n\r\n");
    }

    const std::string headers = request.substr(0, headerEnd);
    std::string body = request.substr(headerEnd + 4);
    std::size_t contentLength = 0;

    std::istringstream headerStream(headers);
    std::string line;
    std::getline(headerStream, line);
    while (std::getline(headerStream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string name = lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
        if (name == "content-length") {
            contentLength = static_cast<std::size_t>(std::stoull(value));
        }
    }

    while (body.size() < contentLength) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("conexao fechada antes do corpo completo");
        }
        body.append(buffer, buffer + received);
    }

    return {headers, body};
}

void sendResponse(socket_t client, int status, const std::string& statusText, const std::string& contentType, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << statusText << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    const std::string data = response.str();
    std::size_t sentTotal = 0;
    while (sentTotal < data.size()) {
        const int sent = send(client, data.data() + sentTotal, static_cast<int>(data.size() - sentTotal), 0);
        if (sent <= 0) {
            return;
        }
        sentTotal += static_cast<std::size_t>(sent);
    }
}

void handleClient(socket_t client) {
    try {
        const auto [headers, body] = splitHeadersAndBody(client);
        std::istringstream firstLine(headers);
        std::string method;
        std::string path;
        firstLine >> method >> path;

        if (method != "POST" || path != "/reconstruct") {
            sendResponse(client, 404, "Not Found", "text/plain", "not found\n");
            return;
        }

        const std::string responseBody = reconstruct(body);
        sendResponse(client, 200, "OK", "application/json", responseBody);
    } catch (const std::exception& err) {
        std::cerr << "request error: " << err.what() << '\n';
        sendResponse(client, 400, "Bad Request", "text/plain", "bad request\n");
    }
}

} // namespace

int main() {
    try {
        SocketRuntime sockets;

        socket_t server = socket(AF_INET, SOCK_STREAM, 0);
        if (server == static_cast<socket_t>(-1)) {
            throw std::runtime_error("nao foi possivel criar socket");
        }

        int yes = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(8080);

        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            closeSocket(server);
            throw std::runtime_error("nao foi possivel vincular a porta 8080");
        }

        if (listen(server, 16) != 0) {
            closeSocket(server);
            throw std::runtime_error("nao foi possivel iniciar listen");
        }

        std::cout << "Server listening on :8080\n";
        while (true) {
            sockaddr_in clientAddress{};
#ifdef _WIN32
            int addressLen = sizeof(clientAddress);
#else
            socklen_t addressLen = sizeof(clientAddress);
#endif
            socket_t client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &addressLen);
            if (client == static_cast<socket_t>(-1)) {
                continue;
            }
            handleClient(client);
            closeSocket(client);
        }
    } catch (const std::exception& err) {
        std::cerr << "Erro no servidor: " << err.what() << '\n';
        return 1;
    }
}
