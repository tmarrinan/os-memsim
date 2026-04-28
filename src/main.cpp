#include "mmu.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

class CommandParser {
private:
    Mmu* mmu;
    
public:
    CommandParser(Mmu* m) : mmu(m) {}
    
    void parseAndExecute(const std::string& command_line) {
        std::istringstream iss(command_line);
        std::string command;
        iss >> command;
        
        if (command == "create") {
            handleCreate(iss);
        } else if (command == "allocate") {
            handleAllocate(iss);
        } else if (command == "set") {
            handleSet(iss);
        } else if (command == "free") {
            handleFree(iss);
        } else if (command == "terminate") {
            handleTerminate(iss);
        } else if (command == "print") {
            handlePrint(iss);
        } else if (command == "exit") {
            exit(0);
        } else if (command.empty()) {
            // Ignore empty lines
            return;
        } else {
            std::cout << "error: command not recognized" << std::endl;
        }
    }
    
private:
    void handleCreate(std::istringstream& iss) {
        uint32_t text_size, data_size;
        if (!(iss >> text_size >> data_size)) {
            std::cout << "Usage: create <text_size> <data_size>" << std::endl;
            return;
        }
        
        uint32_t pid = mmu->createProcess(text_size, data_size);
        if (pid != 0) {
            std::cout << pid << std::endl;
        }
    }
    
    void handleAllocate(std::istringstream& iss) {
        uint32_t pid;
        std::string var_name, type_str;
        uint32_t num_elements;
        
        if (!(iss >> pid >> var_name >> type_str >> num_elements)) {
            std::cout << "Usage: allocate <PID> <var_name> <data_type> <number_of_elements>" << std::endl;
            return;
        }
        
        if (!mmu->isValidPID(pid)) {
            std::cout << "error: process not found" << std::endl;
            return;
        }
        
        DataType type;
        try {
            type = mmu->stringToDataType(type_str);
        } catch (const std::invalid_argument&) {
            std::cout << "error: invalid data type" << std::endl;
            return;
        }
        
        // Check if variable already exists
        Process* proc = mmu->getProcess(pid);
        if (proc && proc->variables.find(var_name) != proc->variables.end()) {
            std::cout << "error: variable already exists" << std::endl;
            return;
        }
        
        uint32_t virtual_address = mmu->allocateMemory(pid, var_name, type, num_elements);
        if (virtual_address != 0) {
            std::cout << virtual_address << std::endl;
        }
    }
    
    void handleSet(std::istringstream& iss) {
        uint32_t pid, offset;
        std::string var_name;
        
        if (!(iss >> pid >> var_name >> offset)) {
            std::cout << "Usage: set <PID> <var_name> <offset> <value_0> <value_1> ... <value_N>" << std::endl;
            return;
        }
        
        if (!mmu->isValidPID(pid)) {
            std::cout << "error: process not found" << std::endl;
            return;
        }
        
        // Read remaining values
        std::vector<std::string> values;
        std::string value;
        while (iss >> value) {
            values.push_back(value);
        }
        
        if (values.empty()) {
            std::cout << "At least one value must be provided" << std::endl;
            return;
        }
        
        try {
            mmu->setMemory(pid, var_name, offset, values);
        } catch (const std::invalid_argument& e) {
            std::string error_msg = e.what();
            if (error_msg == "variable not found") {
                std::cout << "error: variable not found" << std::endl;
            } else if (error_msg == "index out of range") {
                std::cout << "error: index out of range" << std::endl;
            } else {
                std::cout << "error: " << error_msg << std::endl;
            }
        }
    }
    
    void handleFree(std::istringstream& iss) {
        uint32_t pid;
        std::string var_name;
        
        if (!(iss >> pid >> var_name)) {
            std::cout << "Usage: free <PID> <var_name>" << std::endl;
            return;
        }
        
        if (!mmu->isValidPID(pid)) {
            std::cout << "error: process not found" << std::endl;
            return;
        }
        
        // Check if variable exists before calling deallocate
        Process* proc = mmu->getProcess(pid);
        if (!proc || proc->variables.find(var_name) == proc->variables.end()) {
            std::cout << "error: variable not found" << std::endl;
            return;
        }
        
        mmu->deallocateMemory(pid, var_name);
    }
    
    void handleTerminate(std::istringstream& iss) {
        uint32_t pid;
        if (!(iss >> pid)) {
            std::cout << "Usage: terminate <PID>" << std::endl;
            return;
        }
        
        if (!mmu->terminateProcess(pid)) {
            std::cout << "error: process not found" << std::endl;
        }
    }
    
    void handlePrint(std::istringstream& iss) {
        std::string object;
        if (!(iss >> object)) {
            std::cout << "Usage: print <object>" << std::endl;
            return;
        }
        
        if (object == "mmu") {
            mmu->printMMU();
        } else if (object == "page") {
            mmu->printPageTable();
        } else if (object == "processes") {
            mmu->printProcesses();
        } else {
            // Check if it's in format PID:var_name
            size_t colon_pos = object.find(':');
            if (colon_pos != std::string::npos) {
                try {
                    uint32_t pid = std::stoi(object.substr(0, colon_pos));
                    std::string var_name = object.substr(colon_pos + 1);
                    mmu->printVariable(pid, var_name);
                } catch (const std::exception& e) {
                    std::cout << "Invalid format. Use: <PID>:<var_name>" << std::endl;
                }
            } else {
                std::cout << "Unknown object: " << object << std::endl;
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <page_size>" << std::endl;
        std::cout << "Page size must be power of 2 between 1024 and 32768" << std::endl;
        return 1;
    }
    
    uint32_t page_size;
    try {
        page_size = std::stoi(argv[1]);
    } catch (...) {
        std::cout << "Error: invalid page size argument" << std::endl;
        return 1;
    }

    if (page_size < 1024 || page_size > 32768 || (page_size & (page_size - 1)) != 0) {
        std::cout << "Error: page size must be a power of 2 between 1024 and 32768" << std::endl;
        return 1;
    }
    
    Mmu mmu(page_size);
    CommandParser parser(&mmu);
    
    std::cout << "Welcome to the Memory Allocation Simulator! Using a page size of " << page_size << " bytes." << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  * create <text_size> <data_size> (initializes a new process)" << std::endl;
    std::cout << "  * allocate <PID> <var_name> <data_type> <number_of_elements> (allocated memory on the heap)" << std::endl;
    std::cout << "  * set <PID> <var_name> <offset> <value_0> <value_1> <value_2> ... <value_N> (set the value for a variable)" << std::endl;
    std::cout << "  * free <PID> <var_name> (deallocate memory on the heap that is associated with <var_name>)" << std::endl;
    std::cout << "  * terminate <PID> (kill the specified process)" << std::endl;
    std::cout << "  * print <object> (prints data)" << std::endl;
    std::cout << "    * If <object> is \"mmu\", print the MMU memory table" << std::endl;
    std::cout << "    * if <object> is \"page\", print the page table" << std::endl;
    std::cout << "    * if <object> is \"processes\", print a list of PIDs for processes that are still running" << std::endl;
    std::cout << "    * if <object> is a \"<PID>:<var_name>\", print the value of the variable for that process" << std::endl;
    std::cout << std::endl;
    
    std::string command_line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, command_line)) break;
        parser.parseAndExecute(command_line);
    }
    
    return 0;
}