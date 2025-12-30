#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>

class SingleFlight {
public:
    enum class Result {
        IS_LEADER,
        IS_WAITER
    };

    SingleFlight() = default;
    ~SingleFlight() = default;

    SingleFlight(const SingleFlight&) = delete;
    SingleFlight& operator=(const SingleFlight&) = delete;

    SingleFlight(SingleFlight&&) = delete;
    SingleFlight& operator=(SingleFlight&&) = delete;

    Result doSingleFlight(const std::string& key, 
                         std::function<void(const std::string&)> on_result);

    void notifyResult(const std::string& key, const std::string& result);

    void clear();

private:
    struct Flight {
        std::mutex mutex;
        std::string result;
        bool ready = false;
        int waiters = 0;
        std::vector<std::function<void(const std::string&)>> callbacks;
    };

    std::unordered_map<std::string, std::shared_ptr<Flight>> flights_;
    std::mutex flights_mutex_;
};

