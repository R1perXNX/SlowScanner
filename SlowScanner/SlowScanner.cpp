// main.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <cassert>
#include <memory>
#include <queue>
#include <set>
#include <future>
#include "scanner/scan.hpp"
#include "thread_pool.hpp"

class engine {

private:
    long _pid{ -1 };

    std::shared_ptr<m_file> _file;
private:
    template <typename data_type>
    std::function<bool(data_type, data_type, std::optional<data_type>)> compare(scan_type type);

    std::queue<std::shared_ptr<memory_region>> get_regions(std::pair<void*, void*> range, DWORD protect);


public:
    engine(const std::shared_ptr<m_file>& file) :_file(file) {
    };
    ~engine() = default;
    inline void set_pid(long pid) {_pid = pid; }

    template<typename data_type = int>
    std::vector<std::shared_ptr<scan<data_type>>> first_scan(const std::pair<void*, void*>& range, DWORD protect, scan_type type, std::atomic<size_t>& total_entries, const data_type& value1, std::optional<data_type> value2);

    template<typename data_type = int>
    std::vector<std::shared_ptr<scan<data_type>>> next_scan(const std::pair<void*, void*>& range, DWORD protect, scan_type type, std::atomic<size_t>& total_entries, std::vector<std::shared_ptr<scan<data_type>>> prev_scans,
        const data_type& value1, std::optional<data_type> value2);
};


std::unique_ptr<thread_pool> pool;

int main() {

    engine scan_engine(std::make_shared<m_file>("dump.bin"));

    pool = std::make_unique<thread_pool>(8);

    long pid{ -1 };

    std::cout << "Proccess ID: " << std::endl;
    std::cin >> pid;
    

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    auto module_start = reinterpret_cast<LPVOID>(sysInfo.lpMinimumApplicationAddress);
    auto module_end = reinterpret_cast<LPVOID>(sysInfo.lpMaximumApplicationAddress);

    HANDLE h_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h_process) {
        std::cerr << "Failed to open process: " << GetLastError() << "\n";
        return 0;
    }

    scan_engine.set_pid(HandleToLong(h_process));

    std::atomic<size_t> total = 0;

    auto results = scan_engine.first_scan<int>({ module_start, module_end }, PAGE_READWRITE | PAGE_WRITECOPY, scan_type::exact_value, total, 100, std::nullopt);


    std::cout << "Found : " << total.load() << std::endl;

    /*for (const auto& result : results) {
        auto elements = result->elements();

        for (const auto& elem : elements) {
            std::cout << std::hex << "0x" << reinterpret_cast<uint64_t>(result->region()->base()) + (elem.element_index * sizeof(int)) << "     " << std::dec << ": " << elem.value << std::endl;
        }
        
    }*/

    /*auto results2 = scan_engine.next_scan<int>({ module_start, module_end }, PAGE_READWRITE | PAGE_WRITECOPY, scan_type::increased_by, total, results, 200, std::nullopt);
    
    for (const auto& result : results2) {
        auto elements = result->elements();

        for (const auto& elem : elements) {
            std::cout << std::hex << "0x" << reinterpret_cast<uint64_t>(result->region()->base()) + (elem.element_index * sizeof(int)) << "     " << std::dec << ": " << elem.value << std::endl;
        }

    }*/

}

std::queue<std::shared_ptr<memory_region>> engine::get_regions(std::pair<void*, void*> range, DWORD protect)
{
    std::queue<std::shared_ptr<memory_region>> regions;

    auto current_address = range.first;

    HANDLE h_process = LongToHandle(_pid);

    while (current_address < range.second) {
        MEMORY_BASIC_INFORMATION mbi;

        if (VirtualQueryEx(h_process, current_address, &mbi, sizeof(mbi)) == 0) {
            break;
        }

        if (reinterpret_cast<BYTE*>(mbi.BaseAddress) < range.first)
            mbi.BaseAddress = range.first;

        if (reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize > range.second)
            mbi.RegionSize = reinterpret_cast<uint64_t>(range.second) - reinterpret_cast<uint64_t>(mbi.BaseAddress);

        auto current_region = std::make_shared<memory_region>(_file, mbi);


        if (current_region && current_region->has_protection_flags(protect) && current_region->is_commited() && !current_region->is_memmapped())
            regions.push(current_region);

        current_address = reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
    }

    return regions;
}

