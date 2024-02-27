#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include "hwgpio.h"
#include "utils.h"

HWGpio::Implementation HWGpio::make_impl(HWGpio::Channel channel)
{
    std::string strip{channel[0] == '!' ? channel.substr(1) : channel};
    auto category_start = 0;
    auto category_end = strip.find(':');
    if (category_end == strip.npos)
        throw std::runtime_error("Invalid HWGPIO :" + strip);

    auto id_start = category_end + 1;
    auto id_end = strip.npos;

    auto category = strip.substr(category_start, category_end - category_start);
    auto id = strip.substr(id_start, id_end - id_start);

    if (category.starts_with("gpio"))
        return RawGPIO{id};
    if (category.starts_with("usbrelay"))
        return USBRelay{id};
    throw std::runtime_error("Invalid HWGPIO :" + strip);
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
    std::visit([&](auto &a)
               { a.set(value); },
               impl_);
}

void HWGpio::RawGPIO::set(bool value)
{
    echo("/sys/class/gpio/" + hw_ + "/value", std::to_string(value));
}

bool HWGpio::get() const
{
    auto value =
        std::visit([&](auto &a)
                   { return a.get(); },
                   impl_);
    value = is_inverted_ ? !value : value;
    return value;
}

bool HWGpio::RawGPIO::get() const
{
    return cat("/sys/class/gpio/" + hw_ + "/value").starts_with("1");
}