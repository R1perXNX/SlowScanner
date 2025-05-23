#pragma once
#include <iostream>
#include <vector>
#include <span>
#include <string>
#include <mutex>
#include <stack>
#include <unordered_set>
#include <memory>
#include "../mio.hpp"

#define PAGE_SIZE 0x1000

//Thanks GPT for the beautiful doc


/**
 * @brief RAII wrapper for a memory-mapped file region.
 *
 * The active_map class manages a memory mapping (using mio::mmap_sink) and ensures that
 * the underlying mapping is properly unmapped when the active_map object goes out of scope.
 */
class active_map {
private:
    mio::mmap_sink _map; ///< The underlying memory mapping.

public:
    /**
     * @brief Constructs an active_map from a file.
     *
     * @param file_name The name of the file to map.
     * @param offset The offset within the file at which the mapping begins.
     * @param size The size of the mapping.
     *
     * @note Any error encountered during mapping creation is stored in a local error_code.
     */
    explicit active_map(const std::string& file_name, size_t offset, size_t size) {
        std::error_code error;
        _map = mio::make_mmap_sink(file_name, offset, size, error);
        // Optional: handle error if needed, e.g. log error.message() if error is set.
    }

    /**
     * @brief Constructs an active_map by taking ownership of an existing mmap_sink.
     *
     * @param map A mio::mmap_sink to be managed by this active_map.
     */
    explicit active_map(mio::mmap_sink map)
        : _map(std::move(map))
    {
    }

    // Deleted copy operations to prevent accidental duplication of the mapping.
    active_map(const active_map&) = delete;
    active_map& operator=(const active_map&) = delete;

    // Allow move semantics.
    active_map(active_map&& other) noexcept : _map(std::move(other._map)) {}
    active_map& operator=(active_map&& other) noexcept {
        if (this != &other) {
            // Unmap any existing mapping before taking ownership.
            if (_map.data()) {
                _map.unmap();
            }
            _map = std::move(other._map);
        }
        return *this;
    }

    /**
     * @brief Destructor that automatically unmaps the memory if mapped.
     */
    ~active_map() {
        if (_map.data()) {
            _map.unmap();
        }
    }

    /**
     * @brief Returns a reference to the underlying memory mapping.
     *
     * @return Reference to mio::mmap_sink.
     */
    mio::mmap_sink& get() {
        return _map;
    }

    /**
     * @brief Returns a const reference to the underlying memory mapping.
     *
     * @return Const reference to mio::mmap_sink.
     */
    const mio::mmap_sink& get() const {
        return _map;
    }

    /**
     * @brief Overloads the arrow operator for convenient access.
     *
     * @return Pointer to the underlying mio::mmap_sink.
     */
    mio::mmap_sink* operator->() {
        return &_map;
    }

    /**
     * @brief Overloads the arrow operator for const instances.
     *
     * @return Const pointer to the underlying mio::mmap_sink.
     */
    const mio::mmap_sink* operator->() const {
        return &_map;
    }
};


/**
 * @brief Class for file handling with support for memory mapping and dynamic expansion.
 *
 * The m_file class encapsulates Windows file handling (using CreateFileA, SetFilePointerEx, etc.)
 * along with memory mapping functionality via the active_map class.
 */
class m_file {
private:
    HANDLE _file_handle{ INVALID_HANDLE_VALUE }; ///< The file handle.
    std::string _filename;                         ///< The file name.
    size_t _size{ 0 };                             ///< Current size of the file.
    size_t _offset{ 0 };                           ///< Current offset for writing.
    bool _valid{ false };                          ///< Flag indicating if the file is open and valid.
    std::mutex _mutex;                             ///< Mutex for synchronizing file access.

    // Memory mapping-related members.
    std::shared_ptr<active_map> _current_map;        ///< The current active memory map.
    size_t _map_offset{ 0 };                         ///< Offset within the current map.

    /**
     * @brief Creates a new active_map for the file starting at a given offset.
     *
     * @param offset The offset from which the mapping should start.
     * @return A shared pointer to an active_map covering [offset, _size).
     */
    std::shared_ptr<active_map> do_map(size_t offset = 0);

    /**
     * @brief Expands the file size to accommodate additional data.
     *
     * This function doubles the current file size plus the required extra size,
     * and aligns the new size to PAGE_SIZE boundaries.
     *
     * @param size The additional number of bytes to be written.
     * @return true if the file was successfully expanded, false otherwise.
     */
    bool expand_size(size_t size);

public:
    /**
     * @brief Constructs an m_file for an existing or new file.
     *
     * @param name The name of the file.
     */
    m_file(const std::string& name);

    /**
     * @brief Constructs an m_file and sets the file size.
     *
     * If the file exists, it expands (or truncates) it to the aligned size.
     *
     * @param name The name of the file.
     * @param size The required size for the file.
     */
    m_file(const std::string& name, size_t size);

    /**
     * @brief Destructor: Releases the memory mapping and closes the file.
     *
     * Optionally deletes the file from disk.
     */
    ~m_file();

