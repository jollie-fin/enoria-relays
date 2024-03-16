#include <iostream>

#include <sstream>
#include <string_view>

#include <chrono>
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

constexpr auto TOKEN_RENEWAL_PERIOD = 30min;
constexpr auto INFOS_RENEWAL_PERIOD = 5min;

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
    auto now = chrono::steady_clock::now();
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

void FrisquetConnect::set_boiler_mode(mode_e mode)
{
    pass_order({{"SELECTEUR_" + zone_, static_cast<int>(mode)}});
}

FrisquetConnect::program_week FrisquetConnect::get_programmation_week() const
{
    for (const auto &info_zone : infos_["zones"])
    {
        if (info_zone["identifiant"] == zone_)
        {
            program_week retval;
            for (const auto &day : info_zone["programmation"])
            {
                int day_of_week = day["jour"];
                day_of_week = (day_of_week + 6) % 7;
                int i = 0;
                for (auto half_hour : day["plages"])
                    retval[day_of_week][i++] = static_cast<int>(half_hour);
            }
            return retval;
        }
    }
    throw std::runtime_error("Impossible to find " + std::string{zone_});
}

FrisquetConnect::program_day FrisquetConnect::get_programmation_day(int day_of_week) const
{
    return get_programmation_week()[day_of_week % 7];
}

FrisquetConnect::mode_e FrisquetConnect::get_boiler_mode() const
{
    for (const auto &info_zone : infos_["zones"])
    {
        if (info_zone["identifiant"] == zone_)
        {
            int selecteur = info_zone["carac_zone"]["SELECTEUR"];
            switch (selecteur)
            {
            case 5:
            case 6:
            case 7:
            case 8:
                return static_cast<mode_e>(selecteur);
            default:
                throw std::runtime_error(std::to_string(selecteur) + " is an unknown value for SELECTEUR");
            };
        }
    }
    throw std::runtime_error("Impossible to find " + std::string{zone_});
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

static auto decompose_now(std::string_view timezone)
{
    const auto *tz = date::locate_zone(timezone);
    auto to_local = [&](auto tp)
    {
        return tz->to_local(tp);
    };

    auto now = to_local(get_time_now());
    auto start_day = chrono::floor<chrono::days>(now);
    date::weekday wd{start_day};
    int day_of_week = (wd.c_encoding() + 6) % 7; // 0 = Monday
    int programmation_index = (now - start_day) / 30min;

    return std::make_tuple(start_day, day_of_week, programmation_index);
}

bool FrisquetConnect::get() const
{
    auto [start_day, day_of_week, programmation_index] = decompose_now(get_timezone());

    auto today = get_programmation_day(day_of_week);
    return today[programmation_index] == 1;
}

void FrisquetConnect::pass_order(const std::map<std::string, json> &data) const
{
    auto url = std::string{ORDRES_URL} + chaudiere_ + "?token=" + token_;
    std::map<std::string_view, std::string_view> headers{
        {"Host", "fcutappli.frisquet.com"},
        {"Accept", "*/*"},
        {"User-Agent", "Frisquet Connect/2.5 (com.frisquetsa.connect; build:47; iOS 16.3.1) Alamofire/5.2.2"},
        {"Accept-Language", "en-FR;q=1.0, fr-FR;q=0.9"}};

    json payload;
    for (const auto &[key, value] : data)
    {
        json stringified = value.dump();
        payload.emplace_back(json::object({{"cle", key}, {"valeur", stringified}}));
    }

    // std::cout << payload[0].dump(4) << std::endl;
    std::cout << json_request(url, headers, payload) << std::endl;
}

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
    auto [start_day, day_of_week, programmation_index] = decompose_now(get_timezone());
    const auto *tz = date::locate_zone(get_timezone());

    program_week pw{};

    auto compute_index = [&](auto tp)
    {
        return (tz->to_local(tp) - start_day) / 30.min;
    };

    for (const auto &event : events)
    {
        long start = std::floor(compute_index(event.heat_start));
        long end = std::ceil(compute_index(event.heat_end));

        for (int i = std::max(0l, start); i < std::min(end, 48l * 7l); i++)
            pw[(i / 48 + day_of_week) % 7][i % 48] = true;
    }
    if (!is_boiler_connected())
        std::cout << "Boiler " << chaudiere_ << ":" << zone_ << " is not connected, programmation might be transmitted only later" << std::endl;
    set_programmation(pw);
}
