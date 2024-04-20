#include <stdexcept>
#include "usbrelay.h"
#include "utils.h"
#include <hidapi/hidapi.h>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <unistd.h>
#include "log.h"

bool USBRelay::first_ = true;

static std::string convert_wstring(const wchar_t *wstr)
{
    char buffer[8192];
    snprintf(buffer, sizeof(buffer), "%ls", wstr);
    return buffer;
}
std::runtime_error USBRelay::hid_exception() const
{
    return std::runtime_error{
        "Error : " +
        convert_wstring(hid_error(hid_handle_.get()))};
}

static void hid_free()
{
    if (hid_exit())
        ERROR << "Found error in hid_exit" << std::endl;
}

USBRelay::USBRelay(std::string_view id)
{
    if (first_)
    {
        first_ = false;
        if (hid_init())
            throw std::runtime_error{"error in hid_init"};
        atexit(hid_free);
    }

    auto fields = split(id, ':');
    if (fields.size() != 3)
        throw std::runtime_error("Impossible to initialize usbrelay with " +
                                 std::string{id});
    std::from_chars(fields[0].data(),
                    fields[0].data() + fields[0].size(),
                    vid_,
                    16);
    std::from_chars(fields[1].data(),
                    fields[1].data() + fields[1].size(),
                    pid_,
                    16);
    std::from_chars(fields[2].data(),
                    fields[2].data() + fields[2].size(),
                    channel_);
    hid_handle_ = std::shared_ptr<hid_device>{hid_open(vid_, pid_, NULL), hid_close};
    if (!hid_handle_)
        throw hid_exception();
    wchar_t buffer[255];
    if (hid_get_product_string(hid_handle_.get(), buffer, 255))
        throw hid_exception();
    auto product_string = convert_wstring(buffer);
    if (!product_string.starts_with("USBRelay"))
        throw std::runtime_error("Only supporting DCTTECH relay board");
}

unsigned char USBRelay::hw_state() const
{
    unsigned char buf[9] = {0x01};
    hid_get_feature_report(hid_handle_.get(), buf, sizeof(buf));
    return buf[7];
}

void USBRelay::set(bool state)
{
    unsigned char buf[9] = {};
    int res = 0;
    buf[1] = state ? 0xFF : 0xFD;
    buf[2] = channel_ + 1;
    res = hid_write(hid_handle_.get(), buf, sizeof(buf));
    if (res < 0)
        throw hid_exception();
}

bool USBRelay::get() const
{
    return hw_state() & (1 << channel_);
}
