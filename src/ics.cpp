#include <algorithm>
#include <cstdio>
#include <curl/curl.h>
#include <stdexcept>
#include <iostream>
#include <format>
#include <cstdlib>
#include "ics.h"
#include "utils.h"
#include <memory>
#include <array>
namespace ics
{
    static int64_t to_timestamp(std::string_view s, std::string_view timezone)
    {
        auto y = s.substr(0, 4);
        auto m = s.substr(4, 2);
        auto d = s.substr(6, 2);
        auto H = s.substr(9, 2);
        auto M = s.substr(11, 2);
        auto S = s.substr(13, 2);

        auto cmd = std::format("date --date='TZ=\"{}\" {}-{}-{}T{}:{}:{}' +%s",
                               timezone, y, m, d, H, M, S);
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)>
            pipe(popen(cmd.c_str(), "r"),
                 pclose);
        if (!pipe)
            throw std::runtime_error("popen() failed!");
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
            result += buffer.data();
        return std::stoll(result);
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
                current_vevent.start = to_timestamp(t.value, timezone);
            else if (t.key == "DTEND")
                current_vevent.end = to_timestamp(t.value, timezone);
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

    static size_t writeMemoryCallback(void *contents, size_t size, size_t nmemb,
                                      void *userp)
    {
        size_t realsize = size * nmemb;
        auto &mem = *static_cast<std::string *>(userp);
        mem.append(static_cast<char *>(contents), realsize);
        return realsize;
    }

    std::string download(std::string_view url)
    {
        if (url.starts_with("file://"))
        {
            return cat(std::string{url.substr(7)});
        }
        CURL *curl_handle;
        CURLcode res;

        std::string chunk;

        curl_global_init(CURL_GLOBAL_ALL);

        curl_handle = curl_easy_init();
        curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_URL, url.data());
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        // added options that may be required
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);  // redirects
        curl_easy_setopt(curl_handle, CURLOPT_HTTPPROXYTUNNEL, 1L); // corp. proxies etc.
        // curl_easy_setopt(curl_handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);

        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK)
        {
            throw std::runtime_error("Impossible to retrieve " + std::string{url} + " : " + curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return chunk;
    }

    events fetch_from_uri(std::string_view path)
    {
        auto t = download(path);
        auto tokens = split(t);
        return parse_events(tokens);
    }
}