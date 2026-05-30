#ifndef RECON_HPP
#define RECON_HPP

#include <map>
#include <string>
#include <vector>

using Matrix = std::vector<std::vector<double>>;
using Vector = std::vector<double>;

struct ReconResult {
    Vector f;
    int iterations = 0;
    double finalResNorm = 0.0;
};

ReconResult CGNR_Cpp(const Matrix& h, const Vector& g, const std::map<std::string, double>& params);
ReconResult CGNE_Cpp(const Matrix& h, const Vector& g, const std::map<std::string, double>& params);
void printUsage(int iteration, double elapsedSeconds);

#endif
