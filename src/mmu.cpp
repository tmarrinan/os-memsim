#include "mmu.h"
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <tuple>

MMU::MMU(uint32_t pg_size) : page_size(pg_size), next_pid(PID_START) {
    // Validate page size is power of 2 between 1024 and 32768
    if (pg_size < 1024 || pg_size > 32768 || (pg_size & (pg_size - 1)) != 0) {
        throw std::invalid_argument("Page size must be power of 2 between 1024 and 32768");
    }
    
    total_pages = TOTAL_MEMORY / page_size;
    frame_allocation.resize(total_pages, false);
    
    // Initialize free frames list
    for (uint32_t i = 0; i < total_pages; i++) {
        free_frames.push_back({i, page_size});
    }
}

uint32_t MMU::createProcess(uint32_t text_size, uint32_t data_size) {
    // Validate input sizes
    if (text_size < 2048 || text_size > 16384) {
        throw std::invalid_argument("Text size must be between 2048 and 16384 bytes");
    }
    if (data_size > 1024) {
        throw std::invalid_argument("Data size must be between 0 and 1024 bytes");
    }
    
    uint32_t pid = next_pid++;
    Process process(pid, text_size, data_size);
    
    // Calculate memory layout
    uint32_t total_process_memory = text_size + data_size + STACK_SIZE;
    uint32_t pages_needed = (total_process_memory + page_size - 1) / page_size;
    
    // Allocate pages for the process
    uint32_t base_virtual = 0;
    process.text_addr = base_virtual;
    process.data_addr = process.text_addr + text_size;
    process.stack_addr = process.data_addr + data_size;
    process.heap_start = process.stack_addr + STACK_SIZE;
    
    try {
        allocatePages(pid, total_process_memory);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Not enough memory to create process");
    }
    
    processes[pid] = process;
    return pid;
}

bool MMU::terminateProcess(uint32_t pid) {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        return false;
    }
    
    // Deallocate all pages for this process
    auto page_table_it = page_tables.find(pid);
    if (page_table_it != page_tables.end()) {
        for (const auto& entry : page_table_it->second) {
            if (entry.valid) {
                deallocateFrame(entry.frame_number);
            }
        }
        page_tables.erase(page_table_it);
    }
    
    processes.erase(it);
    return true;
}

uint32_t MMU::allocateMemory(uint32_t pid, const std::string& var_name, DataType type, uint32_t num_elements) {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        throw std::invalid_argument("Process not found");
    }
    
    Process& process = it->second;
    
    // Check if variable already exists
    if (process.variables.find(var_name) != process.variables.end()) {
        throw std::invalid_argument("Variable already exists");
    }
    
    uint32_t size = getDataTypeSize(type) * num_elements;
    
    // Find the next available virtual address in the heap
    uint32_t virtual_address = process.heap_start;
    
    // Find the highest allocated address and place new variable after it
    for (const auto& [name, var] : process.variables) {
        uint32_t var_end = var.virtual_address + var.size;
        if (var_end > virtual_address) {
            virtual_address = var_end;
        }
    }
    
    // Ensure the address is properly aligned (4-byte alignment)
    virtual_address = (virtual_address + 3) & ~3;
    
    // Create variable
    Variable var(var_name, type, num_elements, virtual_address);
    process.variables[var_name] = var;
    process.heap_size += size;
    
    return virtual_address;
}

bool MMU::deallocateMemory(uint32_t pid, const std::string& var_name) {
    auto it = processes.find(pid);
    if (it == processes.end()) {
        return false;
    }
    
    Process& process = it->second;
    auto var_it = process.variables.find(var_name);
    if (var_it == process.variables.end()) {
        return false;
    }
    
    process.heap_size -= var_it->second.size;
    process.variables.erase(var_it);
    
    // Note: We don't deallocate pages immediately as they might contain other variables
    // In a real system, this would be more sophisticated
    
    return true;
}

bool MMU::setMemory(uint32_t pid, const std::string& var_name, uint32_t offset, const std::vector<std::string>& values) {
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
    
    // Set values
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

uint32_t MMU::allocateFrame() {
    for (uint32_t i = 0; i < frame_allocation.size(); i++) {
        if (!frame_allocation[i]) {
            frame_allocation[i] = true;
            return i;
        }
    }
    throw std::runtime_error("No free frames available");
}

void MMU::deallocateFrame(uint32_t frame_number) {
    if (frame_number < frame_allocation.size()) {
        frame_allocation[frame_number] = false;
    }
}

uint32_t MMU::virtualToPhysical(uint32_t pid, uint32_t virtual_address) {
    auto page_table_it = page_tables.find(pid);
    if (page_table_it == page_tables.end()) {
        throw std::invalid_argument("Process page table not found");
    }
    
    uint32_t virtual_page = virtual_address / page_size;
    uint32_t page_offset = virtual_address % page_size;
    
    if (virtual_page >= page_table_it->second.size()) {
        throw std::invalid_argument("Virtual page out of range");
    }
    
    const auto& entry = page_table_it->second[virtual_page];
    if (!entry.valid) {
        throw std::invalid_argument("Page not valid");
    }
    
    return entry.frame_number * page_size + page_offset;
}

void MMU::printMMU() const {
    std::cout << " PID  | Variable Name | Virtual Addr | Size     " << std::endl;
    std::cout << "------+---------------+--------------+----------" << std::endl;
    
    // Collect all entries to sort by PID and then by virtual address
    std::vector<std::tuple<uint32_t, std::string, uint32_t, uint32_t>> entries;
    
    for (const auto& [pid, process] : processes) {
        // Add TEXT segment
        entries.push_back({pid, "<TEXT>", process.text_addr, process.text_size});
        
        // Add DATA segment  
        entries.push_back({pid, "<GLOBALS>", process.data_addr, process.data_size});
        
        // Add STACK segment
        entries.push_back({pid, "<STACK>", process.stack_addr, process.stack_size});
        
        // Add variables
        for (const auto& [var_name, variable] : process.variables) {
            entries.push_back({pid, var_name, variable.virtual_address, variable.size});
        }
    }
    
    // Sort by PID, then by virtual address
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b)) {
            return std::get<0>(a) < std::get<0>(b);
        }
        return std::get<2>(a) < std::get<2>(b);
    });
    
    // Print sorted entries
    for (const auto& [pid, var_name, virtual_addr, size] : entries) {
        std::cout << " " << std::setw(4) << pid << " | " << std::setw(13) << var_name << " |   " << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << virtual_addr << std::dec << std::setfill(' ') << " | " << std::setw(8) << size << std::endl;
    }
}

