#pragma once
#include <string_view>
#include <string>
#include <chrono>
#include "db.h"
#include "hwgpio.h"
#include "json_fwd.hpp"

class FrisquetConnect : public HWGpio::GPIOHandler
{
public:
    using program_day = std::array<bool, 48>;
    using program_week = std::array<program_day, 7>;
    using json = nlohmann::json;
    enum class mode_e
    {
        AUTO = 5,
        REDUIT_PERMANENT = 7,
        CONFORT_PERMANENT = 6,
        HORS_GEL = 8,
    };

    FrisquetConnect(std::string_view id);
    // void set(bool);
    bool get() const override;
    void refresh() override;
    void set_boiler_mode(mode_e mode);
    mode_e get_boiler_mode() const;
    std::vector<std::string> get_alarms() const;
    void display_alarm() const;
    bool is_boiler_connected() const;
    void pass_order(const std::map<std::string, json> &data) const;
    void set_programmation(int day, const program_day &pd) const; // Monday = 1
    void set_programmation(const program_week &pw) const;         // Monday, Tuesday, Wednesday...
    program_week get_programmation_week() const;         // Monday, Tuesday, Wednesday...
    program_day get_programmation_day(int day_of_week) const;
    void update_events(const Database::events &events) override;
    std::string get_timezone() const;

protected:

    std::string zone_;
    std::string chaudiere_;
    std::string token_;
    std::string email_;
    std::string password_;
    json infos_;
    std::chrono::steady_clock::time_point token_time_;
    std::chrono::steady_clock::time_point last_update_time_;
};
