#ifndef LOGLITE_READER_DATABASE_HPP_
#define LOGLITE_READER_DATABASE_HPP_

#include "database.hpp"
#include "database_catalog.hpp"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace loglite {

class ReaderDatabase final : public Database {
   public:
    ReaderDatabase(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog);

    void Open();

    PaginatedQueryResult Query(const std::vector<std::string>& fields,
                               const std::vector<QueryFilter>& filters, int limit,
                               int offset) const;

    StatsQueryResult QueryActivityStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;
    StatsQueryResult QueryDatabaseStats(std::string_view since, std::string_view until,
                                        const std::vector<std::string>& fields,
                                        std::string_view ordering) const;

    bool Ping() const;
};

class ReadDatabasePool {
   public:
    ReadDatabasePool(const Config& cfg, std::shared_ptr<DatabaseCatalog> catalog, size_t size);
    ~ReadDatabasePool();

    ReadDatabasePool(const ReadDatabasePool&) = delete;
    ReadDatabasePool& operator=(const ReadDatabasePool&) = delete;

    template <class F>
    auto with_connection(F&& f) -> decltype(f(std::declval<ReaderDatabase&>())) {
        ReaderDatabase& db = acquire();
        try {
            if constexpr (std::is_void_v<decltype(f(db))>) {
                f(db);
                release(db);
            } else {
                auto result = f(db);
                release(db);
                return result;
            }
        } catch (...) {
            release(db);
            throw;
        }
    }

    void Close();

   private:
    ReaderDatabase& acquire();
    void release(ReaderDatabase& db);

    std::vector<std::unique_ptr<ReaderDatabase>> readers_;
    std::queue<ReaderDatabase*> available_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool closed_{false};
};

}  // namespace loglite

#endif  // LOGLITE_READER_DATABASE_HPP_