template<typename data_type>
std::function<bool(data_type, data_type, std::optional<data_type>)> engine::compare(scan_type type)
{
    std::function<bool(data_type, data_type, std::optional<data_type>)> cmp = nullptr;

    switch (type) {
    case scan_type::exact_value: {
        cmp = [](data_type a, data_type b, std::optional<data_type> /*unused*/) {
            return a == b;
            };
        break;
    }
    case scan_type::bigger_than: {
        if constexpr (std::is_same_v<data_type, float>) {
            cmp = [](float a, float b, std::optional<data_type> /*unused*/) {
                return a > b + 0.0001f;
                };
        }
        else if constexpr (std::is_same_v<data_type, double>) {
            cmp = [](double a, double b, std::optional<data_type> /*unused*/) {
                return a > b + 0.0000001;
                };
        }
        else {
            cmp = [](data_type a, data_type b, std::optional<data_type> /*unused*/) {
                return a > b;
                };
        }
        break;
    }
    case scan_type::smaller_than: {
        if constexpr (std::is_same_v<data_type, float>) {
            cmp = [](float a, float b, std::optional<data_type> /*unused*/) {
                return a < b - 0.0001f;
                };
        }
        else if constexpr (std::is_same_v<data_type, double>) {
            cmp = [](double a, double b, std::optional<data_type> /*unused*/) {
                return a < b - 0.0000001;
                };
        }
        else {
            cmp = [](data_type a, data_type b, std::optional<data_type> /*unused*/) {
                return a < b;
                };
        }
        break;
    }
    case scan_type::changed: {
        cmp = [](data_type a, data_type b, std::optional<data_type> /*unused*/) {
            return a != b;
            };
        break;
    }
    case scan_type::unchanged: {
        cmp = [](data_type a, data_type b, std::optional<data_type> /*unused*/) {
            return a == b;
            };
        break;
    }
    case scan_type::increased_by: {
        cmp = [](data_type a, data_type b, std::optional<data_type> c) {
            if (!c.has_value())
                return false;
            return a - b == *c;
            };
        break;
    }
    case scan_type::decreased_by: {
        cmp = [](data_type a, data_type b, std::optional<data_type> c) {
            if (!c.has_value())
                return false;
            return b - a == *c;
            };
        break;
    }
    case scan_type::value_between: {
        cmp = [](data_type a, data_type b, std::optional<data_type> c) {
            if (!c.has_value())
                return false;
            return a > b && a < *c;
            };
        break;
    }
    case scan_type::increased_value: {
        cmp = [](data_type a, data_type b, std::optional<data_type> /*unused*/) {
            return a > b;
            };
        break;
    }
    case scan_type::decreased_value: {
        cmp = [](data_type a, data_type b, std::optional<data_type> /*unused*/) {
            return a < b;
            };
        break;
    }

    default: {
        break;
    }
    }

    return cmp;
}

template<typename data_type>
std::vector<std::shared_ptr<scan<data_type>>> engine::first_scan(const std::pair<void*, void*>& range, DWORD protect, scan_type type, std::atomic<size_t>& total_entries, const data_type& value1, std::optional<data_type> value2)
{
    auto regions = get_regions(range, protect);

    std::vector<std::future<std::shared_ptr<scan<data_type>>>> futures;

    std::vector<std::shared_ptr<scan<data_type>>> tmp_results;

    // While there are regions to process, launch an async task for each.
    while (!regions.empty()) {
        auto current_region = regions.front();
        regions.pop();

        // Create a scan object for the current region.
        auto result = std::make_shared<scan<data_type>>(current_region, type);

        if (pool) {
            // Launch an asynchronous task for processing the region.
            auto future_scan = pool->enqueue([this, result, type, value1, value2, &total_entries]() -> std::shared_ptr<scan<data_type>> {
                    auto region = result->region();
                    if (!region)
                        return result;

                    size_t bytes_read = 0;
                    region->read_memory(_pid, bytes_read);

                    if (!bytes_read)
                        return result;

                    // For unknown value scans, mark as valid right away.
                    if (type == scan_type::unknown_value) {
                        result->set_valid();
                        return result;
                    }

                    // Use the comparison logic (this->compare) to determine if the scan finds a match.
                    auto cmp = this->compare<data_type>(type);
                    if (cmp) {
                        auto success = result->search_value(cmp, value1, value2);
                        if (success) {
                            result->set_valid();
                            total_entries += success;
                        }
                    }
                    return result;
                }
            );

            futures.push_back(std::move(future_scan));
        }
        
    }

    for (auto& future : futures) {
        tmp_results.push_back(future.get());
    }


    //Sort to optimize the next search
    std::vector<std::shared_ptr<scan<data_type>>> results;

    for (const auto& result : tmp_results) {
        if (result->is_valid_result()) {
            results.push_back(result);
        }
    }

    std::sort(results.begin(), results.end(),
        [](const std::shared_ptr<scan<data_type>>& a, const std::shared_ptr<scan<data_type>>& b) {
            return a->region()->base() < b->region()->base(); // Compare the addresses
        }
    );

    return results;
}

