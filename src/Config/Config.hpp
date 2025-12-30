#pragma once

#include <string>
#include <optional>
#include <yaml-cpp/yaml.h>

class Config {
public:
    static Config& getInstance();
    
    bool loadFromFile(const std::string& config_path = "config.yaml");
    bool loadFromString(const std::string& yaml_content);
    
    unsigned short getListenPort() const { return listen_port_; }
    std::string getListenAddress() const { return listen_address_; }
    unsigned int getNumThreads() const { return num_threads_; }
    
    std::string getDbHost() const { return db_host_; }
    unsigned short getDbPort() const { return db_port_; }
    
    size_t getPoolMinSize() const { return pool_min_size_; }
    size_t getPoolMaxSize() const { return pool_max_size_; }
    size_t getPoolIdleTimeout() const { return pool_idle_timeout_seconds_; }
    
    size_t getL1MaxSize() const { return l1_max_size_; }
    bool isL1Enabled() const { return l1_enabled_; }
    
    bool isRedisEnabled() const { return redis_enabled_; }
    std::string getRedisHost() const { return redis_host_; }
    int getRedisPort() const { return redis_port_; }
    int getRedisTimeout() const { return redis_timeout_ms_; }
    
    std::string getLogLevel() const { return log_level_; }
    std::string getLogPattern() const { return log_pattern_; }
    
    bool isSslEnabled() const { return ssl_enabled_; }
    
    bool isValid() const { return valid_; }
    std::string getError() const { return error_message_; }
    
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

private:
    Config();
    
    void setDefaults();
    bool parseYaml(const YAML::Node& config);
    
    unsigned short listen_port_ = 6000;
    std::string listen_address_ = "0.0.0.0";
    unsigned int num_threads_ = 4;
    
    std::string db_host_ = "127.0.0.1";
    unsigned short db_port_ = 5432;
    
    size_t pool_min_size_ = 5;
    size_t pool_max_size_ = 20;
    size_t pool_idle_timeout_seconds_ = 300;
    
    size_t l1_max_size_ = 1000;
    bool l1_enabled_ = true;
    
    bool redis_enabled_ = false;
    std::string redis_host_ = "127.0.0.1";
    int redis_port_ = 6379;
    int redis_timeout_ms_ = 1000;
    
    std::string log_level_ = "info";
    std::string log_pattern_ = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v";
    
    bool ssl_enabled_ = true;
    
    bool valid_ = false;
    std::string error_message_;
};