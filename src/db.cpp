#include "db.h"
#include "utils.h"
#include <ctime>
#include <string>
#include <iostream>
Database::Database(std::string_view path) : sql_(path)
{
}

Database::events Database::fetch_future() const
{
    auto now = get_time_now();
    Database::events retval;
    for (const auto &row : sql_.exec(
             "SELECT e.START, "
             "       e.END, "
             "       e.SALLE, "
             "       e.ENTETE, "
             "       relays.CHANNEL,"
             "       relays.STATE "
             "FROM events e "
             "JOIN ("
             "     SELECT MIN(events.start) as start,"
             "            relays.NICKNAME as nick "
             "     FROM events "
             "     JOIN relays "
             "     ON events.SALLE LIKE relays.NICKNAME "
             "     WHERE events.start > ? "
             "     GROUP BY nick "
             "     ) s "
             "ON e.SALLE LIKE s.nick AND s.START = e.START "
             "JOIN relays "
             "ON relays.NICKNAME = s.nick",
             {now}))
    {
        retval.emplace_back();
        retval.back().start = std::stoll(row[0]);
        retval.back().end = std::stoll(row[1]);
        retval.back().room = row[2];
        retval.back().description = row[3];
        retval.back().channel = row[4];
        retval.back().state = std::stoi(row[5]);
    }
    return retval;
}

Database::events Database::fetch_current() const
{
    auto now = get_time_now();
    Database::events retval;
    for (const auto &row : sql_.exec(
             "SELECT events.START, "
             "       events.END, "
             "       events.SALLE, "
             "       events.ENTETE, "
             "       relays.CHANNEL,"
             "       relays.STATE "
             "FROM events "
             "JOIN relays "
             "ON events.SALLE LIKE relays.NICKNAME "
             "WHERE ? >= events.START - relays.INERTIA "
             "AND ? <= events.END",
             {now, now}))
    {
        retval.emplace_back();
        retval.back().start = std::stoll(row[0]);
        retval.back().end = std::stoll(row[1]);
        retval.back().room = row[2];
        retval.back().description = row[3];
        retval.back().channel = row[4];
        retval.back().state = std::stoi(row[5]);
        retval.back().is_current = true;
    }
    return retval;
}

// ajout 22/02

Database::events Database::fetch_all_to_come() const
{
    auto now = get_time_now();
    Database::events retval;
    for (const auto &row : sql_.exec(
             "SELECT events.START, "
             "       events.END, "
             "       events.SALLE, "
             "       events.ENTETE, "
             "       relays.CHANNEL,"
             "       relays.STATE,"
             "       ? >= events.START - relays.INERTIA AND ? <= events.END "
             "FROM events "
             "JOIN relays "
             "ON events.SALLE LIKE relays.NICKNAME "
             "WHERE ? <= events.END "
	     "ORDER BY events.START "
             "LIMIT 10 ",
             {now,now,now}))
    {
        retval.emplace_back();
        retval.back().start = std::stoll(row[0]);
        retval.back().end = std::stoll(row[1]);
        retval.back().room = row[2];
        retval.back().description = row[3];
        retval.back().channel = row[4];
        retval.back().state = std::stoi(row[5]);
        retval.back().is_current = std::stoi(row[6]);
    }
    return retval;
}

Database::events Database::fetch_between(int64_t before, int64_t after) const
{
    auto now = get_time_now();
    Database::events retval;
    for (const auto &row : sql_.exec(
             "SELECT events.START, "
             "       events.END, "
             "       events.SALLE, "
             "       events.ENTETE, "
             "       relays.CHANNEL,"
             "       relays.STATE ,"
             "       ? >= events.START - relays.INERTIA AND ? <= events.END "
             "FROM events "
             "JOIN relays "
             "ON events.SALLE LIKE relays.NICKNAME "
             "WHERE ? <= events.START "
             "AND events.START <= ?",
             {now, now, before, after}))
    {
        retval.emplace_back();
        retval.back().start = std::stoll(row[0]);
        retval.back().end = std::stoll(row[1]);
        retval.back().room = row[2];
        retval.back().description = row[3];
        retval.back().channel = row[4];
        retval.back().state = std::stoi(row[5]);
        retval.back().is_current = std::stoi(row[6]);
    }
    return retval;
}

size_t Database::current_and_future_events_count() const
{
    auto test_sql = "SELECT NULL FROM events WHERE "
                    " END>=?";
    return sql_.exec(
                   test_sql,
                   {get_time_now()})
        .size();
}

void Database::add_event(const event &e)
{
    auto now = get_time_now();
    auto insert_sql =
        "INSERT INTO events (SALLE, START, END, ENTETE) "
        "            VALUES (?,?,?,?)";

    auto test_sql =
        "SELECT NULL FROM events WHERE "
        " SALLE=? AND START=? AND END=?";

    if (sql_.exec(
                test_sql,
                {e.room,
                 e.start,
                 e.end})
            .empty())
        sql_.exec(insert_sql,
                  {e.room,
                   e.start,
                   e.end,
                   e.description});
}

void Database::update_events(const ics::events &events)
{
    auto transaction = sql_.transaction();
    auto now = get_time_now();
    auto one_month = 86400 * 30;
    auto drop_sql =
        "DELETE FROM events WHERE "
        "events.START > ? "
        "     OR events.END < ? - ?";
    sql_.exec(drop_sql, {now, now, one_month});

    for (const auto &e : events.events)
        if (e.end >= now)
            add_event(event{
                .start = e.start,
                .end = e.end,
                .room = e.location,
                .description = e.summary,
            });

    transaction.success();
}

bool Database::fetch_channel_state(std::string_view channel) const
{
    auto sql =
        "SELECT relays.STATE FROM relays "
        "WHERE CHANNEL = ?";
    auto res = sql_.exec(sql, {channel});
    if (res.empty())
        throw std::runtime_error("Unknown channel " + std::string{channel});
    return res[0][0] == "1";
}

std::string Database::fetch_channel_description(std::string_view channel) const
{
    auto sql =
        "SELECT relays.FULLNAME FROM relays "
        "WHERE CHANNEL = ?";
    auto res = sql_.exec(sql, {channel});
    if (res.empty())
        throw std::runtime_error("Unknown channel " + std::string{channel});
    return res[0][0];
}

void Database::update_channel(std::string_view channel, bool state)
{
    auto now = std::to_string(get_time_now());
    auto state_str = std::to_string(state);
    auto update_sql =
        "UPDATE relays "
        "SET STATE = ?, LAST_UPDATE = ? "
        "WHERE CHANNEL = ? AND STATE <> ?";
    sql_.exec(update_sql, {state_str,
                           now,
                           channel,
                           state_str});
}
