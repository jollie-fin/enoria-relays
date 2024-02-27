#pragma once
#include <string>
#include <string_view>
#include <map>

namespace env
{
    void read_envp(char **envp);
    void read_envp(std::string_view path);
    std::string_view get(std::string_view key, std::string_view def = "");
    std::map<std::string, std::string, std::less<>> read_file(std::string_view path);
}