void MMU::printPageTable() const {
    std::cout << " PID  | Page Number | Frame Number" << std::endl;
    std::cout << "------+-------------+--------------" << std::endl;
    
    for (const auto& [pid, page_table] : page_tables) {
        for (size_t i = 0; i < page_table.size(); i++) {
            if (page_table[i].valid) {
                std::cout << " " << std::setw(4) << pid << " | " << std::setw(11) << i << " | " << std::setw(12) << page_table[i].frame_number << std::endl;
            }
        }
    }
}

void MMU::printProcesses() const {
    for (const auto& [pid, process] : processes) {
        std::cout << pid << std::endl;
    }
}

void MMU::printVariable(uint32_t pid, const std::string& var_name) const {
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

bool MMU::isValidPID(uint32_t pid) const {
    return processes.find(pid) != processes.end();
}

Process* MMU::getProcess(uint32_t pid) {
    auto it = processes.find(pid);
    return (it != processes.end()) ? &it->second : nullptr;
}

const Process* MMU::getProcess(uint32_t pid) const {
    auto it = processes.find(pid);
    return (it != processes.end()) ? &it->second : nullptr;
}

DataType MMU::stringToDataType(const std::string& type_str) const {
    if (type_str == "char") return DataType::CHAR;
    if (type_str == "short") return DataType::SHORT;
    if (type_str == "int") return DataType::INT;
    if (type_str == "float") return DataType::FLOAT;
    if (type_str == "long") return DataType::LONG;
    if (type_str == "double") return DataType::DOUBLE;
    throw std::invalid_argument("Invalid data type");
}

uint32_t MMU::getDataTypeSize(DataType type) const {
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

uint32_t MMU::allocatePages(uint32_t pid, uint32_t size) {
    uint32_t pages_needed = (size + page_size - 1) / page_size;
    
    // Ensure page table exists
    if (page_tables.find(pid) == page_tables.end()) {
        page_tables[pid] = std::vector<PageTableEntry>();
    }
    
    auto& page_table = page_tables[pid];
    uint32_t start_virtual_page = page_table.size();
    
    // Allocate frames and update page table
    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t frame = allocateFrame();
        PageTableEntry entry;
        entry.frame_number = frame;
        entry.valid = true;
        entry.allocated = true;
        page_table.push_back(entry);
    }
    
    return start_virtual_page * page_size;
}

void MMU::deallocatePages(uint32_t pid, uint32_t virtual_address, uint32_t size) {
    uint32_t start_page = virtual_address / page_size;
    uint32_t pages_needed = (size + page_size - 1) / page_size;
    
    auto page_table_it = page_tables.find(pid);
    if (page_table_it == page_tables.end()) {
        return;
    }
    
    auto& page_table = page_table_it->second;
    
    for (uint32_t i = 0; i < pages_needed && (start_page + i) < page_table.size(); i++) {
        if (page_table[start_page + i].valid) {
            deallocateFrame(page_table[start_page + i].frame_number);
            page_table[start_page + i].valid = false;
            page_table[start_page + i].allocated = false;
        }
    }
}

uint32_t MMU::findFreeFrame() {
    return allocateFrame();
}

void MMU::addPageTableEntry(uint32_t pid, uint32_t virtual_page, uint32_t frame_number) {
    if (page_tables.find(pid) == page_tables.end()) {
        page_tables[pid] = std::vector<PageTableEntry>();
    }
    
    auto& page_table = page_tables[pid];
    
    // Ensure page table is large enough
    if (virtual_page >= page_table.size()) {
        page_table.resize(virtual_page + 1);
    }
    
    page_table[virtual_page].frame_number = frame_number;
    page_table[virtual_page].valid = true;
    page_table[virtual_page].allocated = true;
}

bool MMU::parseValue(const std::string& value_str, DataType type, uint8_t* buffer) {
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

std::string MMU::formatValue(const uint8_t* data, DataType type) const {
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
