#include "QueryCache.hpp"
#include "../SingleFlight/SingleFlight.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include <shared_mutex>
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>

#ifdef HAVE_HIREDIS
#include <hiredis/hiredis.h>
#define REDIS_AVAILABLE
#endif

QueryCache& QueryCache::getInstance() {
    static QueryCache instance;
    return instance;
}

QueryCache::QueryCache()
    : redis_port_(6379),
      redis_timeout_ms_(1000),
      redis_enabled_(false),
      singleflight_(std::make_unique<SingleFlight>()) {
#ifdef HAVE_HIREDIS
    redis_ctx_ = nullptr;
#endif
    connectRedis();
}

QueryCache::~QueryCache() {
    disconnectRedis();
}

void QueryCache::setRedisConfig(const std::string& host, int port, int timeout_ms) {
    std::unique_lock<std::shared_mutex> lock(redis_mutex_);
    redis_host_ = host;
    redis_port_ = port;
    redis_timeout_ms_ = timeout_ms;

#ifdef REDIS_AVAILABLE
    if (redis_ctx_) {
        disconnectRedis();
    }
#endif
    connectRedis();
}

void QueryCache::setL1MaxSize(size_t max_size) {
    std::unique_lock<std::shared_mutex> lock(l1_mutex_);
    l1_max_size_ = max_size;
    spdlog::info("[QueryCache] L1 max size set to {}", max_size);
}

void QueryCache::setL1Enabled(bool enabled) {
    std::unique_lock<std::shared_mutex> lock(l1_mutex_);
    l1_enabled_ = enabled;
    if (!enabled) {
        l1_list_.clear();
        l1_map_.clear();
    }
    spdlog::info("[QueryCache] L1 cache {}", enabled ? "enabled" : "disabled");
}

std::string QueryCache::hash_query(const std::string& query) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)query.c_str(), query.length(), digest);
    
    std::stringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }
    return ss.str();
}

std::optional<std::string> QueryCache::get(const std::string& query) {
    std::string key = hash_query(query);
    auto l1_result = getL1(key);
    if (l1_result.has_value()) {
        updateStats(true, false);
        return l1_result;
    }

    updateStats(false, false);

    if (redis_enabled_) {
        auto l2_result = getL2(key);
        if (l2_result.has_value()) {
            putL1(key, l2_result.value());
            updateStats(false, true);
            return l2_result;
        }
        updateStats(false, false);
    }

    return std::nullopt;
}

void QueryCache::put(const std::string& query, const std::string& result) {
    std::string key = hash_query(query);
    putL1(key, result);

    if (redis_enabled_) {
        putL2(key, result);
    }
}

QueryCache::FlightResult QueryCache::doSingleFlight(const std::string& query, 
                                std::function<void(const std::string&)> on_result) {
    auto cached = get(query);
    if (cached.has_value()) {
        spdlog::debug("[QueryCache] Cache HIT for query: {}", query);
        on_result(cached.value());
        return FlightResult::CACHE_HIT;
    }
    
    std::string key = hash_query(query);
    auto result = singleflight_->doSingleFlight(key, on_result);
    
    if (result == SingleFlight::Result::IS_LEADER) {
        return FlightResult::IS_LEADER;
    } else {
        return FlightResult::IS_WAITER;
    }
}

void QueryCache::notifyFlightResult(const std::string& query, const std::string& result) {
    put(query, result);
    
    std::string key = hash_query(query);
    singleflight_->notifyResult(key, result);
}

void QueryCache::clear() {
    {
        std::unique_lock<std::shared_mutex> lock(l1_mutex_);
        l1_list_.clear();
        l1_map_.clear();
    }

#ifdef REDIS_AVAILABLE
    if (redis_enabled_) {
        std::unique_lock<std::shared_mutex> lock(redis_mutex_);
        if (redis_ctx_) {
            redisReply* reply = (redisReply*)redisCommand(redis_ctx_, "FLUSHDB");
            if (reply) {
                freeReplyObject(reply);
            }
        }
    }
#endif

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = Stats{};
    }

    spdlog::info("[QueryCache] Cache cleared");
}

std::optional<std::string> QueryCache::getL1(const std::string& query) {
    if (!l1_enabled_) {
        return std::nullopt;
    }
    
    {
        std::shared_lock<std::shared_mutex> lock(l1_mutex_);
        auto it = l1_map_.find(query);
        if (it == l1_map_.end()) {
            return std::nullopt;
        }
    }
    
    std::unique_lock<std::shared_mutex> lock(l1_mutex_);
    auto it = l1_map_.find(query);
    if (it == l1_map_.end()) {
        return std::nullopt;
    }
    
    l1_list_.splice(l1_list_.end(), l1_list_, it->second);
    
    return it->second->value;
}

