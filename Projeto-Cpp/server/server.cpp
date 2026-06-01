#include "image_utils.hpp"
#include "recon.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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
#include <windows.h>
#include <psapi.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace {

// Estrutura que representa o JSON recebido do cliente.
struct RequestPayload {
    Vector g;
    Matrix h;
    std::map<std::string, double> params;
    int imageSize = 0;
    std::int64_t seed = 0;
};

// Metadados de uma execucao de reconstrucao, usados no JSON de resposta e no CSV.
struct ReconMeta {
    std::string algorithm;
    std::string method;
    std::string startIso;
    std::string endIso;
    std::int64_t startMs = 0;
    std::int64_t endMs = 0;
    std::int64_t elapsedMs = 0;
    double cpuSeconds = 0.0;
    double memoryMb = 0.0;
    int sizePixels = 0;
    int iterations = 0;
    double finalResNorm = 0.0;
    std::string imagePath;
};

// Inicializa e finaliza a API de sockets quando o programa roda no Windows.
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

// Fecha conexoes de forma portavel entre Windows e sistemas Unix.
void closeSocket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

// Retorna o tempo atual em milissegundos para calcular duracao das execucoes.
std::int64_t nowMs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// Retorna o instante atual em UTC no formato ISO-8601.
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

// Garante que a pasta de saida exista antes de gravar imagens e relatorios.
std::string ensureOutputDir() {
    const std::string out = "output";
    std::filesystem::create_directories(out);
    return out;
}

double processMemoryMb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return static_cast<double>(counters.WorkingSetSize) / 1024.0 / 1024.0;
    }
    return 0.0;
#else
    std::ifstream status("/proc/self/status");
    std::string label;
    while (status >> label) {
        if (label == "VmRSS:") {
            double kb = 0.0;
            status >> kb;
            return kb / 1024.0;
        }
        status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return 0.0;
#endif
}

// Escapa caracteres especiais antes de inserir strings em JSON.
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

// Parser JSON simples, suficiente para o formato gerado pelo cliente.
class PayloadParser {
public:
    explicit PayloadParser(const std::string& input) : input_(input) {}

    // Lê os campos conhecidos do payload e ignora campos extras.
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

    // Avanca por espacos, tabs e quebras de linha entre tokens JSON.
    void skipWhitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    // Tenta consumir um caractere esperado sem gerar erro se ele nao existir.
    bool tryConsume(char expected) {
        skipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    // Consome um caractere obrigatorio e falha se o JSON estiver fora do formato.
    void consume(char expected) {
        if (!tryConsume(expected)) {
            throw std::runtime_error(std::string("JSON invalido: esperado '") + expected + "'");
        }
    }

    // Lê strings JSON, incluindo os escapes mais comuns.
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

    // Converte o proximo token numerico para double.
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

    // Lê arrays numericos, usados principalmente para o vetor g.
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

    // Lê matrizes representadas como arrays de arrays numericos.
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

    // Lê o objeto params com parametros numericos dos algoritmos.
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

    // Ignora campos desconhecidos mantendo o parser alinhado no proximo token.
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

// Serializa os metadados de uma execucao para JSON.
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
        << "\"cpu_seconds\":" << std::setprecision(17) << meta.cpuSeconds << ','
        << "\"memory_mb\":" << std::setprecision(17) << meta.memoryMb << ','
        << "\"size_pixels\":" << meta.sizePixels << ','
        << "\"iterations\":" << meta.iterations << ','
        << "\"final_res_norm\":" << std::setprecision(17) << meta.finalResNorm << ','
        << "\"image_path\":\"" << jsonEscape(meta.imagePath) << "\""
        << '}';
    return out.str();
}

// Serializa todas as execucoes em um array JSON para responder ao cliente.
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

// Executa um dos metodos em C++, salva a imagem reconstruida e devolve os
// metadados da execucao.
ReconMeta runCpp(const RequestPayload& req, const std::string& method) {
    const std::string outDir = ensureOutputDir();
    const std::string startIso = isoNow();
    const std::int64_t startMs = nowMs();
    const std::clock_t cpuStart = std::clock();

    const ReconResult result = method == "CGNR"
                                   ? CGNR_Cpp(req.h, req.g, req.params)
                                   : CGNE_Cpp(req.h, req.g, req.params);

    const double cpuSeconds = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
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
    meta.cpuSeconds = cpuSeconds;
    meta.memoryMb = processMemoryMb();
    meta.sizePixels = req.imageSize;
    meta.iterations = result.iterations;
    meta.finalResNorm = result.finalResNorm;
    meta.imagePath = imagePath;
    saveJson(imagePath + ".json", meta);
    return meta;
}

// Acrescenta uma linha por execucao no relatorio comparativo em CSV.
void appendCsv(const std::string& path, const std::vector<ReconMeta>& metas) {
    const bool newFile = !std::filesystem::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        std::cerr << "erro abrir csv: " << path << '\n';
        return;
    }

    if (newFile) {
        out << "timestamp,algorithm,method,start_iso,end_iso,start_ms,end_ms,elapsed_ms,cpu_seconds,memory_mb,"
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
            << std::setprecision(17) << meta.cpuSeconds << ','
            << std::setprecision(17) << meta.memoryMb << ','
            << meta.sizePixels << ','
            << meta.iterations << ','
            << std::setprecision(17) << meta.finalResNorm << ','
            << meta.imagePath << '\n';
    }
}

