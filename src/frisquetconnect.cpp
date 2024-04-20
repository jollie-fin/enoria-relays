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

#include "log.h"

using namespace std::chrono_literals;
using json = nlohmann::json;

constexpr auto AUTH_URL = "https://fcutappli.frisquet.com/api/v1/authentifications";
constexpr auto API_URL = "https://fcutappli.frisquet.com/api/v1/sites/";
constexpr auto ORDRES_URL = "https://fcutappli.frisquet.com/api/v1/ordres/";

constexpr auto INFOS_RENEWAL_PERIOD = 24h;
constexpr auto TOKEN_RENEWAL_PERIOD = 6h;

static auto sc_now()
{
    return chrono::steady_clock::now();
}

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
    : last_infos_update_time_(sc_now() - 2 * INFOS_RENEWAL_PERIOD)
{
    TRACE_CALL();
    DEBUG << id << std::endl;

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
    TRACE_CALL();
    auto raw = download(url, headers, payload);
    DEBUG << raw << std::endl;

    auto retval = json::parse(raw);
    if (retval.contains("message"))
    {
        throw RequestFailure(url, retval);
    }

    return retval;
}

static std::string get_new_token(std::string_view email, std::string_view password)
{
    json payload = {{"locale", "fr"}, {"email", email}, {"password", password}, {"type_client", "IOS"}};
    auto res = json_request(AUTH_URL, {}, payload);
    return res["token"];
}

std::string_view FrisquetConnect::get_token() const
{
    TRACE_CALL();

    auto now = sc_now();

    if (now - last_token_time_ > TOKEN_RENEWAL_PERIOD)
    {
        DEBUG << " Token renewal " << std::flush;
        token_ = get_new_token(email_, password_);
        DEBUG << "Ok" << std::endl;
        last_token_time_ = now;
    }
    else
    {
        DEBUG << "No renewal" << std::endl;
    }
    return token_;
}

void FrisquetConnect::refresh()
{
    const_cast<const FrisquetConnect *>(this)->refresh(false);
}

void FrisquetConnect::refresh(bool force_refresh) const
{
    TRACE_CALL();

    auto now = sc_now();

    if (now - last_infos_update_time_ > INFOS_RENEWAL_PERIOD || force_refresh)
    {
        DEBUG << " infos renewal " << std::flush;
        last_infos_update_time_ = now;
        std::ostringstream sstr;
        sstr << API_URL << chaudiere_ << "?token=" << get_token();
        infos_ = json_request(sstr.str(), {}, {});
        display_alarm();
        DEBUG << "Ok" << std::endl;
    }
    else
    {
        DEBUG << "No renewal" << std::endl;
    }
}

void FrisquetConnect::set_boiler_mode(mode_e mode)
{
    pass_order({{"SELECTEUR_" + zone_, static_cast<int>(mode)}});
}

FrisquetConnect::program_week FrisquetConnect::get_program_week() const
{
    for (const auto &info_zone : infos_["zones"])
    {
        if (info_zone["identifiant"] == zone_)
        {
            program_week retval;
            for (const auto &day : info_zone["program"])
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

FrisquetConnect::program_day FrisquetConnect::get_program_day(int day_of_week) const
{
    return get_program_week()[day_of_week % 7];
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
    TRACE_CALL();
    refresh();
    DEBUG << infos_ << std::endl;
    return infos_.at("timezone");
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
        INFO << chaudiere_ << ":" << zone_ << ":alarme:" << i << std::endl;
}

bool FrisquetConnect::is_boiler_connected() const
{
    refresh();
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
    int program_index = (now - start_day) / 30min;

    return std::make_tuple(start_day, day_of_week, program_index);
}

bool FrisquetConnect::get() const
{
    auto [start_day, day_of_week, program_index] = decompose_now(get_timezone());

    auto today = get_program_day(day_of_week);
    return today[program_index] == 1;
}

void FrisquetConnect::pass_order(const std::map<std::string, json> &data) const
{
    TRACE_CALL();
    refresh(false);

    auto url = std::string{ORDRES_URL} + chaudiere_ + "?token=" + std::string{get_token()};
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

    INFO << payload.dump() << std::endl;
    auto response = json_request(url, headers, payload);
    INFO << response << std::endl;

    refresh(true);
}

void FrisquetConnect::force_set_program(int day, const program_day &pd) const
{
    day = day % 7;
    json sub_payload;
    sub_payload["jour"] = day;
    for (auto i : pd)
        sub_payload["plages"].emplace_back(static_cast<int>(i));
    json payload = json::array({sub_payload});
    pass_order({{"PROGRAMME_" + zone_, payload}});
}

void FrisquetConnect::force_set_program(const program_week &pw) const
{
    TRACE_CALL();
    last_whole_week_update_time_ = sc_now();

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

void FrisquetConnect::set_program_if_necessary(const program_week &pw) const
{
    TRACE_CALL();

    auto elapsed = chrono::duration_cast<chrono::seconds>(sc_now() - last_whole_week_update_time_);
    INFO << "Age of Frisquet Information : " << elapsed << std::endl;

    refresh(false);
    bool program_is_different = get_program_week() != pw;
    if (program_is_different)
        INFO << "Program has changed" << std::endl;
    bool program_is_too_old = elapsed > 24h;
    if (program_is_too_old)
        INFO << "Program has not been updated for 24h" << std::endl;
    if (program_is_too_old || program_is_different)
    {
        INFO << "Force sending information to Frisquet" << std::endl;
        force_set_program(pw);
    }
}

void FrisquetConnect::update_events(const Database::events &events, bool is_inverted)
{
    TRACE_CALL();

    try
    {
        DEBUG << " get timezone" << std::endl;
        auto [start_day, day_of_week, program_index] = decompose_now(get_timezone());
        const auto *tz = date::locate_zone(get_timezone());

        DEBUG << "compute_program" << std::endl;

        program_week pw{};

        for (auto &day : pw)
            for (auto &slot : day)
                slot = is_inverted;

        auto compute_index = [&](auto tp)
        {
            return (tz->to_local(tp) - start_day) / 30.min;
        };

        for (const auto &event : events)
        {
            long start = std::floor(compute_index(event.heat_start));
            long end = std::ceil(compute_index(event.heat_end));

            for (int i = std::max(0l, start); i < std::min(end, 48l * 7l); i++)
                pw[(i / 48 + day_of_week) % 7][i % 48] = !is_inverted;
        }

        DEBUG << "check is_boiler_connected" << std::endl;

        if (!is_boiler_connected())
            INFO << "Boiler " << chaudiere_ << ":" << zone_ << " is not connected, program might be transmitted only later" << std::endl;

        DEBUG << "send program" << std::endl;
        set_program_if_necessary(pw);
    }
    catch (std::exception &e)
    {
        INFO << " failed : " << e.what() << std::endl;
        refresh(true);
    };
}
