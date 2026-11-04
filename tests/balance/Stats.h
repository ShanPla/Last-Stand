#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace ls::tools {

inline double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (const double v : values) sum += v;
    return sum / static_cast<double>(values.size());
}

// Percentile with linear interpolation. p is from 0 to 100. Takes a copy to sort.
inline double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = p / 100.0 * static_cast<double>(values.size() - 1);
    const size_t low = static_cast<size_t>(std::floor(position));
    const size_t high = static_cast<size_t>(std::ceil(position));
    return values[low] + (values[high] - values[low]) * (position - static_cast<double>(low));
}

inline double median(const std::vector<double>& values) { return percentile(values, 50.0); }

// Pearson correlation of two equal-length series. Returns NaN when either is constant.
inline double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    if (n < 2) return std::nan("");
    const double meanA = mean(a);
    const double meanB = mean(b);
    double covariance = 0.0;
    double varianceA = 0.0;
    double varianceB = 0.0;
    for (size_t i = 0; i < n; ++i) {
        covariance += (a[i] - meanA) * (b[i] - meanB);
        varianceA += (a[i] - meanA) * (a[i] - meanA);
        varianceB += (b[i] - meanB) * (b[i] - meanB);
    }
    if (varianceA < 1e-12 || varianceB < 1e-12) return std::nan("");
    return covariance / std::sqrt(varianceA * varianceB);
}

// printf into a std::string, so table code can build lines freely.
inline std::string format(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

// Seconds as m:ss.
inline std::string mmss(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    return format("%d:%02d", total / 60, total % 60);
}

}  // namespace ls::tools
