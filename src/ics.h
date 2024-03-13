#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
#include "utils.h"
namespace ics
{
    struct vevent
    {
        timepoint start;
        timepoint end;
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
