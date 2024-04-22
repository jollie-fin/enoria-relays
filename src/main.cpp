#include "env.h"
#include "gpio.h"
#include <iostream>
#include <string_view>
#include <string>
#include <unistd.h>
#include "ics.h"
#include <chrono>
#include "date/date.h"
#include "date/tz.h"
#include "utils.h"
#include "log.h"

#define SQLITE_PATH "SQLITE_PATH"
#define GPIO_CFG "GPIO_CFG"
#define ENORIA_URI "ENORIA_URI"

using namespace std::chrono_literals;
using namespace date;

static int help(std::string_view name)
{
    RAW << "Usage : "
        << name
        << "(--env CONFIG_FILE) [--automatic|"
           "--read-status|"
           "--read-hw CHANNEL|"
           "--set-hw CHANNEL VALUE|"
           "--program-event CHANNEL DELAY_MIN DESCRIPTION|"
           "--api-list-channels|"
           "--api-list-events BEFORE AFTER|"
           "--api-list-current-events|"
           "--list-events]"
        << std::endl;
    return 1;
}

static void print_events_csv(const Database::events &events)
{
    RAW << "id;channel;description;start;end;is_current" << std::endl;
    for (const auto &event : events)
    {
        RAW
            << event.id
            << ";"
            << event.channel
            << ";"
            << event.description
            << ";"
            << event.start
            << ";"
            << event.end
            << ";"
            << event.is_current
            << std::endl;
    }
}

static int api_list_channels()
{
    RAW << "channel;description;state" << std::endl;
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};
    for (auto channel : gpio.channel_list())
    {
        auto state = db.fetch_channel_state(channel);
        std::string description;
        for (auto d : db.fetch_channel_description(channel))
        {
            if (!description.empty())
                description += '/';
            description += d;
        }
        RAW
            << channel
            << ";"
            << description
            << ";"
            << state
            << std::endl;
    }
    return 1;
}

static int api_list_events(std::string before, std::string after)
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    print_events_csv(db.fetch_between(std::stoll(before), std::stoll(after)));
    return 1;
}

static int api_list_current_events()
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    print_events_csv(db.fetch_current());
    return 1;
}

static auto to_locale(auto sys_time)
{
    return date::zoned_seconds{
        date::current_zone(),
        std::chrono::floor<std::chrono::seconds>(sys_time)};
}

static void print_events(const Database::events &events)
{
    for (const auto &e : events)
        INFO
            << "    "
            << "Event '" << e.description
            << "' in room '" << e.room
            << "' from '" << to_locale(e.start)
            << "' to '" << to_locale(e.end)
            << "' on channel '" << e.channel
            << "' (heating time '" << to_locale(e.heat_start)
            << "'-'" << to_locale(e.heat_end)
            << "')" << std::endl;
}

static int automatic()
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};

    class Timer
    {
    public:
        Timer(std::string_view name,
              chrono::duration<long> period,
              std::function<void()> lambda)
            : name_(name),
              lambda_(lambda),
              period_(period)
        {
        }

        void operator()()
        {
            auto now = chrono::steady_clock::now();
            if (now - previous_ > period_)
            {
                DEBUG << "Timer:" << name_ << std::endl;
                previous_ = now;
                try
                {
                    lambda_();
                }
                catch (std::exception &e)
                {
                    ERROR << " failed : " << e.what() << std::endl;
                }
            }
        }

    protected:
        std::string name_;
        std::function<void()> lambda_;
        chrono::steady_clock::time_point previous_;
        chrono::duration<long> period_;
    };

    std::vector<Timer> timers{
        {"Fetch-enoria",
         1h,
         [&]()
         {
             int count = 5;
             ics::events events;
             while (1)
             {
                 try
                 {
                     INFO << "Fetching new calendar from Enoria... " << std::flush;
                     events = ics::fetch_from_uri(env::get(ENORIA_URI, "http://invalid"));
                     INFO << "Ok!" << std::endl;
                     break;
                 }
                 catch (std::exception &e)
                 {
                     ERROR << " failed :\n"
                           << "    " << e.what() << std::endl;
                     sleep(3);
                     count--;
                     if (count == 0)
                         throw;
                 }
             }

             INFO << "found " << events.events.size() << " events" << std::endl;
             db.update_events(events);
             INFO
                 << db.current_and_future_events_count()
                 << " events are curently in the present or future"
                 << std::endl;
             INFO
                 << "  Current events" << std::endl;
             print_events(db.fetch_current());
             INFO
                 << "  Future events" << std::endl;
             print_events(db.fetch_earliest_in_future());
         }},
        {"Update-GPIO",
         1min,
         [&]()
         {
             INFO << "Fetching current events from database... " << std::flush;
             auto current_state = db.fetch_currently_heating();
             INFO << "ok" << std::endl;
             print_events(current_state);
             gpio.update_channels(current_state);
         }},
        {"Update-programmation",
         30min,
         [&]()
         {
             auto now = get_time_now();
             auto events = db.fetch_between(
                 now - 24h,
                 now + 7 * 24h);
             gpio.dispatch_events(events);
         }},
        {"Refresh-channels",
         5min,
         [&]()
         {
             gpio.refresh_channels();
         }}};

    while (1)
    {
        for (auto &i : timers)
            i();
        sleep(1);
    }

    return 0; // Should not happen
}