void validatePayload(const RequestPayload& req) {
    if (req.h.empty() || req.h[0].empty()) {
        throw std::runtime_error("payload sem matriz H");
    }
    const std::size_t columns = req.h[0].size();
    for (const Vector& row : req.h) {
        if (row.size() != columns) {
            throw std::runtime_error("matriz H com linhas de tamanhos diferentes");
        }
    }
    if (req.g.size() != req.h.size()) {
        throw std::runtime_error("dimensoes invalidas: g deve ter o mesmo numero de valores que as linhas de H");
    }
    if (req.imageSize <= 0) {
        throw std::runtime_error("image_size invalido");
    }
}

std::filesystem::path findFromParents(const std::filesystem::path& relative) {
    std::filesystem::path base = std::filesystem::current_path();
    while (true) {
        const std::filesystem::path candidate = base / relative;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!base.has_parent_path() || base == base.parent_path()) {
            break;
        }
        base = base.parent_path();
    }
    throw std::runtime_error("arquivo nao encontrado: " + relative.string());
}

std::string quoteArg(const std::filesystem::path& path) {
    std::string text = std::filesystem::absolute(path).string();
    std::string quoted = "\"";
    for (const char ch : text) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += '"';
    return quoted;
}

std::string quoteTextArg(const std::string& text) {
    std::string quoted = "\"";
    for (const char ch : text) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += '"';
    return quoted;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("nao foi possivel ler arquivo: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::vector<std::string> pythonInterpreters() {
    std::vector<std::string> interpreters;
    if (const char* envPython = std::getenv("PYTHON")) {
        if (*envPython != '\0') {
            interpreters.push_back(quoteTextArg(envPython));
        }
    }
    interpreters.push_back("python");
#ifdef _WIN32
    interpreters.push_back("py -3");
#else
    interpreters.push_back("python3");
#endif
    return interpreters;
}

std::string runPython(const std::string& body, const std::string& outDir, const std::string& csvPath) {
    try {
        const std::filesystem::path script = findFromParents("Projeto-Python/Projeto.py");
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stamp).count();
        const std::filesystem::path requestPath = std::filesystem::path(outDir) / ("request_" + std::to_string(ns) + ".json");
        const std::filesystem::path resultPath = std::filesystem::path(outDir) / ("python_result_" + std::to_string(ns) + ".json");

        {
            std::ofstream request(requestPath);
            request << body;
        }

        const std::string arguments = " " + quoteArg(script)
            + " --input " + quoteArg(requestPath)
            + " --output-dir " + quoteArg(outDir)
            + " --append-csv " + quoteArg(csvPath)
            + " > " + quoteArg(resultPath);

        int status = 1;
        for (const std::string& interpreter : pythonInterpreters()) {
            const std::string command = interpreter + arguments;
            status = std::system(command.c_str());
            if (status == 0) {
                break;
            }
            std::cerr << "python command failed: " << interpreter << " status " << status << '\n';
        }
        if (status != 0) {
            std::cerr << "python reconstruction failed; install Python/NumPy or set PYTHON\n";
            std::filesystem::remove(requestPath);
            std::filesystem::remove(resultPath);
            return {};
        }

        std::string result = readTextFile(resultPath);
        std::filesystem::remove(requestPath);
        std::filesystem::remove(resultPath);
        return result;
    } catch (const std::exception& err) {
        std::cerr << "python reconstruction unavailable: " << err.what() << '\n';
        return {};
    }
}

std::string trimJsonArray(std::string text) {
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](unsigned char ch) {
                   return !std::isspace(ch);
               }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [](unsigned char ch) {
                   return !std::isspace(ch);
               }).base(),
               text.end());
    if (text.size() >= 2 && text.front() == '[' && text.back() == ']') {
        return text.substr(1, text.size() - 2);
    }
    return {};
}

std::string mergeJsonArrays(const std::string& first, const std::string& second) {
    const std::string firstItems = trimJsonArray(first);
    const std::string secondItems = trimJsonArray(second);
    if (firstItems.empty()) {
        return secondItems.empty() ? "[]" : "[" + secondItems + "]";
    }
    if (secondItems.empty()) {
        return "[" + firstItems + "]";
    }
    return "[" + firstItems + "," + secondItems + "]";
}

// Fluxo principal do endpoint: parseia o corpo, roda CGNE e CGNR, grava o CSV
// e devolve os metadados como JSON.
std::string reconstruct(const std::string& body) {
    const RequestPayload req = parsePayload(body);
    validatePayload(req);

    const std::string outDir = ensureOutputDir();
    const std::string csvPath = outDir + "/report_comparison.csv";
    std::vector<ReconMeta> results;
    results.push_back(runCpp(req, "CGNE"));
    results.push_back(runCpp(req, "CGNR"));
    appendCsv(csvPath, results);

    const std::string cppJson = metasToJson(results);
    const std::string pythonJson = runPython(body, outDir, csvPath);
    return mergeJsonArrays(cppJson, pythonJson);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

// Lê uma requisicao HTTP simples e separa headers e body respeitando Content-Length.
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

// Envia uma resposta HTTP completa para o cliente.
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

// Atende uma conexao: valida rota/metodo, executa a reconstrucao e responde.
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

        // Cria o socket TCP que ficara escutando requisicoes HTTP na porta 8080.
        socket_t server = socket(AF_INET, SOCK_STREAM, 0);
        if (server == static_cast<socket_t>(-1)) {
            throw std::runtime_error("nao foi possivel criar socket");
        }

        // Permite reiniciar o servidor sem aguardar a porta sair do estado TIME_WAIT.
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
        // Loop sequencial: aceita uma conexao, processa a requisicao e fecha o cliente.
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
