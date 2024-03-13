#include "env.h"
#include "gpio.h"
#include <iostream>
#include <string_view>
#include <string>
#include <unistd.h>
#include "ics.h"
#include <chrono>

#define SQLITE_PATH "SQLITE_PATH"
#define GPIO_CFG "GPIO_CFG"
#define ENORIA_URI "ENORIA_URI"

#include "utils.h"

using namespace std::chrono_literals;

static int help(std::string_view name)
{
    std::cout << "Usage : "
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
    std::cout << "id;channel;description;start;end;is_current" << std::endl;
    for (const auto &event : events)
    {
        std::cout
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
    std::cout << "channel;description;state" << std::endl;
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};
    for (auto channel : gpio.channel_list())
    {
        auto state = db.fetch_channel_state(channel);
        auto description = db.fetch_channel_description(channel);
        std::cout
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

static void print_events(const Database::events &events)
{
    for (const auto &e : events)
        std::cout
            << "    "
            << "Event '" << e.description
            << "' in room '" << e.room
            << "' from '" << e.start
            << "' to '" << e.end
            << "' on channel '" << e.channel
            << "'" << std::endl;
}

static int automatic()
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};

    class Timer
    {
    public:
        Timer(std::string_view name,
              std::chrono::duration<long> period,
              std::function<void()> lambda)
            : name_(name),
              period_(period),
              lambda_(lambda)
        {
        }

        void operator()()
        {
            auto now = std::chrono::steady_clock::now();
            if (now - previous_ > period_)
            {
                std::cout << "Timer:" << name_ << std::endl;
                previous_ = now;
                try
                {
                    lambda_();
                }
                catch (std::exception &e)
                {
                    std::cout << " failed : " << e.what() << std::endl;
                }
            }
        }

    protected:
        std::string name_;
        std::function<void()> lambda_;
        std::chrono::steady_clock::time_point previous_;
        std::chrono::duration<long> period_;
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
                     std::cout << "Fetching new calendar from Enoria... " << std::flush;
                     events = ics::fetch_from_uri(env::get(ENORIA_URI, "http://invalid"));
                     std::cout << "Ok!" << std::endl;
                     break;
                 }
                 catch (std::exception &e)
                 {
                     std::cout << " failed :\n"
                               << "    " << e.what() << std::endl;
                     sleep(3);
                     count--;
                     if (count == 0)
                         throw;
                 }
             }

             std::cout << "found " << events.events.size() << " events" << std::endl;
             db.update_events(events);
             std::cout
                 << db.current_and_future_events_count()
                 << " events are curently in the present or future"
                 << std::endl;
             std::cout
                 << "  Current events" << std::endl;
             print_events(db.fetch_current());
             std::cout
                 << "  Future events" << std::endl;
             print_events(db.fetch_future());
         }},
        {"Update-GPIO",
         1min,
         [&]()
         {
             std::cout << "Fetching current events from database... " << std::flush;
             auto current_state = db.fetch_current();
             std::cout << "ok" << std::endl;
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

    std::cout
        << db.current_and_future_events_count()
        << " events are currently in the present or future"
        << std::endl;
    std::cout
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
        std::cout
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
    std::cout
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
    std::cout
        << "Request setting channel "
        << channel
        << " to state "
        << state
        << std::endl;
    gpio.set_channel(channel, state.starts_with("1"));
    auto hw_state = gpio.get_hw_channel(channel);
    std::cout
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
    Database::event e{.start = start, .end = end, .room = channel, .channel = channel, .description = description};

    db.add_event(e);
    std::cout << "Programming event" << std::endl;
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
        std::cout
            << "caught exception:"
            << e.what()
            << std::endl;
        return 1;
    }
}
