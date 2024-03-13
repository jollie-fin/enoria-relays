#include <string>
#include <string_view>
#include <stdexcept>
#include "sqlite.h"
#include <sqlite3.h>
#include <iostream>

SQLite::SQLite(std::string_view path)
{
    std::string p{path};
    sqlite3_open(p.c_str(), &db_);
}

SQLite::~SQLite()
{
    sqlite3_close(db_);
}

void SQLite::exec_wo_return(std::string_view req) const
{
    std::string r{req};
    char *err_msg = NULL;
    int ret = sqlite3_exec(db_, r.c_str(), NULL, NULL, &err_msg);
    if (ret)
        throw std::runtime_error(std::string{"SQL error :"} + sqlite3_errmsg(db_));
}

// helper type for the visitor #4
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

std::vector<SQLite::SqlRow> SQLite::exec(std::string_view req, const SQLite::Params &param) const
{
    std::vector<SqlRow> retval;
    sqlite3_stmt *stmt; // will point to prepared stamement object
    sqlite3_prepare_v2(
        db_,          // the handle to your (opened and ready) database
        req.data(),   // the sql statement, utf-8 encoded
        req.length(), // max length of sql statement
        &stmt,        // this is an "out" parameter, the compiled statement goes here
        nullptr);     // pointer to the tail end of sql statement (when there are
                      // multiple statements inside the string; can be null)
    int bind_idx = 1;
    auto expected_count = static_cast<size_t>(sqlite3_bind_parameter_count(stmt));
    auto actual_count = param.size();
    if (actual_count != expected_count)
        throw std::runtime_error(
            "In SQL (" +
            std::string{req} +
            "), expected " +
            std::to_string(expected_count) +
            " parameters, got " +
            std::to_string(actual_count));
    for (const auto &s : param)
    {
        std::visit(overloaded{[&](std::string_view s)
                              {
                                  sqlite3_bind_text(
                                      stmt,
                                      bind_idx,
                                      s.data(),
                                      s.length(),
                                      SQLITE_STATIC);
                              },
                              [&](int64_t i)
                              {
                                  sqlite3_bind_int64(
                                      stmt,
                                      bind_idx,
                                      i);
                              },
                              [&](timepoint i)
                              {
                                  sqlite3_bind_int64(
                                      stmt,
                                      bind_idx,
                                      to_timestamp(i));
                              },
                              [&](double i)
                              {
                                  sqlite3_bind_double(
                                      stmt,
                                      bind_idx,
                                      i);
                              }},
                   s);
        bind_idx++;
    }

    bool done = false;
    while (!done)
    {
        auto er = sqlite3_step(stmt);
        switch (er)
        {
        case SQLITE_ROW:
            retval.emplace_back();
            for (int i = 0; i < sqlite3_column_count(stmt); i++)
            {
                auto cell = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
                auto length = sqlite3_column_bytes(stmt, i);
                retval.back().emplace_back(cell, length);
            }
            break;
        case SQLITE_DONE:
            done = true;
            break;
        default:
            throw std::runtime_error{"Error while executing sql : " + std::string{req} + " : " + sqlite3_errmsg(db_)};
        }
    }
    sqlite3_finalize(stmt);
    return retval;
}

SQLite::Transaction SQLite::transaction()
{
    return Transaction{*this};
}

SQLite::Transaction::Transaction(SQLite &db) : db_(db), success_(false)
{
    db_.exec_wo_return("BEGIN TRANSACTION");
}
void SQLite::Transaction::failure()
{
    success_ = false;
}

void SQLite::Transaction::success()
{
    success_ = true;
}
SQLite::Transaction::~Transaction()
{
    if (success_)
        db_.exec_wo_return("COMMIT");
    else
        db_.exec_wo_return("ROLLBACK");
}