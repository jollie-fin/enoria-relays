# A simple scheduler for relays

## Context

With the rising energy crisis, the local parish was looking to reduce energy expanditure around heating. The easy approach "first one put on, last one put off" was not sufficient because their buildings are old : they take a while to heat up.

The following idea was suggested. Since the room occupancy schedule is available online through Enoria, why not programmatically put on and off the heater based on the schedule ?

## Goals

This project goal were the following :

- Retrieve a calendar file from online, and populate a local database
- Based on the database, put on and off some relays
- Be as simple as possible to ease maintenance
  - Be monolithic
  - As little dependencies as possible

C++ was choosen as a language due to the familiarity of the main maintener with the language

## How to run

- First, setup `data/env` according to installation path
  - `SQLITE_PATH` will be the path to the local database
  - `GPIO_CFG` will be the path to the descriptor of how to switch the relays
  - `ENORIA_URI` will be the path to the online ICS calendar. For testing, and URI of the form `file://` can be provided
- Setup `data/events.db` and `data/gpio.cfg` based on the relay configuration. Both USB-relay cards and direct GPIO can be used. The roadmap includes interactions with home-assistant in the near future
- Create folder `build`
- From `build`, compile with `cmake ..` and `make`. You can install it with `cmake --install .` if necessary
- Run with `enoria-relays --env [PATH TO YOUR ENV FILE] --automatic` to start it in autonomous mode

Every hour, the ICS will be downloaded according to ENORIA_URI and the SQLITE database will be populated. Every minute, the database will be checked, and relays will be put on and off accordingly
