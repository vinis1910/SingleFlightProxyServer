#pragma once

#include <unordered_map>
#include <list>
#include <string>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <sstream>
#include <iomanip>

#ifdef HAVE_HIREDIS
struct redisContext;
struct redisReply;
#endif

class SingleFlight;

class QueryCache {
public:
    static QueryCache& getInstance();

    std::optional<std::string> get(const std::string& query);

    void put(const std::string& query, const std::string& result);
    
    enum class FlightResult {
        CACHE_HIT,
        IS_LEADER,
        IS_WAITER
    };
    
    FlightResult doSingleFlight(const std::string& query, 
                                std::function<void(const std::string&)> on_result);
    
    void notifyFlightResult(const std::string& query, const std::string& result);

    void clear();

    void setRedisConfig(const std::string& host, int port, int timeout_ms = 1000);
    void setL1MaxSize(size_t max_size);
    void setL1Enabled(bool enabled);

    struct Stats {
        size_t l1_hits = 0;
        size_t l1_misses = 0;
        size_t l2_hits = 0;
        size_t l2_misses = 0;
        size_t l1_size = 0;
    };
    Stats getStats() const;

    QueryCache(const QueryCache&) = delete;
    QueryCache& operator=(const QueryCache&) = delete;

private:
    QueryCache();
    ~QueryCache();

    size_t l1_max_size_ = 1000;
    bool l1_enabled_ = true;

    struct LRUNode {
        std::string key;
        std::string value;
    };

    using LRUList = std::list<LRUNode>;
    using LRUMap = std::unordered_map<std::string, LRUList::iterator>;

    LRUList l1_list_;
    LRUMap l1_map_;
    mutable std::shared_mutex l1_mutex_;

#ifdef HAVE_HIREDIS
    redisContext* redis_ctx_;
#endif
    std::string redis_host_;
    int redis_port_;
    int redis_timeout_ms_;
    bool redis_enabled_;
    mutable std::shared_mutex redis_mutex_;

    mutable Stats stats_;
    mutable std::mutex stats_mutex_;

    std::optional<std::string> getL1(const std::string& query);
    void putL1(const std::string& query, const std::string& result);

    std::optional<std::string> getL2(const std::string& query);
    void putL2(const std::string& query, const std::string& result);

    bool connectRedis();
    void disconnectRedis();

    void updateStats(bool l1_hit, bool l2_hit) const;
    
    static std::string hash_query(const std::string& query);
    
    std::unique_ptr<SingleFlight> singleflight_;
};
