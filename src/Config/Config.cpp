#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

Config::Config() {
    setDefaults();
}

void Config::setDefaults() {
    valid_ = true;
}

bool Config::loadFromFile(const std::string& config_path) {
    try {
        YAML::Node config_node = YAML::LoadFile(config_path);
        return parseYaml(config_node);
    } catch (const YAML::BadFile& e) {
        spdlog::warn("Config file '{}' not found, using defaults", config_path);
        setDefaults();
        return true;
    } catch (const YAML::Exception& e) {
        error_message_ = "YAML parse error: " + std::string(e.what());
        spdlog::error("Failed to parse config: {}", error_message_);
        valid_ = false;
        return false;
    }
}

bool Config::loadFromString(const std::string& yaml_content) {
    try {
        YAML::Node config_node = YAML::Load(yaml_content);
        return parseYaml(config_node);
    } catch (const YAML::Exception& e) {
        error_message_ = "YAML parse error: " + std::string(e.what());
        spdlog::error("Failed to parse config: {}", error_message_);
        valid_ = false;
        return false;
    }
}

bool Config::parseYaml(const YAML::Node& config) {
    try {
        if (config["server"]) {
            const auto& server = config["server"];
            if (server["listen_port"]) {
                listen_port_ = server["listen_port"].as<unsigned short>();
            }
            if (server["listen_address"]) {
                listen_address_ = server["listen_address"].as<std::string>();
            }
            if (server["num_threads"]) {
                num_threads_ = server["num_threads"].as<unsigned int>();
            }
        }
        
        if (config["database"]) {
            const auto& db = config["database"];
            if (db["host"]) {
                db_host_ = db["host"].as<std::string>();
            }
            if (db["port"]) {
                db_port_ = db["port"].as<unsigned short>();
            }
            
            if (db["pool"]) {
                const auto& pool = db["pool"];
                if (pool["min_size"]) {
                    pool_min_size_ = pool["min_size"].as<size_t>();
                }
                if (pool["max_size"]) {
                    pool_max_size_ = pool["max_size"].as<size_t>();
                }
                if (pool["idle_timeout_seconds"]) {
                    pool_idle_timeout_seconds_ = pool["idle_timeout_seconds"].as<size_t>();
                }
            }
        }
        
        if (config["cache"]) {
            const auto& cache = config["cache"];
            
            if (cache["l1"]) {
                const auto& l1 = cache["l1"];
                if (l1["max_size"]) {
                    l1_max_size_ = l1["max_size"].as<size_t>();
                }
                if (l1["enabled"]) {
                    l1_enabled_ = l1["enabled"].as<bool>();
                }
            }
            
            if (cache["l2"] && cache["l2"]["redis"]) {
                const auto& redis = cache["l2"]["redis"];
                if (redis["enabled"]) {
                    redis_enabled_ = redis["enabled"].as<bool>();
                }
                if (redis["host"]) {
                    redis_host_ = redis["host"].as<std::string>();
                }
                if (redis["port"]) {
                    redis_port_ = redis["port"].as<int>();
                }
                if (redis["timeout_ms"]) {
                    redis_timeout_ms_ = redis["timeout_ms"].as<int>();
                }
            }
        }
        
        if (config["logging"]) {
            const auto& logging = config["logging"];
            if (logging["level"]) {
                log_level_ = logging["level"].as<std::string>();
            }
            if (logging["pattern"]) {
                log_pattern_ = logging["pattern"].as<std::string>();
            }
        }
        
        if (config["ssl"]) {
            const auto& ssl = config["ssl"];
            if (ssl["enabled"]) {
                ssl_enabled_ = ssl["enabled"].as<bool>();
            }
        }
        
        valid_ = true;
        error_message_.clear();
        
        spdlog::info("Configuration loaded successfully from YAML");
        return true;
        
    } catch (const YAML::Exception& e) {
        error_message_ = "YAML parse error: " + std::string(e.what());
        spdlog::error("Failed to parse YAML config: {}", error_message_);
        valid_ = false;
        return false;
    } catch (const std::exception& e) {
        error_message_ = "Config error: " + std::string(e.what());
        spdlog::error("Failed to load config: {}", error_message_);
        valid_ = false;
        return false;
    }
}