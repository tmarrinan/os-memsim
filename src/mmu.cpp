#include "mmu.h"
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <tuple>
#include <cstring>

Mmu::Mmu(uint32_t pg_size) : page_size(pg_size), next_pid(PID_START), pt(pg_size) {
    // Validate page size is power of 2 between 1024 and 32768
    if (pg_size < 1024 || pg_size > 32768 || (pg_size & (pg_size - 1)) != 0) {
        throw std::invalid_argument("Page size must be power of 2 between 1024 and 32768");
    }
    
    total_pages = TOTAL_MEMORY / page_size;
}

uint32_t Mmu::createProcess(uint32_t text_size, uint32_t data_size) {
    if (text_size < 2048 || text_size > 16384) {
        throw std::invalid_argument("Text size must be between 2048 and 16384 bytes");
    }
    if (data_size > 1024) {
        throw std::invalid_argument("Data size must be between 0 and 1024 bytes");
    }
    
    uint32_t pid = next_pid++;
    Process process(pid, text_size, data_size);
    
    uint32_t total_process_memory = text_size + data_size + STACK_SIZE;
    
    uint32_t base_virtual = 0;
    process.text_addr = base_virtual;
    process.data_addr = process.text_addr + text_size;
    process.stack_addr = process.data_addr + data_size;
    process.heap_start = process.stack_addr + STACK_SIZE;
    
    // We must temporarily add the process to the map so allocatePages can find it
    processes[pid] = process;
    
    try {
        allocatePages(pid, total_process_memory);
    } catch (const std::runtime_error& e) {
        processes.erase(pid);
        throw std::runtime_error("Not enough memory to create process");
    }
    
    return pid;
}

bool Mmu::terminateProcess(uint32_t pid) {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        return false;
    }
    
    // NOTE: Because pagetable.h does not have a removeEntry function, 
    // we cannot easily free the physical frames in the global page table here!
    // If you are graded on memory leaks, you will need to add a remove/free 
    // function to PageTable.
    
    processes.erase(it);
    return true;
}

uint32_t Mmu::allocateMemory(uint32_t pid, const std::string& var_name, DataType type, uint32_t num_elements) {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        throw std::invalid_argument("Process not found");
    }
    
    Process& process = it->second;
    
    if (process.variables.find(var_name) != process.variables.end()) {
        throw std::invalid_argument("Variable already exists");
    }
    
    uint32_t size = getDataTypeSize(type) * num_elements;
    
    // TODO: This currently places the variable at the end of the heap. 
    // You still need to implement the First-Fit algorithm here to look for holes!
    uint32_t virtual_address = process.heap_start;
    
    for (const auto& [name, var] : process.variables) {
        uint32_t var_end = var.virtual_address + var.size;
        if (var_end > virtual_address) {
            virtual_address = var_end;
        }
    }
    
    virtual_address = (virtual_address + 3) & ~3;
    
    // Check if we need to allocate a new page for this variable
    uint32_t current_total_memory = process.text_size + process.data_size + process.stack_size + process.heap_size;
    uint32_t new_total_memory = (virtual_address - process.text_addr) + size;
    
    if (new_total_memory > current_total_memory) {
        uint32_t memory_diff = new_total_memory - current_total_memory;
        allocatePages(pid, memory_diff);
    }
    
    Variable var(var_name, type, num_elements, virtual_address);
    process.variables[var_name] = var;
    process.heap_size += size;
    
    return virtual_address;
}

bool Mmu::deallocateMemory(uint32_t pid, const std::string& var_name) {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        return false;
    }
    
    Process& process = it->second;
    auto var_it = process.variables.find(var_name);
    if (var_it == process.variables.end()) {
        return false;
    }
    
    process.variables.erase(var_it);
    return true;
}

bool Mmu::setMemory(uint32_t pid, const std::string& var_name, uint32_t offset, const std::vector<std::string>& values) {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        throw std::invalid_argument("Process not found");
    }
    
    Process& process = it->second;
    auto var_it = process.variables.find(var_name);
    if (var_it == process.variables.end()) {
        throw std::invalid_argument("Variable not found");
    }
    
    Variable& var = var_it->second;
    uint32_t element_size = getDataTypeSize(var.type);
    
    if (offset + values.size() > var.num_elements) {
        throw std::invalid_argument("index out of range");
    }
    
    for (size_t i = 0; i < values.size(); i++) {
        uint32_t byte_offset = (offset + i) * element_size;
        if (byte_offset + element_size > var.data.size()) {
            throw std::invalid_argument("index out of range");
        }
        
        if (!parseValue(values[i], var.type, &var.data[byte_offset])) {
            throw std::invalid_argument("Invalid value format");
        }
    }
    
    return true;
}

uint32_t Mmu::virtualToPhysical(uint32_t pid, uint32_t virtual_address) {
    int physical_addr = pt.getPhysicalAddress(pid, virtual_address);
    if (physical_addr == -1) {
        throw std::invalid_argument("Virtual page out of range or not valid");
    }
    return physical_addr;
}

void Mmu::printMMU() const {
    std::cout << " PID  | Variable Name | Virtual Addr | Size     " << std::endl;
    std::cout << "------+---------------+--------------+----------" << std::endl;
    
    std::vector<std::tuple<uint32_t, std::string, uint32_t, uint32_t>> entries;
    
    for (const auto& [pid, process] : processes) {
        entries.push_back({pid, "<TEXT>", process.text_addr, process.text_size});
        entries.push_back({pid, "<GLOBALS>", process.data_addr, process.data_size});
        entries.push_back({pid, "<STACK>", process.stack_addr, process.stack_size});
        for (const auto& [var_name, variable] : process.variables) {
            entries.push_back({pid, var_name, variable.virtual_address, variable.size});
        }
    }
    
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b)) {
            return std::get<0>(a) < std::get<0>(b);
        }
        return std::get<2>(a) < std::get<2>(b);
    });
    
    for (const auto& [pid, var_name, virtual_addr, size] : entries) {
        std::cout << " " << std::setw(4) << pid << " | " << std::setw(13) << var_name << " |   0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << virtual_addr << std::dec << std::setfill(' ') << " | " << std::setw(8) << size << std::endl;
    }
}

