#include <algorithm>
#include <cstdio>
#include <curl/curl.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#ifndef __cpp_lib_format
// std::format polyfill using fmtlib
#include <fmt/core.h>
namespace std
{
    using fmt::format;
}
#else
#include <format>
#endif
// To have chrono::parse
#include "date/date.h"
#include "date/tz.h"
#include <sstream>
#include "ics.h"
#include "utils.h"

namespace ics
{
    static timepoint parse_tp(std::string_view s, std::string_view timezone)
    {
        std::istringstream sstr{std::string{s}};
        date::local_seconds local_tp;
        sstr >> date::parse(std::string{"%Y%m%dT%H%M%S"}, local_tp);

        const auto *zone = date::locate_zone(timezone);
        return zone->to_sys(local_tp);
    }

    struct token
    {
        std::string key;
        std::string value;

        token(std::string_view key, std::string_view value) : key(key), value(value) {}
    };

    using tokens = std::vector<token>;

    events parse_events(const tokens &tok)
    {
        events retval;
        std::string timezone;
        vevent current_vevent;
        for (const auto &t : tok)
        {
            if (t.key == "NAME" && retval.paroisse == "")
                retval.paroisse = t.value;
            else if (t.key == "TZID")
                timezone = t.value;
            else if (t.key == "BEGIN" && t.value == "VEVENT")
                current_vevent = vevent{};
            else if (t.key == "END" && t.value == "VEVENT")
                retval.events.emplace_back(current_vevent);
            else if (t.key == "DTSTART")
                current_vevent.start = parse_tp(t.value, timezone);
            else if (t.key == "DTEND")
                current_vevent.end = parse_tp(t.value, timezone);
            else if (t.key == "LOCATION")
                current_vevent.location = t.value;
            else if (t.key == "SUMMARY")
                current_vevent.summary = t.value;
            else if (t.key == "STATUS")
                current_vevent.status = t.value;
        }
        return retval;
    }

    tokens split(std::string_view input)
    {
        tokens retval;
        size_t begin_key = 0;
        while (begin_key != input.npos)
        {
            if (input[begin_key] == ' ') // handling multiline
            {
                size_t tail_value = begin_key + 1;
                size_t end_value = input.find('\n', tail_value);
                size_t next = end_value == input.npos ? end_value : end_value + 1;
                if (input[end_value - 1] == '\r')
                    end_value--;

                retval.back().value += input.substr(tail_value, end_value - tail_value);
                begin_key = next;
                continue;
            }
            size_t end_key = input.find(':', begin_key);
            if (end_key == input.npos)
                break;

            size_t begin_value = end_key + 1;
            size_t end_value = input.find('\n', begin_value);
            size_t next = end_value == input.npos ? end_value : end_value + 1;
            if (input[end_value - 1] == '\r')
                end_value--;
            auto key = input.substr(begin_key, end_key - begin_key);
            auto value = input.substr(begin_value, end_value - begin_value);
            retval.emplace_back(key, value);
            begin_key = next;
        }
        return retval;
    }

    events fetch_from_uri(std::string_view path)
    {
        auto t = path.starts_with("file://")
                 ? cat(std::string{path.substr(7)})
                 : download(path);
        auto tokens = split(t);
        return parse_events(tokens);
    }
}