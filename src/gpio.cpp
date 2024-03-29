#include <stdexcept>
#include <iterator>
#include <iostream>
#include "env.h"
#include "gpio.h"
#include "hwgpio.h"
#include "utils.h"

GPIO::GPIO(Database &db, std::string_view path) : db_(db)
{
    for (const auto &[channel, hw_channel] : env::read_file(path))
    {
        Channel channel_view = *channels_.emplace(channel).first;
        state_[channel_view] = -1;
        channel_name_to_hw_gpio_.emplace(channel_view, hw_channel);
    }
}

void GPIO::force_sync()
{
    for (const auto &[channel, state] : state_)
        set_channel(channel, state);
}

std::vector<GPIO::Channel> GPIO::channel_list() const
{
    std::vector<GPIO::Channel> retval;
    std::copy(channels_.begin(), channels_.end(), std::back_inserter(retval));
    return retval;
}

void GPIO::check_channel_or_throw(Channel channel) const
{
    if (!state_.count(channel))
        throw std::runtime_error("Invalid channel in DB : " + std::string{channel});
}

void GPIO::dispatch_events(const Database::events &events)
{
    std::map<Channel, Database::events> dispatch;
    for (const auto &e : events)
        dispatch[e.channel].emplace_back(e);

    for (const auto &[channel, events] : dispatch)
        if (channel_name_to_hw_gpio_.count(channel))
            channel_name_to_hw_gpio_[channel].update_events(events);
}

void GPIO::refresh_channels()
{
    for (auto &[channel, gpio] : channel_name_to_hw_gpio_)
        gpio.refresh();
}

void GPIO::update_channels(const Database::events &events)
{
    std::map<Channel, bool> new_state;
    for (auto channel : channel_list())
        new_state[channel] = false;
    for (const auto &e : events)
        if (auto it = new_state.find(e.channel);
            it != new_state.end())
            it->second = true;

    for (const auto &[channel, state] : new_state)
        set_channel(channel, state);
}

void GPIO::set_channel(Channel channel, bool state)
{
    if (state != state_.at(channel))
        std::cout
            << "Setting "
            << channel
            << " to state "
            << state
            << std::endl;

    db_.update_channel(channel, state);
    state_[channel] = state;

    return channel_name_to_hw_gpio_[channel].set(state);
}

bool GPIO::get_hw_channel(Channel channel) const
{
    return channel_name_to_hw_gpio_.at(channel).get();
}
