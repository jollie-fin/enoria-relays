#pragma once

#include <string_view>
#include <variant>
#include "usbrelay.h"

class HWGpio
{
public:
    using Channel = std::string_view;
    HWGpio(Channel hw);
    HWGpio();
    bool get() const;
    void set(bool st);

protected:
    struct RawGPIO
    {
        RawGPIO(Channel hw);
        bool get() const;
        void set(bool);
        std::string hw_;
    };

    struct Nada
    {
        bool get() const { return false; }
        void set(bool) {}
        std::string dummy;
    };

    using Implementation = std::variant<RawGPIO, USBRelay, Nada>;
    static Implementation make_impl(HWGpio::Channel ch);
    Implementation impl_{Nada{""}};
    bool is_inverted_{false};
};