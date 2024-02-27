#include "env.h"
#include "gpio.h"
#include <iostream>
#include <string_view>
#include <string>
#include <unistd.h>
#include "ics.h"
#define SQLITE_PATH "SQLITE_PATH"
#define GPIO_CFG "GPIO_CFG"
#define ENORIA_URI "ENORIA_URI"
#include "utils.h"

static int help(std::string_view name)
{
    std::cout << "Usage : "
              << name
              << "(--env CONFIG_FILE) [--automatic|"
                 "--read-status|"
                 "--read-hw CHANNEL|"
                 "--set-hw CHANNEL VALUE|"
                 "--program-event CHANNEL DELAY_MIN DESCRIPTION]"
              << std::endl;
    return 1;
}

static void print_events(const Database::events &events)
{
    for (const auto &e : events)
        std::cout
            << "    "
            << "Event '" << e.description
            << "' in room '" << e.room
            << "' from '" << convert_time(e.start)
            << "' to '" << convert_time(e.end)
            << "' on channel '" << e.channel
            << "'" << std::endl;
}
static int automatic()
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};

    while (1)
    {
        try
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
        }
        catch (std::exception &e)
        {
            std::cout << " failed : " << e.what() << std::endl;
        }
        for (int minute = 0; minute < 60; minute++)
        {
            try
            {
                std::cout << "Fetching current events from database... " << std::flush;
                auto current_state = db.fetch_current();
                std::cout << "ok" << std::endl;
                print_events(current_state);
                gpio.update_channels(current_state);
            }
            catch (std::exception &e)
            {
                std::cout << " failed : " << e.what() << std::endl;
            }
            sleep(60);
        }
    }

    return 0; // Should not happen
}

static int read_status()
{
    Database db{env::get(SQLITE_PATH, "test.db")};
    GPIO gpio{db, env::get(GPIO_CFG, "gpio.cfg")};
    for (auto channel : gpio.channel_list())
    {
        auto state = gpio.get_channel_from_db(channel);
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
    int64_t start = get_time_now();
    int64_t end = start + std::stoll(length_min) * 60;
    start -= 30 * 60; // start is 30 min in the past

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