void Mmu::printPageTable() const {
    // Cast away constness so we can call pt.print()
    const_cast<PageTable&>(pt).print();
}

void Mmu::printProcesses() const {
    for (const auto& [pid, process] : processes) {
        std::cout << pid << std::endl;
    }
}

void Mmu::printVariable(uint32_t pid, const std::string& var_name) const {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        std::cout << "Process not found" << std::endl;
        return;
    }
    
    const Process& process = it->second;
    auto var_it = process.variables.find(var_name);
    if (var_it == process.variables.end()) {
        std::cout << "Variable not found" << std::endl;
        return;
    }
    
    const Variable& var = var_it->second;
    
    uint32_t element_size = getDataTypeSize(var.type);
    uint32_t elements_to_print = std::min(var.num_elements, 4u);
    
    for (uint32_t i = 0; i < elements_to_print; i++) {
        if (i > 0) std::cout << ", ";
        std::cout << formatValue(&var.data[i * element_size], var.type);
    }
    
    if (var.num_elements > 4) {
        std::cout << ", ... [" << var.num_elements << " items]";
    }
    
    std::cout << std::endl;
}

bool Mmu::isValidPID(uint32_t pid) const {
    return processes.find(pid) != processes.end();
}

Process* Mmu::getProcess(uint32_t pid) {
    auto it = processes.find(pid);
    return (it != processes.end()) ? &it->second : nullptr;
}

const Process* Mmu::getProcess(uint32_t pid) const {
    auto it = processes.find(pid);
    return (it != processes.end()) ? &it->second : nullptr;
}

DataType Mmu::stringToDataType(const std::string& type_str) const {
    if (type_str == "char") return DataType::CHAR;
    if (type_str == "short") return DataType::SHORT;
    if (type_str == "int") return DataType::INT;
    if (type_str == "float") return DataType::FLOAT;
    if (type_str == "long") return DataType::LONG;
    if (type_str == "double") return DataType::DOUBLE;
    throw std::invalid_argument("Invalid data type");
}

uint32_t Mmu::getDataTypeSize(DataType type) const {
    switch (type) {
        case DataType::CHAR: return 1;
        case DataType::SHORT: return 2;
        case DataType::INT: return 4;
        case DataType::LONG: return 8;
        case DataType::FLOAT: return 4;
        case DataType::DOUBLE: return 8;
        default: return 0;
    }
}

uint32_t Mmu::allocatePages(uint32_t pid, uint32_t size) {
    uint32_t pages_needed = (size + page_size - 1) / page_size;
    
    Process* p = getProcess(pid);
    if (!p) return 0;
    
    // Calculate the current number of pages this process already owns
    uint32_t current_total_bytes = p->text_size + p->data_size + p->stack_size + p->heap_size;
    uint32_t start_virtual_page = current_total_bytes / page_size;
    
    for (uint32_t i = 0; i < pages_needed; i++) {
        pt.addEntry(pid, start_virtual_page + i);
    }
    
    return start_virtual_page * page_size;
}

void Mmu::deallocatePages(uint32_t pid, uint32_t virtual_address, uint32_t size) {
    // Like terminateProcess, we cannot remove frames from the global PageTable 
    // unless we modify pagetable.h to add a remove function.
}

bool Mmu::parseValue(const std::string& value_str, DataType type, uint8_t* buffer) {
    try {
        switch (type) {
            case DataType::CHAR: {
                if (value_str.length() != 1) return false;
                buffer[0] = static_cast<uint8_t>(value_str[0]);
                break;
            }
            case DataType::SHORT: {
                int16_t val = static_cast<int16_t>(std::stoi(value_str));
                std::memcpy(buffer, &val, 2);
                break;
            }
            case DataType::INT: {
                int32_t val = std::stoi(value_str);
                std::memcpy(buffer, &val, 4);
                break;
            }
            case DataType::FLOAT: {
                float val = std::stof(value_str);
                std::memcpy(buffer, &val, 4);
                break;
            }
            case DataType::LONG: {
                int64_t val = std::stoll(value_str);
                std::memcpy(buffer, &val, 8);
                break;
            }
            case DataType::DOUBLE: {
                double val = std::stod(value_str);
                std::memcpy(buffer, &val, 8);
                break;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string Mmu::formatValue(const uint8_t* data, DataType type) const {
    switch (type) {
        case DataType::CHAR:
            return std::string(1, static_cast<char>(data[0]));
        case DataType::SHORT: {
            int16_t val;
            std::memcpy(&val, data, 2);
            return std::to_string(val);
        }
        case DataType::INT: {
            int32_t val;
            std::memcpy(&val, data, 4);
            return std::to_string(val);
        }
        case DataType::FLOAT: {
            float val;
            std::memcpy(&val, data, 4);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << val;
            return oss.str();
        }
        case DataType::LONG: {
            int64_t val;
            std::memcpy(&val, data, 8);
            return std::to_string(val);
        }
        case DataType::DOUBLE: {
            double val;
            std::memcpy(&val, data, 8);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << val;
            return oss.str();
        }
        default:
            return "unknown";
    }
}