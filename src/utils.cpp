#include "utils.h"
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <ctime>

#include <iostream>

int64_t get_time_now()
{
    std::time_t t = std::time(0); // get time now
    return t;
}

bool exists(const std::string &path)
{
    std::ifstream f{path};
    return f.good();
}

void echo(const std::string &path, std::string_view payload)
{
    std::ofstream file{path};
    file << payload;
}
std::string cat(const std::string &path)
{
    std::ifstream file{path};
    if (!file)
        throw std::runtime_error("Impossible to open " + path);
    std::stringstream retval;
    retval << file.rdbuf();
    return retval.str();
}

std::string convert_time(time_t timestamp)
{
    char buffer[64] = "";
    tm t = *localtime(&timestamp);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &t);
    return buffer;
}

std::vector<std::string_view> split(std::string_view str, char sep, size_t max_count)
{
    std::vector<std::string_view> retval;
    size_t start_position = 0;
    while (start_position != str.npos && retval.size() < max_count - 1)
    {
        size_t end_position = str.find(sep, start_position);
        retval.emplace_back(str.substr(start_position, end_position - start_position));
        start_position = end_position == str.npos ? end_position : end_position + 1;
    }
    if (start_position < str.npos)
    {
        retval.emplace_back(str.substr(start_position));
    }
    return retval;
}

std::pair<std::string_view, std::string_view> split2(std::string_view str, char sep)
{
    size_t start_position = 0;
    size_t end_position = str.find(start_position, sep);
    if (end_position == str.npos)
        return {str, ""};
    return {str.substr(start_position, end_position), str.substr(end_position + 1)};
}