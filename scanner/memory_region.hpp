#pragma once
#include "../dumpable/dumpable.hpp"

class memory_region : public dumpable<uint8_t> {
private:
	MEMORY_BASIC_INFORMATION _mbi;
	long _process_handle{ -1 };
	bool _mem_reserved{ false };

public:

	memory_region(const std::shared_ptr<m_file>& file, MEMORY_BASIC_INFORMATION& mbi, long process_handle) : 
		dumpable<uint8_t>(file), _mbi(mbi), _process_handle(process_handle){}
	~memory_region() override = default;

	inline void* base() { return _mbi.BaseAddress; }
	inline size_t size() { return _mbi.RegionSize; }

	inline bool has_protection_flags(DWORD protect_flags) { return (_mbi.Protect & protect_flags) != 0; }
	inline bool is_commited() { return _mbi.State == MEM_COMMIT; }
	inline bool is_memmapped() { return _mbi.Type == MEM_MAPPED; }

	//Supposed to be called once per object, does not handle multiple file chunks
	int read_memory();

	inline std::span<uint8_t> raw_bytes() const {
		if (_data.empty()) return {};
		return { _data[0].data(), _data[0].size() };
	}

	class strided_span {
	public:
		strided_span(const uint8_t* ptr, size_t total_bytes, size_t stride)
			: _ptr(ptr), _count(total_bytes / stride), _stride(stride) {
		}

		size_t size() const { return _count; }
		const uint8_t* operator[](size_t i) const { return _ptr + i * _stride; }

	private:
		const uint8_t* _ptr;
		size_t         _count;
		size_t         _stride;
	};

	strided_span elements_by_size(size_t elem_size) const {
		auto raw = raw_bytes();
		return { raw.data(), raw.size(), elem_size };
	}

};