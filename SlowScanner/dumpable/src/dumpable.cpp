#include "../dumpable.hpp"

m_file::m_file(const std::string& name) : _filename(name)
{
    ::DeleteFileA(_filename.c_str());

     _file_handle = CreateFileA(
         _filename.c_str(),
         GENERIC_READ | GENERIC_WRITE,
         FILE_SHARE_READ | FILE_SHARE_WRITE,
         nullptr,
         OPEN_ALWAYS,
         FILE_ATTRIBUTE_NORMAL,
         nullptr
     );

    if (_file_handle == INVALID_HANDLE_VALUE) {
        _valid=false;
        return;
    }

    LARGE_INTEGER file_size;
    if (GetFileSizeEx(_file_handle, &file_size)) {
        _size = static_cast<size_t>(file_size.QuadPart);
        _valid = true;
    }
    else {
        _valid = false;
    }
}

m_file::m_file(const std::string& name, size_t size) : m_file(name)
{
    if (!_valid) {
        return;
    }

    size_t aligned_size = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    LARGE_INTEGER li_size;
    li_size.QuadPart = static_cast<LONGLONG>(aligned_size);

    if (SetFilePointerEx(_file_handle, li_size, nullptr, FILE_BEGIN) &&
        SetEndOfFile(_file_handle)) {
        _size = aligned_size;
    }
    else {
        _valid = false;
    }
}

m_file::~m_file()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _valid = false;

    _current_map.reset();

    // Close the file handle if valid.
    if (_file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(_file_handle);
        _file_handle = INVALID_HANDLE_VALUE;
    }

    // Optionally, delete the file from disk.
    if (!_filename.empty()) {
        DeleteFileA(_filename.c_str());
    }
}

char* m_file::write(char* buffer, size_t size, std::shared_ptr<active_map>& active)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_offset + size > _size) {
        this->expand_size(size);

        auto new_map = do_map(_offset);

        if (new_map->get().data()) {
            _current_map = new_map;
            _map_offset = 0;
        }
        else {
            return nullptr;
        }
    }

    active = _current_map;

    auto dst = active->get().data() + _map_offset;

    if (!dst)
        return nullptr;

    if (buffer)
        memcpy(dst, buffer, size);
    else
        memset(dst, 0, size);

    this->_map_offset += size;
    this->_offset += size;

    return dst;
}

bool m_file::valid()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _valid;
}

std::shared_ptr<active_map> m_file::do_map(size_t offset)
{
    return std::make_shared<active_map>(_filename, offset, _size - offset);
}

bool m_file::expand_size(size_t size)
{
    if (!_valid || _file_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    size_t new_size = (_size + size) * 2;

    size_t aligned_size = ((new_size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    LARGE_INTEGER li_size;
    li_size.QuadPart = static_cast<LONGLONG>(aligned_size);

    if (!SetFilePointerEx(_file_handle, li_size, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(_file_handle)) {
        return false;
    }

    _size = aligned_size;

    return true;
}