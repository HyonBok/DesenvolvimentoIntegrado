#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
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
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
    int delayMs = 0;
};

// Arquivo de configuracao base
struct BaseConfig {
    std::string model = "2";
    std::int64_t seed = 0;
    std::string matrixRelative;
    int imageWidth = 30;
    int imageHeight = 30;
    int maxIter = 10;
    double tol = 0.0001;
    std::vector<SignalFile> signals;
};

// Opcoes do cliente(modelo)
struct ClientOptions {
    std::string model = "2";
};

// Modelo
struct ModelDefaults {
    std::string folderName;
    std::string matrixRelative;
    int imageWidth = 30;
    int imageHeight = 30;
    std::vector<std::string> signals;
};

// Opcoes de modelo pre-definidas
const ModelDefaults& defaultsForModel(const std::string& model) {
    static const ModelDefaults model1 = {
        "Dados Modelo 1",
        "Dados/Dados Modelo 1/H-1.csv/H-1.csv",
        60,
        60,
        {
            "Dados/Dados Modelo 1/G-1.csv",
            "Dados/Dados Modelo 1/G-2.csv",
            "Dados/Dados Modelo 1/A-60x60-1.csv",
        },
    };
    static const ModelDefaults model2 = {
        "Dados Modelo 2",
        "Dados/Dados Modelo 2/H-2.csv/H-2.csv",
        30,
        30,
        {
            "Dados/Dados Modelo 2/g-30x30-1.csv",
            "Dados/Dados Modelo 2/g-30x30-2.csv",
            "Dados/Dados Modelo 2/A-30x30-1.csv",
        },
    };

    if (model == "1") {
        return model1;
    }
    if (model == "2") {
        return model2;
    }
    throw std::runtime_error("modelo invalido. Use 1 ou 2.");
}

// Conversor CSV que trata virgula e ponto e virgula
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

// Procura arquivos de dados partindo do diretorio atual e subindo ate a raiz do projeto
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

// Remove espacos em branco do inicio e fim de uma string
std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Formata 
std::string normalizeToken(std::string value) {
    value = trim(value);
    std::string normalized;
    for (char ch : value) {
        if (ch == '_' || ch == ' ') {
            ch = '-';
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

// Garante modelo = 1 ou 2
std::string normalizeModel(const std::string& model) {
    const std::string value = normalizeToken(model);
    if (value == "1" || value == "2") {
        return value;
    }
    throw std::runtime_error("modelo invalido. Use 1 ou 2.");
}

// Argumentos para executar no cliente
ClientOptions parseClientOptions(int argc, char* argv[]) {
    ClientOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model") {
            if (i + 1 >= argc) {
                throw std::runtime_error("argumento sem valor: " + arg);
            }
            options.model = normalizeModel(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Uso: recon_client [--model 1|2]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("argumento desconhecido: " + arg);
        }
    }
    options.model = normalizeModel(options.model);
    return options;
}

// Caminho base
std::filesystem::path baseConfigPath(const std::string& model) {
    const ModelDefaults& defaults = defaultsForModel(model);
    return findFromParents(std::filesystem::path("Dados") / defaults.folderName) / "base_config.txt";
}

// Criar arquivo de configuracao base, caso nao exista
void createBaseConfig(const std::filesystem::path& path, const std::string& model) {
    const ModelDefaults& defaults = defaultsForModel(model);
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    const auto seed = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    std::mt19937_64 rng(static_cast<std::uint64_t>(seed));
    std::uniform_int_distribution<int> delayMs(250, 1000);

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("nao foi possivel criar arquivo base: " + path.string());
    }

    out << "seed=" << seed << '\n'
        << "matrix=" << defaults.matrixRelative << '\n'
        << "image_width=" << defaults.imageWidth << '\n'
        << "image_height=" << defaults.imageHeight << '\n'
        << "max_iter=10\n"
        << "tol=0.0001\n";
    for (std::size_t i = 0; i < defaults.signals.size(); ++i) {
        const int delay = i == 0 ? 0 : delayMs(rng);
        out << "signal=" << defaults.signals[i] << ",delay_ms=" << delay << '\n';
    }
}

// Analisa linha do arquivo de configuracao onde estiver o sinal
SignalFile parseSignalLine(const std::string& value) {
    SignalFile signal;
    const std::size_t comma = value.find(',');
    signal.relative = trim(comma == std::string::npos ? value : value.substr(0, comma));
    if (comma != std::string::npos) {
        const std::string rest = value.substr(comma + 1);
        const std::string prefix = "delay_ms=";
        const std::size_t delay = rest.find(prefix);
        if (delay != std::string::npos) {
            signal.delayMs = std::stoi(trim(rest.substr(delay + prefix.size())));
        }
    }
    signal.path = findFromParents(signal.relative);
    return signal;
}

// Carregar arquivo base
BaseConfig loadBaseConfig(const std::string& model) {
    const std::string normalizedModel = normalizeModel(model);
    const std::filesystem::path path = baseConfigPath(normalizedModel);
    if (!std::filesystem::exists(path)) {
        createBaseConfig(path, normalizedModel);
    }

    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("nao foi possivel abrir arquivo base: " + path.string());
    }

    BaseConfig config;
    config.model = normalizedModel;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "seed") {
            config.seed = std::stoll(value);
        } else if (key == "matrix") {
            config.matrixRelative = value;
        } else if (key == "image_width") {
            config.imageWidth = std::stoi(value);
        } else if (key == "image_height") {
            config.imageHeight = std::stoi(value);
        } else if (key == "max_iter") {
            config.maxIter = std::stoi(value);
        } else if (key == "tol") {
            config.tol = std::stod(value);
        } else if (key == "signal") {
            config.signals.push_back(parseSignalLine(value));
        }
    }

    if (config.seed == 0 || config.matrixRelative.empty() || config.signals.empty()) {
        throw std::runtime_error("arquivo base incompleto: " + path.string());
    }
    return config;
}

