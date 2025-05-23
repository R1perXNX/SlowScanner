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


struct scan_entry {
	uint64_t value;
	uint64_t snapshot_value;
	size_t   element_index;
};


//Tips: you can make this class dumpable to save memory space (I don't need that for now)

class scan {
	std::shared_ptr<memory_region> _region;
	scan_type                      _type;
	size_t                         _elem_size;
	bool                           _valid{ false };
	std::vector<scan_entry>        _results;

public:
	using comparator_fn = std::function<bool(uint64_t, uint64_t, std::optional<uint64_t>)>;
public:
	scan(std::shared_ptr<memory_region> region,
		scan_type type,
		size_t elem_size)
		: _region(std::move(region))
		, _type(type)
		, _elem_size(elem_size)
	{
		if (!(elem_size == 1 || elem_size == 2 || elem_size == 4 || elem_size == 8)) {
			throw std::invalid_argument("element_size must be 1,2,4 or 8");
		}
	}

	scan(const scan&) = delete;
	scan& operator=(const scan&) = delete;
	scan(scan&&) noexcept = default;
	scan& operator=(scan&&) noexcept = default;

	inline scan_type type() { return _type; }

	void set_valid() { _valid = true; }

	bool is_valid() const { return _valid; }
	scan_type type() const { return _type; }
	size_t element_size() const { return _elem_size; }
	const std::vector<scan_entry>& results() const { return _results; }
	std::shared_ptr<memory_region> region() const { return _region; }


	__forceinline uint64_t search_value(const comparator_fn& cmp,
		uint64_t ref1_bits,
		std::optional<uint64_t> ref2_bits = std::nullopt)
	{
		_results.clear();
		if (!_region) return 0;

		auto span = _region->elements_by_size(_elem_size);
		uint64_t count = 0;

		for (size_t i = 0; i < span.size(); ++i) {
			uint64_t v = 0;

			std::memcpy(&v, span[i], _elem_size);
			if (cmp(v, ref1_bits, ref2_bits)) {
				_results.push_back({ v, v, i });
				++count;
			}
		}
		_valid = (count > 0);
		return count;
	}

	__forceinline void add_result(const scan_entry& e) {
		_results.push_back(e);
	}

	void update() {
		if (_region && !_results.empty()) {
			_region->read_memory();

			for (auto& e : _results)
				std::memcpy(&e.value, _region->elements_by_size(_elem_size)[e.element_index], _elem_size);

		}
	}
};

