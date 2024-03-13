#pragma once

#include <string_view>
#include <vector>
#include <variant>
#include "utils.h"

struct sqlite3;
class SQLite
{
public:
    class Transaction
    {
        friend SQLite;

    private:
        Transaction(SQLite &);

    public:
        ~Transaction();
        void success();
        void failure();

    private:
        SQLite &db_;
        bool success_{false};
    };

    using SqlRow = std::vector<std::string>;
    using Param = std::variant<std::string_view, int64_t, double, timepoint>;
    using Params = std::vector<Param>;
    SQLite(std::string_view path);
    ~SQLite();
    Transaction transaction();
    void exec_wo_return(std::string_view req) const;
    std::vector<SqlRow> exec(std::string_view req,
                             const Params &param = {}) const;

protected:
    sqlite3 *db_;
};