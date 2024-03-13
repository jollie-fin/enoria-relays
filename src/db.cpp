#include "db.h"
#include "utils.h"
#include <string>
#include <iostream>
#include <set>

using namespace std::chrono_literals;

Database::Database(std::string_view path) : sql_(path)
{
}

static Database::event create_event(const SQLite::SqlRow &row)
{
    if (row.size() != 8)
        throw std::runtime_error{"impossible to create event from sql row"};
    Database::event retval;
    retval.start = from_timestamp(row[0]);
    retval.end = from_timestamp(row[1]);
    retval.room = row[2];
    retval.description = row[3];
    retval.channel = row[4];
    retval.state = std::stoi(row[5]);
    retval.is_current = std::stoi(row[6]);
    retval.id = std::stoi(row[7]);
    return retval;
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
             "       relays.STATE, "
             "       0, "
             "       e.ID "
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
        retval.emplace_back(create_event(row));
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
             "       relays.STATE, "
             "       1, "
             "       events.ID "
             "FROM events "
             "JOIN relays "
             "ON events.SALLE LIKE relays.NICKNAME "
             "WHERE ? >= events.START - relays.INERTIA "
             "AND ? <= events.END",
             {now, now}))
    {
        retval.emplace_back(create_event(row));
    }
    return retval;
}

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
             "       ? >= events.START - relays.INERTIA AND ? <= events.END, "
             "       events.ID "
             "FROM events "
             "JOIN relays "
             "ON events.SALLE LIKE relays.NICKNAME "
             "WHERE ? <= events.END "
             "ORDER BY events.START "
             "LIMIT 10 ",
             {now, now, now}))
    {
        retval.emplace_back(create_event(row));
    }
    return retval;
}

Database::events Database::fetch_between(
    timepoint before,
    timepoint after) const
{
    return fetch_between(to_timestamp(before), to_timestamp(after));
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
             "       ? >= events.START - relays.INERTIA AND ? <= events.END, "
             "       events.ID "
             "FROM events "
             "JOIN relays "
             "ON events.SALLE LIKE relays.NICKNAME "
             "WHERE ? <= events.START "
             "AND events.START <= ?",
             {now, now, before, after}))
    {
        retval.emplace_back(create_event(row));
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
    auto insert_sql =
        "INSERT INTO events (SALLE, START, END, ENTETE) "
        "            VALUES (?,?,?,?)";

    sql_.exec(insert_sql,
              {e.room,
               e.start,
               e.end,
               e.description});
}

void Database::update_events(const ics::events &ics_events)
{
    auto transaction = sql_.transaction();
    auto now = get_time_now();
    auto drop_old_sql =
        "DELETE FROM events WHERE events.END < ?";
    sql_.exec(drop_old_sql, {now - std::chrono::months{1}});

    events db_events;
    auto find_id_sql =
        "SELECT events.ID FROM events "
        " WHERE events.START = ? "
        " AND events.END = ? "
        " AND events.SALLE = ? "
        " AND events.ENTETE = ? ";
    std::set<int64_t> id_to_keep;
    events events_to_add;
    for (const auto &e : ics_events.events)
    {
        auto id_candidates = sql_.exec(find_id_sql, {e.start, e.end, e.location, e.summary});
        if (!id_candidates.empty())
        {
            auto id = std::stoll(id_candidates[0][0]);
            id_to_keep.emplace(id);
        }
        else if (e.start > now)
        {
            events_to_add.emplace_back(event{
                .start = e.start,
                .end = e.end,
                .room = e.location,
                .description = e.summary,
            });
        }
    }
    auto get_ids_sql = "SELECT id FROM events WHERE events.START > ?";
    auto delete_id_sql = "DELETE FROM events WHERE ID = ?";
    for (const auto &row : sql_.exec(get_ids_sql, {now}))
    {
        auto id = std::stoll(row[0]);
        if (!id_to_keep.count(id))
            sql_.exec(delete_id_sql, {id});
    }

    for (const auto &e : events_to_add)
        add_event(e);
    // if (!events_to_add.empty())
    //     throw std::runtime_error{"leaving"};
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
    auto now = std::to_string(to_timestamp(get_time_now()));
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
