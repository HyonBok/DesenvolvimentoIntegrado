#ifndef IMAGE_UTILS_HPP
#define IMAGE_UTILS_HPP

#include <string>
#include <vector>

using ImageMatrix = std::vector<std::vector<double>>;

bool saveImageFromMatrix(const ImageMatrix& matrix, const std::string& path);
bool saveImageFromVector(const std::vector<double>& values, const std::string& path, int sizePixels);

#endif
