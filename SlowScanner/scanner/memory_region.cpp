#include "memory_region.hpp"

int memory_region::read_memory(long pid, size_t& bytes_read)
{
    if (alloc_f(size()) == -1)
        return 0;

    BOOL ok = ReadProcessMemory(
        LongToHandle(pid),
        base(),
        _data[0].data(),
        size(),
        &bytes_read
    );

    if (ok) {
        _has_data = true;
        return 1;
    }
    return 0;
}
