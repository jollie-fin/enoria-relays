#include "log.h"
#include "env.h"
#include <iostream>

namespace Log
{
    static std::string get_label(int requested)
    {
        switch (requested)
        {
        case LEVEL_ERROR:
            return "ERROR";
        case LEVEL_WARNING:
            return "WARNING";
        case LEVEL_INFO:
            return "INFO";
        case LEVEL_DEBUG:
            return "DEBUG";
        default:
            return std::to_string(requested);
        }
    }

    std::ostream log(int requested, const std::source_location loc)
    {
        std::string fn = loc.file_name();
        auto end = fn.find('.');
        auto start = fn.rfind('/');
        if (start == std::string::npos)
            start = 0;
        else
            start++;
        auto category = fn.substr(start, end - start);

        std::string current_str{env::get("LOGLEVEL_" + category, std::to_string(LEVEL_INFO))};
        auto current = std::stoi(current_str);
        auto *retval = current >= requested ? std::cout.rdbuf() : nullptr;
        std::ostream stream{retval};
        stream << get_label(requested) << "[" << category << "]:";
        return std::ostream{retval};
    }
}