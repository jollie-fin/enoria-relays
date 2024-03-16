#include "db.h"
#include "utils.h"
#include <string>
#include <set>

#define SELECT_FROM_EVENT                                    \
    "SELECT events.START, \n"                                \
    "       events.END, \n"                                  \
    "       relays.FULLNAME, \n"                             \
    "       events.ENTETE, \n"                               \
    "       relays.CHANNEL,\n"                               \
    "       relays.STATE,\n"                                 \
    "       ? >= events.START - relays.INERTIA \n"           \
    "       AND ? <= events.END as is_current, \n"           \
    "       events.ID, \n"                                   \
    "       events.START - relays.INERTIA AS heat_start, \n" \
    "       CASE WHEN relays.STOP_DURING_MASS \n"            \
    "               AND events.ENTETE LIKE '%messe%' \n"     \
    "            THEN events.START \n"                       \
    "            ELSE events.END \n"                         \
    "            END \n"                                     \
    "            AS heat_end \n"                             \
    "FROM events \n"

#define SELECT_FROM_EVENTS_JOIN_RELAYS \
    SELECT_FROM_EVENT                  \
    "JOIN relays \n"                   \
    "ON events.SALLE LIKE relays.PATTERN_MATCHING \n"

using namespace std::chrono_literals;

Database::Database(std::string_view path) : sql_(path)
{
}

static Database::event create_event(const SQLite::SqlRow &row)
{
    if (row.size() != 10)
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
    retval.heat_start = from_timestamp(row[8]);
    retval.heat_end = from_timestamp(row[9]);
    return retval;
}
Database::events Database::fetch_earliest_in_future() const
{
    auto now = get_time_now();
    Database::events retval;
    for (const auto &row : sql_.exec(
             SELECT_FROM_EVENT
             "JOIN (\n"
             "     SELECT MIN(e.start) as start,\n"
             "            relays.PATTERN_MATCHING as pattern \n"
             "     FROM events e \n"
             "     JOIN relays \n"
             "     ON e.SALLE LIKE relays.PATTERN_MATCHING \n"
             "     WHERE e.start > ? \n"
             "     GROUP BY pattern \n"
             "     ) s \n"
             "ON events.SALLE LIKE s.pattern AND s.start = events.START \n"
             "JOIN relays \n"
             "ON relays.PATTERN_MATCHING = s.pattern",
             {now, now, now}))
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
             SELECT_FROM_EVENTS_JOIN_RELAYS
             "WHERE is_current ",
             {now, now}))
    {
        retval.emplace_back(create_event(row));
    }
    return retval;
}

Database::events Database::fetch_currently_heating() const
{
    auto now = get_time_now();
    Database::events retval;
    for (const auto &row : sql_.exec(
             SELECT_FROM_EVENTS_JOIN_RELAYS
             "WHERE ? >= heat_start \n"
             "AND ? <= heat_end ",
             {now, now, now, now}))
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
             SELECT_FROM_EVENTS_JOIN_RELAYS
             "WHERE ? <= events.END \n"
             "ORDER BY events.START \n"
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
             SELECT_FROM_EVENTS_JOIN_RELAYS
             "WHERE ? <= events.START \n"
             "AND events.START <= ?",
             {now, now, before, after}))
    {
        retval.emplace_back(create_event(row));
    }
    return retval;
}

size_t Database::current_and_future_events_count() const
{
    auto test_sql = "SELECT NULL FROM events WHERE \n"
                    " END>=?";
    return sql_.exec(
                   test_sql,
                   {get_time_now()})
        .size();
}

void Database::add_event(const event &e)
{
    auto insert_sql =
        "INSERT INTO events (SALLE, START, END, ENTETE) \n"
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
    sql_.exec(drop_old_sql, {now - chrono::months{1}});

    events db_events;
    auto find_id_sql =
        "SELECT events.ID FROM events \n"
        " WHERE events.START = ? \n"
        " AND events.END = ? \n"
        " AND events.SALLE = ? \n"
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
        "SELECT relays.STATE FROM relays \n"
        "WHERE CHANNEL = ?";
    auto res = sql_.exec(sql, {channel});
    if (res.empty())
        throw std::runtime_error("Unknown channel " + std::string{channel});
    return res[0][0] == "1";
}

std::vector<std::string> Database::fetch_channel_description(std::string_view channel) const
{
    std::vector<std::string> retval;
    auto sql =
        "SELECT relays.FULLNAME FROM relays \n"
        "WHERE CHANNEL = ?";
    auto res = sql_.exec(sql, {channel});
    if (res.empty())
        throw std::runtime_error("Unknown channel " + std::string{channel});
    for (const auto &row : res)
        retval.emplace_back(row[0]);
    return retval;
}

void Database::update_channel(std::string_view channel, bool state)
{
    auto now = std::to_string(to_timestamp(get_time_now()));
    auto state_str = std::to_string(state);
    auto update_sql =
        "UPDATE relays \n"
        "SET STATE = ?, LAST_UPDATE = ? \n"
        "WHERE CHANNEL = ? AND STATE <> ?";
    sql_.exec(update_sql, {state_str,
                           now,
                           channel,
                           state_str});
}
