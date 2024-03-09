#pragma once
#include "db.h"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include "hwgpio.h"

class GPIO
{
public:
    using Channel = std::string_view;
    GPIO(Database &db, std::string_view path);
    void set_channel(Channel channel, bool state);
    void check_channel_or_throw(Channel channel) const;
    bool get_hw_channel(Channel channel) const;
    void update_channels(const Database::events &events);
    std::vector<Channel> channel_list() const;
    void force_sync();

protected:
    std::map<Channel, HWGpio, std::less<>> channel_name_to_hw_gpio_;
    Database db_;
    std::set<std::string> channels_;
    std::map<Channel, int, std::less<>> state_;
};