void QueryCache::putL1(const std::string& query, const std::string& result) {
    if (!l1_enabled_) {
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(l1_mutex_);

    auto it = l1_map_.find(query);
    if (it != l1_map_.end()) {
        it->second->value = result;
        l1_list_.splice(l1_list_.end(), l1_list_, it->second);
        return;
    }

    if (l1_list_.size() >= l1_max_size_) {
        auto oldest = l1_list_.begin();
        l1_map_.erase(oldest->key);
        l1_list_.pop_front();
    }

    l1_list_.push_back({query, result});
    l1_map_[query] = std::prev(l1_list_.end());
}

std::optional<std::string> QueryCache::getL2(const std::string& query) {
#ifdef REDIS_AVAILABLE
    std::shared_lock<std::shared_mutex> lock(redis_mutex_);

    if (!redis_ctx_ || !redis_enabled_) {
        return std::nullopt;
    }

    std::string key = "query:" + query;

    redisReply* reply = (redisReply*)redisCommand(redis_ctx_, "GET %s", redis_key.c_str());
    if (!reply) {
        lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(redis_mutex_);
        
        disconnectRedis();
        if (connectRedis() && redis_ctx_) {
            reply = (redisReply*)redisCommand(redis_ctx_, "GET %s", redis_key.c_str());
        }
        
        if (!reply) {
            return std::nullopt;
        }
    }

    if (!reply) {
        return std::nullopt;
    }

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReplyObject(reply);
    return result;
#else
    (void)query;
    return std::nullopt;
#endif
}

void QueryCache::putL2(const std::string& key, const std::string& result) {
#ifdef REDIS_AVAILABLE
    std::unique_lock<std::shared_mutex> lock(redis_mutex_);

    if (!redis_ctx_ || !redis_enabled_) {
        return;
    }

    std::string redis_key = "query:" + key;

    redisReply* reply = (redisReply*)redisCommand(redis_ctx_, "SETEX %s 3600 %b", 
                                                   redis_key.c_str(), result.data(), result.size());
    if (!reply) {
        disconnectRedis();
        if (connectRedis() && redis_ctx_) {
            reply = (redisReply*)redisCommand(redis_ctx_, "SETEX %s 3600 %b", 
                                             redis_key.c_str(), result.data(), result.size());
        }
    }

    if (reply) {
        freeReplyObject(reply);
    }
#else
    (void)key;
    (void)result;
#endif
}

bool QueryCache::connectRedis() {
#ifdef REDIS_AVAILABLE
    if (redis_host_.empty()) {
        redis_enabled_ = false;
        return false;
    }

    struct timeval timeout = {redis_timeout_ms_ / 1000, (redis_timeout_ms_ % 1000) * 1000};
    redis_ctx_ = redisConnectWithTimeout(redis_host_.c_str(), redis_port_, timeout);

    if (!redis_ctx_ || redis_ctx_->err) {
        if (redis_ctx_) {
            spdlog::warn("[QueryCache] Redis connection failed: {}", redis_ctx_->errstr);
            redisFree(redis_ctx_);
            redis_ctx_ = nullptr;
        } else {
            spdlog::warn("[QueryCache] Redis connection failed: cannot allocate context");
        }
        redis_enabled_ = false;
        return false;
    }

    redis_enabled_ = true;
    spdlog::info("[QueryCache] Redis connected to {}:{}", redis_host_, redis_port_);
    return true;
#else
    redis_enabled_ = false;
    spdlog::warn("[QueryCache] Redis support not compiled (hiredis not available)");
    return false;
#endif
}

void QueryCache::disconnectRedis() {
#ifdef REDIS_AVAILABLE
    if (redis_ctx_) {
        redisFree(redis_ctx_);
        redis_ctx_ = nullptr;
    }
#endif
    redis_enabled_ = false;
}

void QueryCache::updateStats(bool l1_hit, bool l2_hit) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (l1_hit) {
        stats_.l1_hits++;
    } else {
        stats_.l1_misses++;
        if (l2_hit) {
            stats_.l2_hits++;
        } else {
            stats_.l2_misses++;
        }
    }
}

QueryCache::Stats QueryCache::getStats() const {
    std::shared_lock<std::shared_mutex> lock1(l1_mutex_);
    std::lock_guard<std::mutex> lock2(stats_mutex_);
    
    Stats stats = stats_;
    stats.l1_size = l1_map_.size();
    return stats;
}
