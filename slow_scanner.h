#include <iostream>
#include <vector>
#include <thread>
#include <cassert>
#include <memory>
#include <queue>
#include <set>
#include <future>
#include "thread_pool.hpp"
#include "singleton.hpp"
#include "scanner/scan.hpp"

//SlowScanner: https://github.com/R1perXNX/SlowScanner


enum class element_type {
	U8,    // 1 byte unsigned
	U16,   // 2 byte unsigned
	U32,   // 4 byte unsigned
	U64,   // 8 byte unsigned
	Float, // 4 byte float
	Double // 8 byte double
};


inline size_t element_size(element_type t) {
	switch (t) {
	case element_type::U8:    return 1;
	case element_type::U16:   return 2;
	case element_type::U32:   return 4;
	case element_type::U64:   return 8;
	case element_type::Float: return 4;
	case element_type::Double:return 8;
	default:                  return 1;
	}
}



class slow_scanner : public singleton<slow_scanner> {

private:
	std::shared_ptr<m_file>			_file;
	std::unique_ptr<thread_pool>	_pool;
	long							_process_handle{ -1 };

	static class driver				_driver;

private:
	std::queue<std::shared_ptr<memory_region>> get_regions(std::pair<void*, void*> range, DWORD protect);

	inline scan::comparator_fn make_comparator(scan_type type, element_type et) {
		return [type, et](uint64_t a_bits, uint64_t b_bits, std::optional<uint64_t> c_bits) {
			// helper to read values
			auto read_int = [&](auto cast_to) {
				return static_cast<decltype(cast_to)>(a_bits);
				};
			auto read_fp = [&](auto dst, uint64_t bits) {
				dst = 0;
				std::memcpy(&dst, &bits, sizeof(dst));
				return dst;
				};

			// dispatch by element kind
			switch (et) {
			case element_type::Float: {
				float a{}, b{}, c{};

				a = read_fp(a, a_bits);
				b = read_fp(b, b_bits);
				c = c_bits ? read_fp(c, *c_bits) : 0;

				const auto eps = 0.01f;

				if (type == scan_type::exact_value)   return std::abs(a - b) <= eps;
				if (type == scan_type::increased_value) return a > b + eps;
				if (type == scan_type::decreased_value) return a < b - eps;
				if (type == scan_type::bigger_than)     return a > b + eps;
				if (type == scan_type::smaller_than)    return a < b - eps;
				if (type == scan_type::changed)         return std::abs(a - b) > eps;
				if (type == scan_type::unchanged)       return std::abs(a - b) <= eps;
				if (type == scan_type::increased_by)    return c_bits && (std::abs((a - b) - c) <= eps);
				if (type == scan_type::decreased_by)    return c_bits && (std::abs((b - a) - c) <= eps);
				if (type == scan_type::value_between)   return c_bits && (a > b + eps && a < c - eps);
				return false;
			}
			case element_type::Double: {
				double a{}, b{}, c{};
				a = read_fp(a, a_bits);
				b = read_fp(b, b_bits);
				c = c_bits ? read_fp(c, *c_bits) : 0;
				const auto eps = 1e-7;
				if (type == scan_type::exact_value)   return a == b;
				if (type == scan_type::increased_value) return a > b;
				if (type == scan_type::decreased_value) return a < b;
				if (type == scan_type::bigger_than)     return a > b + eps;
				if (type == scan_type::smaller_than)    return a < b - eps;
				if (type == scan_type::changed)         return a != b;
				if (type == scan_type::unchanged)       return a == b;
				if (type == scan_type::increased_by)    return c_bits && (a - b) == c;
				if (type == scan_type::decreased_by)    return c_bits && (b - a) == c;
				if (type == scan_type::value_between)   return c_bits && (a > b && a < c);
				return false;
			}
			default: {
				uint64_t a = a_bits;
				uint64_t b = b_bits;
				uint64_t c = c_bits.value_or(0);
				if (type == scan_type::exact_value)   return a == b;
				if (type == scan_type::increased_value) return a > b;
				if (type == scan_type::decreased_value) return a < b;
				if (type == scan_type::bigger_than)     return a > b;
				if (type == scan_type::smaller_than)    return a < b;
				if (type == scan_type::changed)         return a != b;
				if (type == scan_type::unchanged)       return a == b;
				if (type == scan_type::increased_by)    return c_bits && (a - b) == c;
				if (type == scan_type::decreased_by)    return c_bits && (b - a) == c;
				if (type == scan_type::value_between)   return c_bits && (a > b && a < c);
				return false;
			}
			}
			};
	}



public:

	slow_scanner() {
		_file = std::make_shared<m_file>("dump.bin");
		_pool = std::make_unique<thread_pool>(8);
	};

	~slow_scanner() = default;

	std::vector<std::shared_ptr<scan>> first_scan(
		const std::pair<void*, void*>& range,
		DWORD protect,
		scan_type type,
		element_type elem_type,
		uint64_t raw1,
		std::optional<uint64_t> raw2 = std::nullopt);

	void next_scan(
		const std::pair<void*, void*>& range,
		DWORD protect,
		scan_type type,
		element_type elem_type,
		std::vector<std::shared_ptr<scan>>& prev_scans,
		uint64_t raw1,
		std::optional<uint64_t> raw2 = std::nullopt);

	__forceinline void attach_to(long process_handle) { _process_handle = process_handle; }
};

