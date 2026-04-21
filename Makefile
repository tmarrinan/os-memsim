CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET = mmu-simulator
SOURCES = main.cpp mmu.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "Testing MMU Simulator..."
	@echo "Creating test script..."
	@echo "create 2048 512" > test_commands.txt
	@echo "allocate 1024 my_array int 10" >> test_commands.txt
	@echo "set 1024 my_array 0 1 2 3 4" >> test_commands.txt
	@echo "print 1024:my_array" >> test_commands.txt
	@echo "print processes" >> test_commands.txt
	@echo "print mmu" >> test_commands.txt
	@echo "exit" >> test_commands.txt
	@echo "Running test with 4096 byte page size..."
	./$(TARGET) 4096 < test_commands.txt
	@rm -f test_commands.txt

help:
	@echo "Available targets:"
	@echo "  all     - Build the simulator"
	@echo "  clean   - Remove compiled files"
	@echo "  test    - Run basic tests"
	@echo "  help    - Show this help"

.PHONY: all clean test help
