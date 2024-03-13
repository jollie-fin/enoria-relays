#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include "hwgpio.h"
#include "utils.h"

#include "usbrelay.h"
#include "frisquetconnect.h"

std::unique_ptr<HWGpio::GPIOHandler> HWGpio::make_impl(Channel channel)
{
    auto [category, id] = split2(channel, ':');
    category = category[0] == '!' ? category.substr(1) : category;
    if (category.starts_with("gpio"))
        return std::make_unique<RawGPIO>(id);
    if (category.starts_with("usbrelay"))
        return std::make_unique<USBRelay>(id);
    if (category.starts_with("frisquetconnect"))
        return std::make_unique<FrisquetConnect>(id);
    throw std::runtime_error("Invalid HWGPIO :" + std::string{category});
    return {};
}

HWGpio::RawGPIO::RawGPIO(HWGpio::Channel hw) : hw_("gpio" + std::string{hw})
{
    auto direction = "/sys/class/gpio/" + hw_ + "/direction";
    if (!exists(direction))
        echo("/sys/class/gpio/export", hw);
    if (!cat(direction).starts_with("out"))
    {
        std::cout << "Setting to out" << std::endl;
        echo(direction, "out");
    }
}
HWGpio::HWGpio(HWGpio::Channel ch) : impl_(make_impl(ch)), is_inverted_(ch[0] == '!')
{
}

HWGpio::HWGpio()
{
}

void HWGpio::set(bool value)
{
    value = is_inverted_ ? !value : value;
    impl_->set(value);
}

void HWGpio::RawGPIO::set(bool value)
{
    echo("/sys/class/gpio/" + hw_ + "/value", std::to_string(value));
}

bool HWGpio::get() const
{
    auto value = impl_->get();
    value = is_inverted_ ? !value : value;
    return value;
}

void HWGpio::update_events(const Database::events &events)
{
    impl_->update_events(events);
}

void HWGpio::refresh()
{
    impl_->refresh();
}

bool HWGpio::RawGPIO::get() const
{
    return cat("/sys/class/gpio/" + hw_ + "/value").starts_with("1");
}