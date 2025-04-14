#pragma once

#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <optional>
#include <array>
#include "memory_region.hpp"

enum scan_type {
    unknown_value,
    increased_value,
    decreased_value,
    exact_value,
    increased_by,
    decreased_by,
    smaller_than,
    bigger_than,
    changed,
    unchanged,
    value_between
};


template<typename data_type>
struct scan_entry {
    data_type value;
    size_t element_index;
};


//Tips: you can make this class dumpable to save memory space (I don't need that for now)

template<typename data_type>
class scan {
    std::vector<scan_entry<data_type>> _results;
    std::shared_ptr<memory_region> _associated_region;

    scan_type _type;
    bool _valid_scan{ false };

public:
    scan(const std::shared_ptr<memory_region>& region, scan_type type) : _associated_region(region), _type(type) {}

    scan(const scan& other) = delete;
    scan& operator=(const scan& other) = delete;

    // Move constructor
    scan(scan&& other) noexcept
        : _results(std::move(other._results)),
        _associated_region(std::move(other._associated_region)),
        _type(other._type) // assuming scan_type is trivially copyable
    {
        // Optionally reset other's _type if needed (e.g., other._type = scan_type::none;)
    }

    // Move assignment operator
    scan& operator=(scan&& other) noexcept
    {
        if (this != &other)
        {
            _results = std::move(other._results);
            _associated_region = std::move(other._associated_region);
            _type = other._type;
        }
        return *this;
    }

    std::shared_ptr<memory_region> region() { return _associated_region; }

    inline scan_type type() { return _type; }

    inline std::span<scan_entry<data_type>> elements() {
        return this->_results;
    
    }

    uint64_t search_value(std::function<bool(data_type, data_type, std::optional<data_type>)> comparator, const data_type& value1, std::optional<data_type> value2);

    void add_element(const scan_entry<data_type>& elem);
    void set_valid() { _valid_scan = true; }
    bool is_valid_result() { return _valid_scan; }
};

template<typename data_type>
inline uint64_t scan<data_type>::search_value(std::function<bool(data_type, data_type, std::optional<data_type>)> comparator, const data_type& value1, std::optional<data_type> value2)
{
    auto total_elements{ 0 };
    if (!_associated_region)
        return total_elements;

    auto elements = _associated_region->elements<data_type>();

    size_t index = 0;

    for (const auto& elem : elements) {
        if (comparator(elem, value1, value2)) {
            _results.push_back({ elem, index });
            ++total_elements;
        }
        ++index;
    }

    return total_elements;
}

template<typename data_type>
inline void scan<data_type>::add_element(const scan_entry<data_type>& elem)
{
    _results.push_back(elem);
}

