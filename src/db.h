#pragma once

#include "ics.h"
#include "sqlite.h"
#include <vector>
#include <chrono>
#include "utils.h"

class Database
{
public:
    struct event
    {
        int64_t id{-1};
        timepoint heat_start;
        timepoint heat_end;
        timepoint start;
        timepoint end;
        std::string room;
        std::string channel;
        std::string description;
        bool state{false};
        bool is_current{false};
    };
    using events = std::vector<event>;
    Database(std::string_view path);
    void add_event(const event &ev);
    void update_events(const ics::events &ev);
    void update_channel(std::string_view channel, bool state);
    events fetch_all_to_come() const;
    events fetch_between(timepoint before,
                         timepoint after) const;
    events fetch_between(int64_t before, int64_t after) const;
    events fetch_current() const;
    events fetch_currently_heating() const;
    bool fetch_channel_state(std::string_view channel) const;
    std::vector<std::string> fetch_channel_description(std::string_view channel) const;
    events fetch_earliest_in_future() const;
    size_t current_and_future_events_count() const;

protected:
    SQLite sql_;
};