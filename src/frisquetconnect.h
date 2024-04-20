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
    void refresh(bool force_refresh = false) const;
    std::string_view get_token() const;
    void set_boiler_mode(mode_e mode);
    mode_e get_boiler_mode() const;
    std::vector<std::string> get_alarms() const;
    void display_alarm() const;
    bool is_boiler_connected() const;
    void pass_order(const std::map<std::string, json> &data) const;
    void force_set_program(int day, const program_day &pd) const; // Monday = 1
    void force_set_program(const program_week &pw) const;         // Monday, Tuesday, Wednesday...
    void set_program_if_necessary(const program_week &pw) const;  // Monday, Tuesday, Wednesday...
    program_week get_program_week() const;                        // Monday, Tuesday, Wednesday...
    program_day get_program_day(int day_of_week) const;
    void update_events(const Database::events &events, bool is_inverted) override;
    std::string get_timezone() const;

protected:
    std::string zone_;
    std::string chaudiere_;
    mutable std::string token_;
    std::string email_;
    std::string password_;
    mutable json infos_;
    mutable std::chrono::steady_clock::time_point last_token_time_;
    mutable std::chrono::steady_clock::time_point last_infos_update_time_;
    mutable std::chrono::steady_clock::time_point last_whole_week_update_time_;
};
