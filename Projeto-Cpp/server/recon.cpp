#include "recon.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <string>
#endif

namespace {

double dot(const Vector& a, const Vector& b) {
    double sum = 0.0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

Vector matVec(const Matrix& a, const Vector& x) {
    Vector y(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        double sum = 0.0;
        const std::size_t n = std::min(a[i].size(), x.size());
        for (std::size_t j = 0; j < n; ++j) {
            sum += a[i][j] * x[j];
        }
        y[i] = sum;
    }
    return y;
}

Matrix transpose(const Matrix& a) {
    if (a.empty()) {
        return {};
    }

    Matrix b(a[0].size(), Vector(a.size(), 0.0));
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < a[i].size(); ++j) {
            b[j][i] = a[i][j];
        }
    }
    return b;
}

double norm2sq(const Vector& a) {
    double sum = 0.0;
    for (const double value : a) {
        sum += value * value;
    }
    return sum;
}

int maxIterations(const std::map<std::string, double>& params) {
    const auto it = params.find("max_iter");
    return it == params.end() ? 10 : static_cast<int>(it->second);
}

double tolerance(const std::map<std::string, double>& params) {
    const auto it = params.find("tol");
    return it == params.end() ? 1e-4 : it->second;
}

double memoryMb() {
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

} // namespace

void printUsage(int iteration, double elapsedSeconds) {
    std::cout << "\n--- Monitoramento Iteracao " << iteration << " ---\n";
    std::cout << "Memoria em uso: " << memoryMb() << " MB\n";
    std::cout << "Tempo decorrido: " << elapsedSeconds << " s\n";
}

ReconResult CGNR_Cpp(const Matrix& h, const Vector& g, const std::map<std::string, double>& params) {
    const std::size_t m = h.size();
    const std::size_t n = h.empty() ? 0 : h[0].size();
    Vector f(n, 0.0);
    const Matrix ht = transpose(h);
    Vector r = g;
    Vector z = matVec(ht, r);
    Vector p = z;

    const int maxIter = maxIterations(params);
    const double tol = tolerance(params);
    double prevNorm = norm2sq(r);
    double finalNorm = 0.0;
    int iter = 0;
    const auto started = std::chrono::steady_clock::now();

    for (int i = 0; i < maxIter; ++i) {
        iter = i + 1;
        if (i == maxIter / 2) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            printUsage(i, elapsed);
        }

        const Vector wi = matVec(h, p);
        const double numer = norm2sq(z);
        const double denom = norm2sq(wi);
        if (denom == 0.0) {
            break;
        }

        const double alpha = numer / denom;
        for (std::size_t j = 0; j < n; ++j) {
            f[j] += alpha * p[j];
        }
        for (std::size_t j = 0; j < m; ++j) {
            r[j] -= alpha * wi[j];
        }

        Vector zNext = matVec(ht, r);
        const double beta = numer == 0.0 ? 0.0 : norm2sq(zNext) / numer;
        for (std::size_t j = 0; j < n; ++j) {
            p[j] = zNext[j] + beta * p[j];
        }

        z = std::move(zNext);
        const double currNorm = norm2sq(r);
        const double eps = currNorm - prevNorm;
        prevNorm = currNorm;
        finalNorm = std::sqrt(currNorm);
        if (std::abs(eps) < tol) {
            break;
        }
    }

    return {f, iter, finalNorm};
}

ReconResult CGNE_Cpp(const Matrix& h, const Vector& g, const std::map<std::string, double>& params) {
    const std::size_t m = h.size();
    const std::size_t n = h.empty() ? 0 : h[0].size();
    Vector f(n, 0.0);
    Vector r = g;
    const Matrix ht = transpose(h);
    Vector p = matVec(ht, r);

    const int maxIter = maxIterations(params);
    const double tol = tolerance(params);
    double prevNorm = norm2sq(r);
    double finalNorm = 0.0;
    int iter = 0;
    const auto started = std::chrono::steady_clock::now();

    for (int i = 0; i < maxIter; ++i) {
        iter = i + 1;
        if (i == maxIter / 2) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            printUsage(i, elapsed);
        }

        const double rTr = dot(r, r);
        const double pTp = dot(p, p);
        if (pTp == 0.0) {
            break;
        }

        const double alpha = rTr / pTp;
        for (std::size_t j = 0; j < n; ++j) {
            f[j] += alpha * p[j];
        }

        const Vector hp = matVec(h, p);
        for (std::size_t j = 0; j < m; ++j) {
            r[j] -= alpha * hp[j];
        }

        const double rTrNew = dot(r, r);
        const double beta = rTr == 0.0 ? 0.0 : rTrNew / rTr;
        const Vector htR = matVec(ht, r);
        for (std::size_t j = 0; j < n; ++j) {
            p[j] = htR[j] + beta * p[j];
        }

        const double currNorm = norm2sq(r);
        const double eps = currNorm - prevNorm;
        prevNorm = currNorm;
        finalNorm = std::sqrt(currNorm);
        if (std::abs(eps) < tol) {
            break;
        }
    }

    return {f, iter, finalNorm};
}
