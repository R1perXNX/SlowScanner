#include "memory_region.hpp"
#include "../slow_scanner.h"

int memory_region::read_memory()
{
	if (!_mem_reserved) {
		if (alloc_f(size()) == -1)
			return 0;
		else
			_mem_reserved = true;

	}
	SIZE_T bytes_read = 0;
	BOOL success = ReadProcessMemory(
		LongToHandle(_process_handle),
		base(),
		_data[0].data(),
		size(),
		&bytes_read
	);

	return success;
}
