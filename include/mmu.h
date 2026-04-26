#ifndef MMU_H
#define MMU_H

#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <variant>
#include <sstream>
#include "pagetable.h"

const uint64_t TOTAL_MEMORY = 67108864; // 64 MB
const uint32_t STACK_SIZE = 65536;      // 64 KB
const uint32_t PID_START = 1024;

enum class DataType {
    CHAR = 1,
    SHORT = 2,
    INT = 4,
    LONG = 8,
    FLOAT = 16,
    DOUBLE = 32
};

struct Variable {
    std::string name;
    DataType type;
    uint32_t num_elements;
    uint32_t size;
    uint32_t virtual_address;
    std::vector<uint8_t> data;
    
    Variable() = default;
    
    Variable(std::string n, DataType t, uint32_t num, uint32_t va) 
        : name(n), type(t), num_elements(num), virtual_address(va) {
        uint32_t element_size = 0;
        switch (t) {
            case DataType::CHAR: element_size = 1; break;
            case DataType::SHORT: element_size = 2; break;
            case DataType::INT: element_size = 4; break;
            case DataType::FLOAT: element_size = 4; break;
            case DataType::LONG: element_size = 8; break;
            case DataType::DOUBLE: element_size = 8; break;
        }
        size = element_size * num;
        data.resize(size);
    }
};

struct Process {
    uint32_t pid;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t stack_size;
    uint32_t heap_size;
    uint32_t text_addr;
    uint32_t data_addr;
    uint32_t stack_addr;
    uint32_t heap_start;
    std::map<std::string, Variable> variables;
    
    Process() = default;
    
    Process(uint32_t id, uint32_t txt, uint32_t dat) 
        : pid(id), text_size(txt), data_size(dat), stack_size(STACK_SIZE), heap_size(0) {}
};

class Mmu {
private:
    uint32_t page_size;
    uint32_t total_pages;
    uint32_t next_pid;
    
    std::unordered_map<uint32_t, Process> processes;
    
    // The single source of truth for our pages and frames!
    PageTable pt; 
    
public:
    Mmu(uint32_t pg_size);
    
    // Process management
    uint32_t createProcess(uint32_t text_size, uint32_t data_size);
    bool terminateProcess(uint32_t pid);
    
    // Memory allocation
    uint32_t allocateMemory(uint32_t pid, const std::string& var_name, DataType type, uint32_t num_elements);
    bool deallocateMemory(uint32_t pid, const std::string& var_name);
    bool setMemory(uint32_t pid, const std::string& var_name, uint32_t offset, const std::vector<std::string>& values);
    
    // Page management
    uint32_t virtualToPhysical(uint32_t pid, uint32_t virtual_address);
    
    // Print functions
    void printMMU() const;
    void printPageTable() const;
    void printProcesses() const;
    void printVariable(uint32_t pid, const std::string& var_name) const;
    
    // Utility functions
    bool isValidPID(uint32_t pid) const;
    Process* getProcess(uint32_t pid);
    const Process* getProcess(uint32_t pid) const;
    DataType stringToDataType(const std::string& type_str) const;
    uint32_t getDataTypeSize(DataType type) const;
    
private:
    // Helper functions
    uint32_t allocatePages(uint32_t pid, uint32_t size);
    void deallocatePages(uint32_t pid, uint32_t virtual_address, uint32_t size);
    bool parseValue(const std::string& value_str, DataType type, uint8_t* buffer);
    std::string formatValue(const uint8_t* data, DataType type) const;
};

#endif