static int list_current_and_future_events()
{
    Database db{env::get(SQLITE_PATH, "test.db")};

    INFO
        << db.current_and_future_events_count()
        << " events are currently in the present or future"
        << std::endl;
    INFO
        << " events to come" << std::endl;
    print_events(db.fetch_all_to_come());

    return 0;
}

static int read_status()
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};
    for (auto channel : gpio.channel_list())
    {
        auto state = db.fetch_channel_state(channel);
        INFO
            << "channel "
            << channel
            << " has state "
            << state
            << std::endl;
    }
    print_events(db.fetch_current());
    return 0;
}

static int read_hw(std::string_view channel)
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};
    auto hw_state = gpio.get_hw_channel(channel);
    INFO
        << "channel "
        << channel
        << " has hardware state "
        << hw_state
        << std::endl;
    return 0;
}

static int set_hw(std::string_view channel, std::string_view state)
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};
    INFO
        << "Request setting channel "
        << channel
        << " to state "
        << state
        << std::endl;
    gpio.set_channel(channel, state.starts_with("1"));
    auto hw_state = gpio.get_hw_channel(channel);
    INFO
        << "channel "
        << channel
        << " has hardware state "
        << hw_state
        << std::endl;

    return 0;
}

static int program_event(std::string channel, std::string length_min, char **description_words)
{
    std::string description;
    for (int i = 0; description_words[i]; i++)
    {
        description += i == 0 ? "" : " ";
        description += description_words[i];
    }
    auto start = get_time_now();
    auto end = start + std::stoll(length_min) * 1min;
    start -= 30min; // start is 30 min in the past

    Database db{env::get(SQLITE_PATH, "test.db")};
    Database::event e{.heat_start = start, .heat_end = end, .start = start, .end = end, .room = channel, .channel = channel, .description = description};

    db.add_event(e);
    INFO << "Programming event" << std::endl;
    print_events({e});
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    using namespace std::string_literals;
    env::read_envp(envp);

    auto tool_name = argc ? argv[0] : "heater-automation";
    if (argc)
    {
        argc--;
        argv++;
    }

    if (argc >= 2 && argv[0] == "--env"s)
    {
        env::read_envp(argv[1]);
        argc -= 2;
        argv += 2;
    }

    if (argc < 1)
        return help(tool_name);

    auto mode = std::string{argv[0]};
    try
    {
        if (mode == "--automatic")
            return automatic();
        else if (mode == "--read-status")
            return read_status();
        else if (mode == "--read-hw" && argc >= 2)
            return read_hw(argv[1]);
        else if (mode == "--set-hw" && argc >= 3)
            return set_hw(argv[1], argv[2]);
        else if (mode == "--program-event" && argc >= 4)
            return program_event(argv[1], argv[2], argv + 3);
        else if (mode == "--list-events")
            return list_current_and_future_events();
        else if (mode == "--api-list-channels")
            return api_list_channels();
        else if (mode == "--api-list-events" && argc >= 3)
            return api_list_events(argv[1], argv[2]);
        else if (mode == "--api-list-current-events")
            return api_list_current_events();
        else
            return help(tool_name);
    }
    catch (std::exception &e)
    {
        ERROR
            << "caught exception:"
            << e.what()
            << std::endl;
        return 1;
    }
}
