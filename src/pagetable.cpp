#include <algorithm>
#include "pagetable.h"
#include <iomanip>

PageTable::PageTable(int page_size)
{
    _page_size = page_size;
}

PageTable::~PageTable()
{
}

std::vector<std::string> PageTable::sortedKeys()
{
    std::vector<std::string> keys;

    std::map<std::string, int>::iterator it;
    for (it = _table.begin(); it != _table.end(); it++)
    {
        keys.push_back(it->first);
    }

    std::sort(keys.begin(), keys.end(), PageTableKeyComparator());

    return keys;
}

void PageTable::addEntry(uint32_t pid, int page_number)
{
    // Combination of pid and page number act as the key to look up frame number
    std::string entry = std::to_string(pid) + "|" + std::to_string(page_number);

    int frame = 0; 
    // Calculate the total number of frames in the 64 MB system
    int total_frames = 67108864 / _page_size;

    // Create a temporary checklist to see which frames are taken
    std::vector<bool> used_frames(total_frames, false);
    
    // Loop through the map and mark the taken frames as 'true'
    for (const auto& pair : _table) {
        int taken_frame = pair.second;
        if (taken_frame < total_frames) {
            used_frames[taken_frame] = true;
        }
    }

    int frame = -1; 
    
    // Find the first free frame (First-Fit)
    for (int i = 0; i < total_frames; i++) {
        if (!used_frames[i]) {
            frame = i;
            break;
        }
    }

    // Assign the frame if we found one
    if (frame != -1) {
        _table[entry] = frame;
    } else {
        std::cout << "error: out of physical memory" << std::endl;
    }
}

int PageTable::getPhysicalAddress(uint32_t pid, uint32_t virtual_address)
{
    // Convert virtual address to page_number and page_offset
    int page_number = virtual_address / _page_size;
    int page_offset = virtual_address % _page_size;

    // Combination of pid and page number act as the key to look up frame number
    std::string entry = std::to_string(pid) + "|" + std::to_string(page_number);
    
    // If entry exists, look up frame number and convert virtual to physical address
    int address = -1;
    if (_table.count(entry) > 0)
    {
        int frame_number = _table[entry];
        address = (frame_number * _page_size) + page_offset;
    }

    return address;
}

void PageTable::print()
{
    int i;

    std::cout << " PID  | Page Number | Frame Number" << std::endl;
    std::cout << "------+-------------+--------------" << std::endl;

    std::vector<std::string> keys = sortedKeys();

    for (i = 0; i < keys.size(); i++)
    {
        std::string key = keys[i];
        
        // Split the string at the '|' character
        size_t sep_pos = key.find("|");
        std::string pid_str = key.substr(0, sep_pos);
        std::string page_str = key.substr(sep_pos + 1);
        
        // Get the frame number from the table
        int frame = _table[key];

        // Print with specific widths to match the header alignment perfectly
        std::cout << " " << std::setw(4) << pid_str 
                  << " | " << std::setw(11) << page_str 
                  << " | " << std::setw(12) << frame << std::endl;
    }
}
