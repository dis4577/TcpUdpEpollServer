# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -I./ -pthread

# Directories
SRC_DIR := Src
HEADERS_DIR := Headers
BUILD_DIR := Build
TARGET_DIR := $(BUILD_DIR)

# Source files
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC_FILES))

# Target executable
TARGET := $(TARGET_DIR)/server

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJ_FILES)
	@mkdir -p $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

# Compile source files to object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)/*

# Phony targets (not actual files)
.PHONY: all clean