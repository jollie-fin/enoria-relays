#include "env.h"
#include <map>
#include <string>
#include <fstream>
#include <stdexcept>

static std::map<std::string, std::string, std::less<>> global_env;

namespace env
{
    static void read_line(std::map<std::string, std::string, std::less<>> &out, std::string_view line)
    {
        if (line.starts_with('#'))
            return;
        auto col1_start = 0;
        auto col1_end = line.find('=');
        if (col1_end == line.npos)
            return;
        auto col2_start = col1_end + 1;
        auto col2_end = line.npos;
        std::string line_str{line};
        out[line_str.substr(col1_start, col1_end - col1_start)] =
            line_str.substr(col2_start, col2_end - col2_start);
    }

    void read_envp(char **envp)
    {
        while (*envp)
        {
            std::string line{*envp};
            envp++;
            read_line(global_env, line);
        }
    }

    void read_envp(std::string_view path)
    {
        for (const auto &[key, value] : read_file(path))
            global_env[key] = value;
    }

    std::string_view get(std::string_view key, std::string_view def)
    {
        auto it = global_env.find(key);
        if (it == global_env.end())
            return def;
        else
            return it->second;
    }

    std::map<std::string, std::string, std::less<>> read_file(std::string_view path)
    {
        std::map<std::string, std::string, std::less<>> retval;
        auto file = std::ifstream(std::string{path});
        if (!file)
            throw std::runtime_error("File " + std::string{path} + " was not found");
        std::string line;
        while (std::getline(file, line))
            read_line(retval, line);

        return retval;
    }
}