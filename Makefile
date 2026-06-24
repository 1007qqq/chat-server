CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG
CPPFLAGS ?= -Icpp_server/include
LDFLAGS ?=
LDLIBS ?= -pthread

BUILD_DIR := build/cpp
TARGET := $(BUILD_DIR)/chat_server
SOURCES := cpp_server/src/main.cpp cpp_server/src/http.cpp cpp_server/src/app.cpp
OBJECTS := $(patsubst cpp_server/src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all run clean frontend

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: cpp_server/src/%.cpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

frontend:
	cd frontend && npm install && npm run build

clean:
	rm -rf $(BUILD_DIR)
