CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread -I./include
LDFLAGS = -pthread

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests

# Source files for proxy
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# Server objects exclude load_balancer.o
SERVER_OBJS = $(filter-out $(OBJ_DIR)/load_balancer.o, $(OBJS))

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.cpp, $(OBJ_DIR)/tests_%.o, $(TEST_SRCS))
NON_MAIN_OBJS = $(filter-out $(OBJ_DIR)/main.o $(OBJ_DIR)/load_balancer.o, $(OBJS))

TARGET = $(BIN_DIR)/proxy_server
TEST_TARGET = $(BIN_DIR)/test_runner
LB_TARGET = $(BIN_DIR)/load_balancer

.PHONY: all clean test dirs

all: dirs $(TARGET) $(LB_TARGET)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/tests_%.o: $(TEST_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(SERVER_OBJS)
	$(CXX) $(SERVER_OBJS) -o $@ $(LDFLAGS)
	@echo "Built $(TARGET) successfully."

$(LB_TARGET): $(OBJ_DIR)/load_balancer.o $(OBJ_DIR)/http_parser.o
	$(CXX) $(OBJ_DIR)/load_balancer.o $(OBJ_DIR)/http_parser.o -o $@ $(LDFLAGS)
	@echo "Built $(LB_TARGET) successfully."

$(TEST_TARGET): dirs $(NON_MAIN_OBJS) $(TEST_OBJS)
	$(CXX) $(NON_MAIN_OBJS) $(TEST_OBJS) -o $@ $(LDFLAGS)
	@echo "Built $(TEST_TARGET) successfully."

test: $(TEST_TARGET)
	@echo "Running tests..."
	./$(TEST_TARGET)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
