CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -g -Wno-array-bounds
LDFLAGS = -pthread -lssl -lcrypto

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

SOURCES = $(SRC_DIR)/main.cpp \
          $(SRC_DIR)/ProxyServer/ProxyServer.cpp \
          $(SRC_DIR)/Session/Session.cpp \
          $(SRC_DIR)/QueryCache/QueryCache.cpp \
          $(SRC_DIR)/SingleFlight/SingleFlight.cpp \
          $(SRC_DIR)/Config/Config.cpp

OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/singleflight-proxy

UNAME_S := $(shell uname -s 2>/dev/null || echo "Windows")
BOOST_ROOT ?= $(shell find /c/boost /c/msys64/mingw64 /usr/local /opt/homebrew -type d -name "boost" -o -path "*/boost/include" 2>/dev/null | head -1)
BOOST_INCLUDE ?= $(shell find /c/boost /c/msys64/mingw64 /usr/local /opt/homebrew -type d -name "boost" -path "*/include/boost" 2>/dev/null | head -1)

ifeq ($(UNAME_S),Windows)
    ifneq ($(BOOST_INCLUDE),)
        CXXFLAGS += -I$(dir $(BOOST_INCLUDE))
    else ifneq ($(BOOST_ROOT),)
        CXXFLAGS += -I$(BOOST_ROOT)/include
    else
        CXXFLAGS += -I/c/boost/include -I/c/msys64/mingw64/include
        LDFLAGS += -L/c/boost/lib -L/c/msys64/mingw64/lib
    endif
    TARGET := $(TARGET).exe
else ifeq ($(UNAME_S),Linux)
    HIREDIS_AVAILABLE := $(shell pkg-config --exists hiredis 2>/dev/null && echo "yes" || echo "no")
    YAMLCPP_AVAILABLE := $(shell pkg-config --exists yaml-cpp 2>/dev/null && echo "yes" || echo "no")
    
    ifeq ($(HIREDIS_AVAILABLE),yes)
        CXXFLAGS += -DHAVE_HIREDIS $(shell pkg-config --cflags boost spdlog fmt openssl hiredis 2>/dev/null || echo "-I/usr/include")
        LDFLAGS += $(shell pkg-config --libs boost spdlog fmt openssl hiredis 2>/dev/null || echo "-lspdlog -lfmt -lboost_system -lssl -lcrypto -lhiredis")
    else
        CXXFLAGS += $(shell pkg-config --cflags boost spdlog fmt openssl 2>/dev/null || echo "-I/usr/include")
        LDFLAGS += $(shell pkg-config --libs boost spdlog fmt openssl 2>/dev/null || echo "-lspdlog -lfmt -lboost_system -lssl -lcrypto")
    endif
    
    ifeq ($(YAMLCPP_AVAILABLE),yes)
        CXXFLAGS += $(shell pkg-config --cflags yaml-cpp 2>/dev/null)
        LDFLAGS += $(shell pkg-config --libs yaml-cpp 2>/dev/null || echo "-lyaml-cpp")
    else
        YAMLCPP_INCLUDE := $(shell find /usr/include /usr/local/include -name "yaml-cpp" -type d 2>/dev/null | head -1)
        ifneq ($(YAMLCPP_INCLUDE),)
            CXXFLAGS += -I$(dir $(YAMLCPP_INCLUDE))
        endif
        LDFLAGS += -lyaml-cpp
    endif
else ifeq ($(UNAME_S),Darwin)
    CXXFLAGS += -I/usr/local/include -I/opt/homebrew/include
    LDFLAGS += -L/usr/local/lib -L/opt/homebrew/lib -lssl -lcrypto -lyaml-cpp
endif

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	@echo "Linking $@"
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete! Run with: ./$@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@echo "Compiling $<"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)/ProxyServer
	@mkdir -p $(BUILD_DIR)/Session
	@mkdir -p $(BUILD_DIR)/QueryCache
	@mkdir -p $(BUILD_DIR)/SingleFlight
	@mkdir -p $(BUILD_DIR)/Config

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

clean:
	@echo "Cleaning build files"
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean complete!"

rebuild: clean all

run: $(TARGET)
	@echo "Running proxy server"
	./$(TARGET)

.PHONY: all clean rebuild run help