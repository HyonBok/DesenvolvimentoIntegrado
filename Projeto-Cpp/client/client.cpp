#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace {

// Garante que a biblioteca de sockets seja inicializada no Windows e liberada
// automaticamente ao fim do programa. Em Linux/macOS nao ha inicializacao extra.
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

// Fecha o socket usando a chamada correta para cada sistema operacional.
void closeSocket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

using Matrix = std::vector<std::vector<double>>;
using Vector = std::vector<double>;

struct SignalFile {
    std::string relative;
    std::filesystem::path path;
};

std::vector<double> parseCsvNumbers(const std::string& line) {
    std::vector<double> values;
    std::string normalized = line;
    for (char& ch : normalized) {
        if (ch == ';') {
            ch = ',';
        }
    }

    std::stringstream row(normalized);
    std::string cell;
    while (std::getline(row, cell, ',')) {
        if (cell.empty()) {
            continue;
        }
        values.push_back(std::stod(cell));
    }
    return values;
}

// Procura arquivos de dados partindo do diretorio atual e subindo ate a raiz
// do projeto. Isso permite executar o binario pela pasta client, server ou build.
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

Vector readVectorCsv(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("nao foi possivel abrir vetor: " + path.string());
    }

    Vector values;
    std::string line;
    while (std::getline(in, line)) {
        const Vector row = parseCsvNumbers(line);
        values.insert(values.end(), row.begin(), row.end());
    }
    return values;
}

Matrix readMatrixCsv(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("nao foi possivel abrir matriz: " + path.string());
    }

    Matrix matrix;
    std::string line;
    std::size_t columns = 0;
    while (std::getline(in, line)) {
        Vector row = parseCsvNumbers(line);
        if (row.empty()) {
            continue;
        }
        if (columns == 0) {
            columns = row.size();
        } else if (row.size() != columns) {
            throw std::runtime_error("matriz com quantidade inconsistente de colunas: " + path.string());
        }
        matrix.push_back(std::move(row));
    }
    return matrix;
}

void validateDimensions(const Vector& g, const Matrix& h, const std::filesystem::path& gPath) {
    if (h.empty() || h[0].empty()) {
        throw std::runtime_error("matriz H vazia");
    }
    if (g.size() != h.size()) {
        std::ostringstream err;
        err << "dimensoes incompativeis para " << gPath.string()
            << ": g tem " << g.size() << " valores, mas H tem " << h.size() << " linhas";
        throw std::runtime_error(err.str());
    }
}

void appendNumber(std::ostringstream& out, double value) {
    out << std::setprecision(17) << value;
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

// Monta manualmente o JSON enviado ao endpoint /reconstruct.
std::string buildPayloadJson(const Vector& g,
                             const Matrix& h,
                             int imageSize,
                             std::int64_t seed,
                             const std::string& signalPath,
                             const std::string& matrixPath) {
    std::ostringstream out;
    out << "{\"g\":[";
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        appendNumber(out, g[i]);
    }

    out << "],\"H\":[";
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << '[';
        for (std::size_t j = 0; j < h[i].size(); ++j) {
            if (j != 0) {
                out << ',';
            }
            appendNumber(out, h[i][j]);
        }
        out << ']';
    }

    out << "],\"params\":{\"max_iter\":10,\"tol\":0.0001},"
        << "\"image_size\":" << imageSize << ','
        << "\"seed\":" << seed << ','
        << "\"meta\":{\"note\":\"Payload generated by C++ client from sample CSV data\","
        << "\"signal_path\":\"" << jsonEscape(signalPath) << "\","
        << "\"matrix_path\":\"" << jsonEscape(matrixPath) << "\"}}";
    return out.str();
}

// Envia todos os bytes da requisicao, pois send() pode transmitir apenas parte
// do buffer em uma chamada.
bool sendAll(socket_t sock, const std::string& data) {
    std::size_t total = 0;
    while (total < data.size()) {
        const int sent = send(sock, data.data() + total, static_cast<int>(data.size() - total), 0);
        if (sent <= 0) {
            return false;
        }
        total += static_cast<std::size_t>(sent);
    }
    return true;
}

// Abre uma conexao TCP, envia uma requisicao HTTP POST com JSON e retorna
// apenas o corpo da resposta.
std::string postJson(const std::string& host, int port, const std::string& path, const std::string& body) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0) {
        throw std::runtime_error("nao foi possivel resolver o host");
    }

    socket_t sock = -1;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == static_cast<socket_t>(-1)) {
            continue;
        }
        if (connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
            break;
        }
        closeSocket(sock);
        sock = -1;
    }
    freeaddrinfo(result);

    if (sock == static_cast<socket_t>(-1)) {
        throw std::runtime_error("nao foi possivel conectar ao servidor");
    }

    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n"
            << "Host: " << host << ':' << port << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body;

    if (!sendAll(sock, request.str())) {
        closeSocket(sock);
        throw std::runtime_error("erro ao enviar dados ao servidor");
    }

    std::string response;
    char buffer[8192];
    while (true) {
        const int received = recv(sock, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        response.append(buffer, buffer + received);
    }
    closeSocket(sock);

    const std::size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return response;
    }
    return response.substr(headerEnd + 4);
}

} // namespace

int main() {
    try {
        SocketRuntime sockets;

        const std::string hRelative = "Dados/Dados Modelo 2/H-2.csv/H-2.csv";
        const auto hPath = findFromParents(hRelative);
        const std::vector<SignalFile> signalFiles = {
            {"Dados/Dados Modelo 2/g-30x30-1.csv", findFromParents("Dados/Dados Modelo 2/g-30x30-1.csv")},
            {"Dados/Dados Modelo 2/g-30x30-2.csv", findFromParents("Dados/Dados Modelo 2/g-30x30-2.csv")},
        };

        std::cout << "Carregando H de " << hPath.string() << '\n';
        const Matrix h = readMatrixCsv(hPath);
        const int imageSize = static_cast<int>(h[0].size());

        // A seed controla apenas os intervalos entre os sinais enviados.
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
        const auto seed = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        std::mt19937_64 rng(static_cast<std::uint64_t>(seed));
        std::uniform_int_distribution<int> delayMs(250, 1000);

        const std::string sequenceFile = "signal_sequence_" + std::to_string(seed) + ".txt";
        std::ofstream sequence(sequenceFile);
        sequence << "seed=" << seed << '\n';
        sequence << "matrix=" << hPath.string() << '\n';

        for (std::size_t i = 0; i < signalFiles.size(); ++i) {
            const auto& signal = signalFiles[i];
            const auto& gPath = signal.path;
            std::cout << "Carregando sinal " << gPath.string() << '\n';
            const Vector g = readVectorCsv(gPath);
            validateDimensions(g, h, gPath);

            const int waitMs = i == 0 ? 0 : delayMs(rng);
            if (waitMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            }

            sequence << "signal=" << gPath.string() << ",delay_ms=" << waitMs << '\n';
            const std::string payload = buildPayloadJson(g, h, imageSize, seed, signal.relative, hRelative);
            const std::string reply = postJson("localhost", 8080, "/reconstruct", payload);
            std::cout << "Server reply for " << gPath.filename().string() << ": " << reply << '\n';
        }

        std::cout << "Sequencia registrada em " << sequenceFile << '\n';
    } catch (const std::exception& err) {
        std::cerr << "Erro ao enviar ao servidor: " << err.what() << '\n';
        return 1;
    }

    return 0;
}
