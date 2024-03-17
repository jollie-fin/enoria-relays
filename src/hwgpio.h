#pragma once

#include <string_view>
#include <memory>

#include "db.h"

class HWGpio
{
public:
    using Channel = std::string_view;
    HWGpio(Channel hw);
    HWGpio();
    bool get() const;
    void set(bool st);
    void update_events(const Database::events &events);
    void refresh();

    struct GPIOHandler
    {
        GPIOHandler(std::string = "") {}
        virtual bool get() const { return false; }
        virtual void set(bool) {}
        virtual void update_events(const Database::events &events, bool is_inverted) {}
        virtual void refresh() {}
    };

    static std::unique_ptr<GPIOHandler> make_impl(Channel);

protected:
    struct RawGPIO : public GPIOHandler
    {
        RawGPIO(Channel hw);
        bool get() const override;
        void set(bool) override;
        std::string hw_;
    };

    struct FakeGPIO : public GPIOHandler
    {
        FakeGPIO(Channel hw);
        void set(bool) override;
        std::string hw_;
    };

    std::unique_ptr<GPIOHandler> impl_;
    bool is_inverted_{false};
};