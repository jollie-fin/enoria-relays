#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <string_view>

namespace ics
{
    struct vevent
    {
        int64_t start;
        int64_t end;
        std::string summary;
        std::string status;
        std::string location;
    };

    struct events
    {
        std::string paroisse;
        std::vector<vevent> events;
    };

    events fetch_from_uri(std::string_view path);
}