// Metodo auxiliar para leitura de arquivo CSV(vetor)
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

// Metodo auxiliar para leitura de arquivo CSV(matriz)
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

// Validar dimensoes
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

// Formata numero com precisao
void appendNumber(std::ostringstream& out, double value) {
    out << std::setprecision(17) << value;
}

// Evita caracteres especiais em JSON
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
                             const BaseConfig& config,
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

    out << "],\"params\":{\"max_iter\":" << config.maxIter << ",\"tol\":";
    appendNumber(out, config.tol);
    out << "},"
        << "\"image_size\":" << config.imageWidth * config.imageHeight << ','
        << "\"image_width\":" << config.imageWidth << ','
        << "\"image_height\":" << config.imageHeight << ','
        << "\"seed\":" << config.seed << ','
        << "\"meta\":{\"note\":\"Payload generated by C++ client from sample CSV data\","
        << "\"signal_path\":\"" << jsonEscape(signalPath) << "\","
        << "\"matrix_path\":\"" << jsonEscape(matrixPath) << "\"}}";
    return out.str();
}

// Envia todos os bytes da requisicao, pois send() pode transmitir apenas parte do buffer em uma chamada
// Ou seja -- loop ate que todo o conteudo seja enviado
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

// Abre uma conexao TCP, envia uma requisicao HTTP POST com JSON e retorna corpo
std::string postJson(const std::string& host, int port, const std::string& path, const std::string& body) {
    // Utilizando biblioteca ws2tcpip.h, cria socket e conecta ao servidor
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Resolve o host e porta para obter informacoes de conexao
    addrinfo* result = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0) {
        throw std::runtime_error("nao foi possivel resolver o host");
    }

    // Tenta as opcoes de endereco
    socket_t sock = -1;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        // Verifica se o socket foi criado com sucesso
        if (sock == static_cast<socket_t>(-1)) {
            continue;
        }
        // Tenta conectar
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

int main(int argc, char* argv[]) {
    try {
        SocketRuntime sockets;
        const ClientOptions options = parseClientOptions(argc, argv);

        BaseConfig config = loadBaseConfig(options.model);
        const auto hPath = findFromParents(config.matrixRelative);

        std::cout << "Carregando H de " << hPath.string() << '\n';
        const Matrix h = readMatrixCsv(hPath);

        std::cout << "Modelo: " << config.model << '\n';
        std::cout << "Usando arquivo base " << baseConfigPath(config.model).string() << '\n';
        std::cout << "Seed compartilhada: " << config.seed << '\n';

        for (std::size_t i = 0; i < config.signals.size(); ++i) {
            const auto& signal = config.signals[i];
            const auto& gPath = signal.path;
            std::cout << "Carregando sinal " << gPath.string() << '\n';
            const Vector g = readVectorCsv(gPath);
            validateDimensions(g, h, gPath);

            if (signal.delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(signal.delayMs));
            }

            const std::string payload = buildPayloadJson(g, h, config, signal.relative, config.matrixRelative);
            const std::string reply = postJson("localhost", 8080, "/reconstruct", payload);
            std::cout << "Server reply for " << gPath.filename().string() << ": " << reply << '\n';
        }
    } catch (const std::exception& err) {
        std::cerr << "Erro ao enviar ao servidor: " << err.what() << '\n';
        return 1;
    }

    return 0;
}
