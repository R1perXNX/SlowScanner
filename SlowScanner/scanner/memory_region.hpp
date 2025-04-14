#pragma once
#include "../dumpable/dumpable.hpp"


class memory_region : public dumpable<uint8_t> {
private:
    bool _has_data{ false };
    MEMORY_BASIC_INFORMATION _mbi;
  

public:
    memory_region(const std::shared_ptr<m_file>& file, const MEMORY_BASIC_INFORMATION& mbi): dumpable<uint8_t>(file), _mbi(mbi) {}
    ~memory_region() override = default;

    inline void* base() { return _mbi.BaseAddress; }
    inline size_t size() { return _mbi.RegionSize; }

    inline bool has_protection_flags(DWORD protect_flags) { return (_mbi.Protect & protect_flags) != 0;}
    inline bool is_commited() { return _mbi.State == MEM_COMMIT;}
    inline bool is_memmapped() { return _mbi.Type == MEM_MAPPED; }

    //Supposed to be called once per object, does not handle multiple file chunks
    int read_memory(long pid, size_t& bytes_read);

    template <typename data_type>
    std::span<data_type> elements();

};

template<typename data_type>
std::span<data_type> memory_region::elements()
{
    if (!_has_data)
        return std::span<data_type>();

    return std::span<data_type>(reinterpret_cast<data_type*>(_data[0].data()), _data[0].size() / sizeof(data_type));
}