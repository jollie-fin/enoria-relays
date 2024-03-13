#pragma once

#include <string_view>
#include <string>
#include <memory>
#include <stdexcept>
#include "hwgpio.h"
struct hid_device_;
class USBRelay : public HWGpio::GPIOHandler
{
public:
    USBRelay(std::string_view id);
    void set(bool) override;
    bool get() const override;

protected:
    unsigned char hw_state() const;
    std::runtime_error hid_exception() const;
    std::shared_ptr<hid_device_> hid_handle_;
    u_int64_t vid_;
    u_int64_t pid_;
    int channel_;
    static bool first_;
};