    /**
     * @brief Writes data to the file.
     *
     * If necessary, the file is expanded and a new mapping is created.
     *
     * @param buffer A pointer to the data to be written. If nullptr, only allocation occurs.
     * @param size The number of bytes to write.
     * @param active On output, receives the active_map that contains the written data.
     * @return Pointer to the destination in the mapped memory, or nullptr on failure.
     */
    char* write(char* buffer, size_t size, std::shared_ptr<active_map>& active);

    /**
     * @brief Checks whether the file is currently valid.
     *
     * @return true if valid, false otherwise.
     */
    bool valid();

    /**
     * @brief Returns the file name.
     *
     * @return A string_view of the file name.
     */
    __forceinline std::string_view file_name() { return _filename; }

    /**
     * @brief Returns the current file size.
     *
     * @return File size in bytes.
     */
    __forceinline size_t size() { std::lock_guard<std::mutex> lock(_mutex);  return _size; }
};

// Custom hash and equality functors for shared_ptr<active_map>
// They compare the underlying raw pointer to ensure that the set contains unique mappings.
template <typename T>
struct shared_ptr_address_hash {
    std::size_t operator()(const std::shared_ptr<T>& ptr) const {
        return std::hash<T*>{}(ptr.get());
    }
};

template <typename T>
struct shared_ptr_address_equal {
    bool operator()(const std::shared_ptr<T>& lhs, const std::shared_ptr<T>& rhs) const {
        return lhs.get() == rhs.get();
    }
};

/**
 * @brief A set that holds unique active_map objects based on their underlying addresses.
 */
using map_identity_set = std::unordered_set<std::shared_ptr<active_map>,
    shared_ptr_address_hash<active_map>,
    shared_ptr_address_equal<active_map>>;



/**
 * @brief Template class for buffering and dumping data to a file.
 *
 * The dumpable class accumulates data in memory and writes it to disk via an m_file instance.
 * It also tracks active memory maps to ensure that maps with the same underlying address are not duplicated.
 *
 * @tparam data_type The type of data to be dumped (default is uint8_t).
 */
template <typename data_type>
class dumpable {
protected:
    std::vector<std::span<data_type>> _data;      ///< Collection of spans representing written data.
    std::vector<data_type> _in_memory_data;         ///< In-memory data buffer.
    map_identity_set _maps;                         ///< Set to track unique memory maps.
    std::shared_ptr<m_file> _file;                  ///< Associated m_file instance for writing.

    static std::string file_name;                   ///< (Optional) static file name.

    /**
     * @brief Writes raw buffer data to the file.
     *
     * This function delegates to m_file::write, records the mapping, and returns a span covering the written data.
     *
     * @param buffer Pointer to the data to write.
     * @param size Number of elements to write.
     * @return A span covering the written data, or an empty span if the write fails.
     */
    std::span<data_type> write_data(data_type* buffer, size_t size);

public:
    dumpable() = default;
    explicit dumpable(const std::shared_ptr<m_file>& file) : _file(file) {}
    virtual ~dumpable() = default;

    /**
     * @brief Adds a single element to the in-memory buffer.
     *
     * The data is written to disk only when dump() is called.
     *
     * @param element The element to add.
     */
    __forceinline void add(const data_type& element) {
        _in_memory_data.push_back(element);
    }

    /**
     * @brief Writes raw buffer data to the file.
     *
     * @param buffer Pointer to the data.
     * @param size Number of elements in the buffer.
     * @return The index of the written data span in the internal vector, or -1 on failure.
     */
    __forceinline int dump(data_type* buffer, size_t size) {
        auto span = write_data(buffer, size);

        if (span.empty())
            return -1;

        _data.push_back(span);

        return _data.size() - 1;
    }

    /**
     * @brief Writes an external vector to the file.
     *
     * @param data The vector containing the data to write.
     * @return The index of the written data span, or -1 on failure.
     */
    __forceinline int dump(std::vector<data_type>& data) {
        return dump(data.data(), data.size());
    }

    /**
     * @brief Writes the entire in-memory buffer to the file.
     *
     * @return The index of the written data span, or -1 on failure.
     */
    __forceinline int dump() {
        return dump(_in_memory_data);
    }

    /**
     * @brief Allocates space in the file without writing actual data (fill the space with 0's).
     *
     * @param size The size in elements to allocate.
     * @return The index of the allocated span, or -1 on failure.
     */
    __forceinline int alloc_f(size_t size) {
        return dump(nullptr, size);
    }

    __forceinline std::span<data_type> span(int index) {
        if (index == -1 || index >= _data.size() )
            return std::span<data_type>();

        return _data[index];
    }
};

template<typename data_type>
std::string dumpable<data_type>::file_name;

template<typename data_type>
std::span<data_type> dumpable<data_type>::write_data(data_type* buffer, size_t size)
{
    auto byte_size = size * sizeof(data_type);
    std::error_code error;
    std::shared_ptr<active_map> map;

    // Write data to the file. The write() function returns a pointer to the mapped memory.
    auto ptr = _file->write(reinterpret_cast<char*>(buffer), byte_size, map);

    if (ptr) {
        // Insert the active_map into the set (only if its underlying address is unique).
        _maps.insert(map);
        return std::span<data_type>(reinterpret_cast<data_type*>(ptr), size);
    }
    return std::span<data_type>();
}