template<typename data_type>
std::vector<std::shared_ptr<scan<data_type>>> engine::next_scan(const std::pair<void*, void*>& range, DWORD protect, scan_type type, std::atomic<size_t>& total_entries, std::vector<std::shared_ptr<scan<data_type>>> prev_scans, const data_type& value1, std::optional<data_type> value2)
{
    auto regions = get_regions(range, protect);

    std::vector<std::shared_ptr<scan<data_type>>> tmp_results;


    int i = 0;
    while (i < prev_scans.size() && !regions.empty()) {
        auto current_region = regions.front();
        const auto& prev_scan = prev_scans[i];

        regions.pop();

        // Advance prev_scan until we find a region that could overlap with current_region
        if (reinterpret_cast<uint64_t>(prev_scan->region()->base()) + prev_scan->region()->size() < reinterpret_cast<uint64_t>(current_region->base())) {
            i++;
            continue;
        }

        // Check if the remaining region in prev_scan overlaps with current_region
        if (reinterpret_cast<uint64_t>(prev_scan->region()->base()) < reinterpret_cast<uint64_t>(current_region->base()) + current_region->size()) {

            auto result = std::make_shared<scan<data_type>>(current_region, type);
            tmp_results.push_back(result);

            {
                size_t bytes_read = 0;

                auto success = current_region->read_memory(_pid, bytes_read);

                if (!success)
                    continue;

                auto cmp = this->compare<data_type>(type);
                

                
                
                if (prev_scan->type() == scan_type::unknown_value &&
                    type  != scan_type::exact_value && 
                    type != scan_type::value_between &&
                    type != scan_type::bigger_than &&
                    type != scan_type::smaller_than) {
                    
                    auto old_elements = prev_scan->region()->elements<data_type>();
                    auto new_elements = current_region->elements<data_type>();

                    for (size_t i = 0; i < old_elements.size(); i++) {
                        if (new_elements.size() <= i)
                            break;

                        switch (type) {
                        case scan_type::increased_value:
                        case scan_type::decreased_value:
                        case scan_type::changed:
                        case scan_type::unchanged:
                        case scan_type::decreased_by:
                        case scan_type::increased_by: {
                            if (cmp(new_elements[i], old_elements[i], value1)) {
                                result->add_element({ new_elements[i], i });
                                result->set_valid();
                            }
                            break;
                        default:
                            break;
                        }
                        }
                        
                    }
                    
                }
                else {
                    switch (type) {
                    case scan_type::increased_value:
                    case scan_type::decreased_value:
                    case scan_type::changed:
                    case scan_type::unchanged:
                    case scan_type::decreased_by:
                    case scan_type::increased_by: {
                        auto old_elements = prev_scan->elements();
                        auto new_elements = current_region->elements<data_type>();

                        for (const auto& old_elem : old_elements) {
                            if (old_elem.element_index >= new_elements.size())
                                continue;

                            if (cmp(new_elements[old_elem.element_index], old_elem.value, value1)) {
                                result->add_element({ new_elements[old_elem.element_index] ,old_elem.element_index });
                                result->set_valid();
                            }
                        }
                        break;
                    }
                    case scan_type::exact_value:
                    case scan_type::value_between: {
                    case scan_type::smaller_than:
                    case scan_type::bigger_than: {
                        auto old_elements = prev_scan->elements();
                        auto new_elements = current_region->elements<data_type>();

                        for (const auto& old_elem : old_elements) {
                            if (old_elem.element_index >= new_elements.size())
                                continue;

                            if (cmp(new_elements[old_elem.element_index], value1, value2)) {
                                result->add_element({ new_elements[old_elem.element_index] ,old_elem.element_index });
                                result->set_valid();
                            }
                        }
                        break;
                    }
                    default:
                        break;
                    }
                    }

                }
            }
        }
    }

    std::vector<std::shared_ptr<scan<data_type>>> results;

    for (const auto& result : tmp_results) {
        if (result->is_valid_result()) {
            results.push_back(result);
        }
    }

    std::sort(results.begin(), results.end(),
        [](const std::shared_ptr<scan<data_type>>& a, const std::shared_ptr<scan<data_type>>& b) {
            return a->region()->base() < b->region()->base(); // Compare the addresses
        }
    );

    return results;
}
