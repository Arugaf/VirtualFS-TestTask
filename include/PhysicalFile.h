//
// Created by arugaf on 26.08.2021.
//

#ifndef TEST_CASE_VFS_PHYSICALFILE_H
#define TEST_CASE_VFS_PHYSICALFILE_H

namespace TestTask {
    namespace fs = std::filesystem;

    template<typename stream = std::fstream>
    class PhysicalFile {
     public:
        explicit PhysicalFile() = delete;
        explicit PhysicalFile(const fs::path& filepath);

        PhysicalFile(const PhysicalFile&) = delete;

        size_t Write(const char* buf, size_t length);
        size_t Write(const char* buf, size_t length, size_t pos);
        size_t Read(char* buf, size_t length, size_t pos);

        [[nodiscard]] size_t GetSize() const;
        [[nodiscard]] const fs::path& GetPath() const;

     private:
        stream str;
        fs::path filepath;

        size_t file_size;

        std::mutex io_mutex;
    };

    template<typename stream>
    PhysicalFile<stream>::PhysicalFile(const fs::path& filepath) : str(stream()), filepath(filepath), file_size(0) {
        if (!fs::exists(filepath)) {
            str.open(filepath, stream::out);
            str.close();
        }

        file_size = GetSize();

        str.open(filepath, stream::out | stream::in | stream::binary);
    }

    template<typename stream>
    inline size_t PhysicalFile<stream>::GetSize() const {
        return file_size ? : fs::file_size(filepath);
    }

    template<typename stream>
    size_t PhysicalFile<stream>::Write(const char* buf, size_t length, size_t pos) {
        if (!buf || !length || pos > GetSize()) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(io_mutex);

        str.seekp(pos);
        auto prev = str.tellp();

        str.write(buf, length);
        str.flush();

        if (pos + length > file_size) {
            file_size = str.tellp();
        }

        return str.tellp() - prev;
    }

    template<typename stream>
    size_t PhysicalFile<stream>::Write(const char* buf, size_t length) {
        return Write(buf, length, GetSize());
    }

    template<typename stream>
    size_t PhysicalFile<stream>::Read(char* buf, size_t length, size_t pos) {
        if (!buf || !length || pos >= GetSize()) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(io_mutex);

        str.seekg(pos);
        auto prev = str.tellg();

        str.read(buf, std::min(length, GetSize() - pos));

        return str.tellg() - prev;
    }

    template<typename stream>
    const fs::path& PhysicalFile<stream>::GetPath() const {
        return filepath;
    }
}

#endif //TEST_CASE_VFS_PHYSICALFILE_H
