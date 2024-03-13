#include <iostream>

#include <sstream>
#include <curl/curl.h>
#include <string_view>

#include <chrono>
#include <unistd.h>
#include <algorithm>

#include "json.hpp"
#include "frisquetconnect.h"
#include "utils.h"
#include "date/date.h"
#include "date/tz.h"

using namespace std::chrono_literals;
using json = nlohmann::json;

constexpr auto AUTH_URL = "https://fcutappli.frisquet.com/api/v1/authentifications";
constexpr auto API_URL = "https://fcutappli.frisquet.com/api/v1/sites/";
constexpr auto ORDRES_URL = "https://fcutappli.frisquet.com/api/v1/ordres/";

constexpr auto TOKEN_RENEWAL_PERIOD = std::chrono::minutes{30};
constexpr auto INFOS_RENEWAL_PERIOD = std::chrono::minutes{5};

class RequestFailure : public std::exception
{
public:
    RequestFailure(std::string_view url, const json &payload)
    {
        reason_ = payload["message"];
        message_ = "Impossible to retrieve :\"";
        message_ += url;
        message_ += "\"; reason:";
        message_ += reason_;
    }
    virtual const char *what() const throw()
    {
        return message_.c_str();
    }

    std::string_view reason() const
    {
        return reason_;
    }

protected:
    std::string reason_;
    std::string message_;
};

FrisquetConnect::FrisquetConnect(std::string_view id)
{
    auto fields = split(id, ':');
    if (fields.size() != 4)
        throw std::runtime_error("Impossible to initialize frisquetconnect with " +
                                 std::string{id});
    email_ = fields[0];
    password_ = fields[1];
    chaudiere_ = fields[2];
    zone_ = fields[3];
    refresh();
}

static json json_request(std::string_view url,
                         const std::map<std::string_view, std::string_view> &headers,
                         const json &payload)
{
    auto result = download(url, headers, payload);
    auto retval = json::parse(result);
    if (retval.contains("message"))
    {
        throw RequestFailure(url, retval);
    }
    return json::parse(result);
}

static std::string get_token(std::string_view email, std::string_view password)
{
    json payload = {{"locale", "fr"}, {"email", email}, {"password", password}, {"type_client", "IOS"}};
    auto res = json_request(AUTH_URL, {}, payload);
    return res["token"];
}

void FrisquetConnect::refresh()
{
    auto now = std::chrono::steady_clock::now();
    if (now - token_time_ > TOKEN_RENEWAL_PERIOD)
    {
        token_time_ = now;
        token_ = get_token(email_, password_);
    }
    if (now - last_update_time_ > INFOS_RENEWAL_PERIOD)
    {
        last_update_time_ = now;
        token_ = get_token(email_, password_);
        std::ostringstream sstr;
        sstr << API_URL << chaudiere_ << "?token=" << token_;
        infos_ = json_request(sstr.str(), {}, {});
        display_alarm();
    }
}

int FrisquetConnect::get_mode(std::string_view zone) const
{
    for (const auto &info_zone : infos_["zones"])
    {
        if (info_zone["identifiant"] == zone)
            return info_zone["carac_zone"]["SELECTEUR"];
    }
    throw std::runtime_error("Impossible to find " + std::string{zone});
}

std::string FrisquetConnect::get_timezone() const
{
    return infos_["timezone"];
}

std::vector<std::string> FrisquetConnect::get_alarms() const
{
    std::vector<std::string> retval;
    for (const auto &i : infos_["alarmes"])
        retval.emplace_back(i["nom"]);
    for (const auto &i : infos_["alarmes_pro"])
        retval.emplace_back(i["nom"]);
    return retval;
}

void FrisquetConnect::display_alarm() const
{
    for (const auto &i : get_alarms())
        std::cout << chaudiere_ << ":" << zone_ << ":alarme:" << i << std::endl;
}

