#pragma once
#include <string>
#include <vector>
#include <string_view>

int64_t get_time_now();
bool exists(const std::string &path);
void echo(const std::string &path, std::string_view payload);
std::vector<std::string_view> split(std::string_view str, char sep, size_t max_count = std::string::npos);
std::pair<std::string_view, std::string_view> split2(std::string_view str, char sep);
std::string cat(const std::string &path);
std::string convert_time(time_t timestamp);
