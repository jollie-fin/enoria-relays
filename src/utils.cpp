#include "utils.h"
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <curl/curl.h>
#include <string>
#include <iostream>

using json = nlohmann::json;

static size_t writeMemoryCallback(void *contents, size_t size, size_t nmemb,
                                    void *userp)
{
    size_t realsize = size * nmemb;
    auto &mem = *static_cast<std::string *>(userp);
    mem.append(static_cast<char *>(contents), realsize);
    return realsize;
}

std::string download(std::string_view url,
                  const std::map<std::string_view, std::string_view> &headers,
                  const json &payload)
{
    CURL *curl_handle;
    CURLcode res;

    std::string result;
    std::string payload_str;

    struct curl_slist *curl_headers = NULL;

    curl_global_init(CURL_GLOBAL_ALL);

    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.data());
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    // added options that may be required
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);  // redirects
    curl_easy_setopt(curl_handle, CURLOPT_HTTPPROXYTUNNEL, 1L); // corp. proxies etc.
    // curl_easy_setopt(curl_handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);

    if (!headers.empty())
    {
        for (const auto &[key, value] : headers)
        {
            std::ostringstream sstr;
            sstr << key << ": " << value;
            curl_headers = curl_slist_append(curl_headers, sstr.str().c_str());
        }
    }
    if (!payload.empty())
    {
        curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");
        std::ostringstream sstr;
        sstr << payload;
        payload_str = sstr.str();
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload_str.c_str());
    }
    if (curl_headers)
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, curl_headers);

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK)
        throw std::runtime_error(
            "Impossible to retrieve " + 
            std::string{url} + 
            " : " + 
            curl_easy_strerror(res));

    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    return result;
}

timepoint get_time_now()
{
    return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

timepoint from_timestamp(int64_t timestamp)
{
    return timepoint{std::chrono::seconds{timestamp}};
}

timepoint from_timestamp(std::string timestamp)
{
    return from_timestamp(std::stoll(timestamp));
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
    size_t end_position = str.find(sep);

    if (end_position == str.npos)
        return {str, ""};
    return {str.substr(start_position, end_position), str.substr(end_position + 1)};
}