bool FrisquetConnect::is_boiler_connected() const
{
    for (const auto &i : get_alarms())
        if (i.starts_with("Box Frisquet Connect déconnectée"))
            return false;
    return true;
}

bool FrisquetConnect::get() const
{
    const auto *tz = date::locate_zone(get_timezone());

    auto now = tz->to_local(get_time_now());
    auto start_day = std::chrono::floor<std::chrono::days>(now);
    date::weekday wd{start_day};
    int day_of_week = wd.c_encoding(); // 0 = Sunday

    auto programmation_index = (now - start_day) / std::chrono::minutes{30};
    for (const auto &zone : infos_["zones"])
        if (zone["identifiant"] == zone_)
            for (const auto &day : zone["programmation"])
                if (day["jour"] == day_of_week)
                    return day["plages"][programmation_index] == 1;
    throw std::runtime_error("Impossible to find the right slot in programmation of FrisquetConnect");
}

void FrisquetConnect::pass_order(const std::map<std::string, json> &data) const
{
    auto url = std::string{ORDRES_URL} + chaudiere_ + "?token=aa" + token_;
    std::map<std::string_view, std::string_view> headers{
        {"Host", "fcutappli.frisquet.com"},
        {"Accept", "*/*"},
        {"User-Agent", "Frisquet Connect/2.5 (com.frisquetsa.connect; build:47; iOS 16.3.1) Alamofire/5.2.2"},
        {"Accept-Language", "en-FR;q=1.0, fr-FR;q=0.9"}};

    json payload;
    for (const auto &[key, value] : data)
        payload.emplace_back(json::object({{"cle", key}, {"valeur", value}}));

    std::cout << payload << std::endl;
    // std::cout << json_request(url, headers, payload) << std::endl;
}

// void FrisquetConnect::set(bool value)
// {
//     auto mode = value ? mode_e::CONFORT_PERMANENT : mode_e::REDUIT_PERMANENT;
//     mode = mode_e::AUTO;

//     pass_order({{"SELECTEUR_" + zone_, std::to_string(static_cast<int>(mode))}});
// }

void FrisquetConnect::set_programmation(int day, const program_day &pd) const
{
    day = day % 7;
    json sub_payload;
    sub_payload["jour"] = day;
    for (auto i : pd)
        sub_payload["plages"].emplace_back(static_cast<int>(i));
    json payload = json::array({sub_payload});
    pass_order({{"PROGRAMME_" + zone_, payload}});
}

void FrisquetConnect::set_programmation(const program_week &pw) const
{
    json payload;
    for (int day = 1; day <= 7; day++)
    {
        json day_payload;
        day_payload["jour"] = day % 7;
        for (auto i : pw[day - 1])
            day_payload["plages"].emplace_back(static_cast<int>(i));
        payload.emplace_back(day_payload);
    }
    pass_order({{"PROGRAMME_" + zone_, payload}});
}

void FrisquetConnect::update_events(const Database::events &events)
{
    const auto *tz = date::locate_zone(get_timezone());
    auto to_local = [&](auto tp)
    {
        return tz->to_local(tp);
    };

    auto now = to_local(get_time_now());
    auto start_day = std::chrono::floor<std::chrono::days>(now);
    date::weekday wd{start_day};
    int index_offset = (wd.c_encoding() + 6) % 7; // 0 = Monday
    program_week pw{};

    auto half_hour = std::chrono::duration<double>{30 * 60};

    for (const auto &event : events)
    {
        long start = std::floor((to_local(event.start) - start_day) / half_hour);
        long end = std::ceil((to_local(event.end) - start_day) / half_hour);

        for (int i = std::max(0l, start); i < std::min(end, 48l * 7l); i++)
            pw[(i / 48 + index_offset) % 7][i % 48] = true;
    }
    if (!is_boiler_connected())
        std::cout << "Boiler " << chaudiere_ << ":" << zone_ << " is not connected, programmation might be transmitted only later" << std::endl;
    set_programmation(pw);
}
