#include "client.hpp"

#include "db.hpp"

namespace sql {
    client::~client() { _db->release(); }

    bool client::exec(const std::string& sql) const { return _db->exec(sql); }
} // namespace sql
