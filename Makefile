CXX= g++
CXXFLAGS= -std=c++17 -Wall -Wextra -O2
TARGET= server
SRC=server.cpp
BIN_DIR=bin
BIN= $(BIN_DIR)/$(TARGET)

all: $(BIN)

$(BIN): $(SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)
	
run: $(BIN)
	./$(BIN)

test:
	@chmod +x stress_test.sh
	@./stress_test.sh

clean:
	rm -f $(BIN)

