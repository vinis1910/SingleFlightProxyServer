CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -g
LDFLAGS = -pthread

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

SOURCES = $(SRC_DIR)/main.cpp \
          $(SRC_DIR)/ProxyServer/ProxyServer.cpp \
          $(SRC_DIR)/Session/Session.cpp

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
    CXXFLAGS += $(shell pkg-config --cflags boost spdlog fmt 2>/dev/null || echo "-I/usr/include")
    LDFLAGS += $(shell pkg-config --libs boost spdlog fmt 2>/dev/null || echo "-lspdlog -lfmt -lboost_system")
else ifeq ($(UNAME_S),Darwin)
    CXXFLAGS += -I/usr/local/include -I/opt/homebrew/include
    LDFLAGS += -L/usr/local/lib -L/opt/homebrew/lib
endif

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	@echo "Linking $@..."
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete! Run with: ./$@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@echo "Compiling $<..."
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)/ProxyServer
	@mkdir -p $(BUILD_DIR)/Session

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

clean:
	@echo "Limpando arquivos de build..."
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Limpeza completa!"

rebuild: clean all

run: $(TARGET)
	@echo "Running proxy server..."
	./$(TARGET)

.PHONY: all clean rebuild run help

