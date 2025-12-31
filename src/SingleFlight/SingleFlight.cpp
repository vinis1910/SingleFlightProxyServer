#include "SingleFlight.hpp"
#include <spdlog/spdlog.h>
#include <thread>

SingleFlight::Result SingleFlight::doSingleFlight(
    const std::string& key,
    std::function<void(const std::string&)> on_result) {
    
    std::shared_ptr<Flight> flight;
    bool is_new_flight = false;
    
    {
        std::lock_guard<std::mutex> lock(flights_mutex_);
        auto it = flights_.find(key);
        if (it != flights_.end()) {
            flight = it->second;
            {
                std::lock_guard<std::mutex> flight_lock(flight->mutex);
                if (flight->ready) {
                    flights_.erase(it);
                    flight = std::make_shared<Flight>();
                    flights_[key] = flight;
                    is_new_flight = true;
                }
            }
        } else {
            flight = std::make_shared<Flight>();
            flights_[key] = flight;
            is_new_flight = true;
        }
    }
    
    if (!is_new_flight) {
        std::lock_guard<std::mutex> lock(flight->mutex);
        if (flight->ready) {
            on_result(flight->result);
            return Result::IS_WAITER;
        }
        flight->waiters++;
        flight->callbacks.push_back(on_result);
        spdlog::info("[SingleFlight] Session waiting for key: {} ({} waiters)", 
                    key, flight->waiters);
        return Result::IS_WAITER;
    }
    
    spdlog::info("[SingleFlight] Session is leader for key: {}", key);
    return Result::IS_LEADER;
}

void SingleFlight::notifyResult(const std::string& key, const std::string& result) {
    std::shared_ptr<Flight> flight;
    {
        std::lock_guard<std::mutex> lock(flights_mutex_);
        auto it = flights_.find(key);
        if (it == flights_.end()) {
            spdlog::warn("[SingleFlight] notifyResult: No flight found for key: {}", key);
            return;
        }
        flight = it->second;
    }
    
    std::vector<std::function<void(const std::string&)>> callbacks_to_notify;
    int num_waiters = 0;
    {
        std::lock_guard<std::mutex> lock(flight->mutex);
        flight->result = result;
        flight->ready = true;
        callbacks_to_notify = flight->callbacks;
        num_waiters = flight->waiters;
    }
    

    for (auto& callback : callbacks_to_notify) {
        try {
            callback(result);
        } catch (const std::exception& e) {
            spdlog::error("[SingleFlight] Callback error: {}", e.what());
        }
    }
    
    spdlog::info("[SingleFlight] Notified {} waiters for key: {}", num_waiters, key);
    
    {
        std::lock_guard<std::mutex> lock(flights_mutex_);
        flights_.erase(key);
    }
}

void SingleFlight::clear() {
    std::lock_guard<std::mutex> lock(flights_mutex_);
    flights_.clear();
    spdlog::info("[SingleFlight] All flights cleared");
}

