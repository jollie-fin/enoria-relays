#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <chrono>
#include "json.hpp"
namespace chrono = std::chrono;
using timepoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;

std::string download(std::string_view url,
                     const std::map<std::string_view, std::string_view> &headers = {},
                     const nlohmann::json &payload = {});

int64_t to_timestamp(auto tp)
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               tp.time_since_epoch())
        .count();
}

timepoint from_timestamp(int64_t);
timepoint from_timestamp(std::string);

timepoint get_time_now();

bool exists(const std::string &path);
void echo(const std::string &path, std::string_view payload);
std::vector<std::string_view> split(std::string_view str, char sep, size_t max_count = std::string::npos);
std::pair<std::string_view, std::string_view> split2(std::string_view str, char sep);
std::string cat(const std::string &path);
