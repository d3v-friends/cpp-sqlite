#include <map>
#include <mutex>
#include <string>

#include "client.hpp"
#include "db.hpp"

namespace sql {
    client::~client() {
        _queries.clear();
        _db->release();
    }

    bool client::exec(const std::string& sql) const { return _db->exec(sql); }

    bool client::release_cursor(const cursor_id id) {
        std::scoped_lock lock{_mutex};
        return _queries.erase(id);
    }
} // namespace sql
