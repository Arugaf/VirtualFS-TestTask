//
// Created by arugaf on 26.08.2021.
//

#ifndef TEST_CASE_VFS_VFS_H
#define TEST_CASE_VFS_VFS_H

#include <algorithm>
#include <future>
#include <filesystem>
#include <string>
#include <iostream>
#include <map>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <stack>

#include "IVFS.h"
#include "File.h"
#include "PhysicalFile.h"
#include "Exceptions.h"

namespace TestTask {
    namespace fs = std::filesystem;

    enum FileType : char {
        dir = 0b00000111,
        file = 0b01110000
    };

    /*
     * Solution based on page memory design where file consists of fixed-size blocks of bytes, each of one representing
     * content of directory or file. All files and directories have unique page numbers which are stored in
     * corresponding parent directory.
     *
     * All info about files consists of type (1 byte), name size (sizeof(size_t) bytes), name (name size bytes) and
     * page number (sizeof(size_t) bytes). If there is no more size inside page - class creates new one etc. Last
     * sizeof(size_t) bytes contains number of next page of related info. All new pages are added to the end of file.
     * Thus, all pages of virtual file or dir usually somewhat highly fragmented.
     *
     * First page of virtual file starts with number representing byte count (excluding this field and fields with
     * num of next page). Also, first page of physical file contains number of all virtual files inside this file.
     *
     * For performance reasons it's impossible to create files inside root directory.
     *
     */

    template<size_t max_files = 5,
            size_t page_size = 1u << 12,
            typename stream = std::fstream>
    class VFS : public IVFS {
    public:
        using Page = std::array<char, page_size>;
        using PFile = PhysicalFile<stream>;

        VFS() = delete;
        VFS(const std::initializer_list<fs::path>& files, const fs::path& root = fs::current_path());
        VFS(const VFS&) = delete;
        VFS(VFS&&) = delete;

        File* Open(const char* name) override;
        File* Create(const char* name) override;

        size_t Read(File* f, char* buff, size_t len) override;
        size_t Write(File* f, char* buff, size_t len) override;

        void Close(File* f) override;

        VFS& operator=(const VFS&) = delete;
        VFS& operator=(VFS&&) = delete;

    private:
        struct Dir {
            const fs::path& p_file;
            size_t page;
        };

        std::map<fs::path, PhysicalFile<stream>> physical_files;
        std::unordered_map<std::string, File> virtual_files;
        std::unordered_map<std::string, Dir> virtual_dirs;

        std::mutex editing_files_mutex;
        std::mutex v_files_mutex;
        std::mutex v_dirs_mutex;
        std::map<fs::path, std::recursive_mutex> io_mutexes;

        static constexpr size_t st_size = sizeof(size_t);
        const size_t& start;

        std::atomic<size_t> num_of_files;

        const Page zeroes_sequence{0};
        const fs::path vRoot = "/";

        void Init(const fs::path& path, PFile& file);
        auto ReadFileInfo(const Page& page, size_t pos) const;
        size_t FindFileInPage(const Page& page, const fs::path& file_name,
                              FileType type = FileType::file) const;
        size_t FindPageEnd(const Page& page) const;


        size_t GetNextPage(const Page& page) const;
        void SetNextPage(PFile& p_file, size_t page, size_t next_page);

        size_t GetFileLength(const Page& page) const;
        void SetFileLength(PhysicalFile <stream>& p_file, size_t page, size_t len);

        void InsertDir(std::string name, size_t page, const fs::path& path);
        void InsertFile(std::string name, size_t page, const fs::path& path);
        void IncrementNumOfFiles(PFile& p_file);

        PFile& FindSmallestPFile();

        void CreateFile(const fs::path& parent_dir, const fs::path& file,
                        PFile& p_file,
                        const fs::path& p_file_path, FileType type = FileType::file);
        size_t WriteToFile(const char* buf, PFile& p_file,
                           const fs::path& p_file_path, size_t len,
                           size_t page, size_t pos, bool carry = true);
        void CreateEmptyPage(PFile& p_file);

        std::stack<fs::path> OpenExistingDirectories(fs::path& file_path);
        bool OpenExistingFile(PFile& p_file, const fs::path& file_path);
    };


    template<size_t max_files, size_t page_size, typename stream>
    VFS<max_files, page_size, stream>::VFS(const std::initializer_list<fs::path>& files,
                                           const fs::path& root) : num_of_files(0), start(st_size) {
        if (!files.size()) {
            throw NoFiles();
        }

        if (files.size() > max_files) {
            throw TooManyFiles();
        }

        if (!fs::exists(root)) {
            fs::create_directories(root);
        }

        if (!fs::is_directory(root)) {
            throw RootIsNotDirectory();
        }

        for (auto file : files) {
            if (file.is_relative()) {
                file = fs::absolute(root) / file;
            }

            if (!fs::exists(file.parent_path())) {
                fs::create_directories(file.parent_path());
            }

            if (physical_files.count(file) == 0) {
                physical_files.emplace(file, file);
                io_mutexes[file];
            } else {
                throw FileAlreadyExists();
            }

            // If file is empty then initialize it with empty page of page_size filled with zero sequence
            if (!physical_files.at(file).GetSize()) {
                if (st_size != physical_files.at(file).Write(zeroes_sequence.data(), st_size)) {
                    throw FileWritingError();
                }
                CreateEmptyPage(physical_files.at(file));
            }
        }

        // May use async cause all files have individual descriptor and io_stream
        std::vector<std::future<void>> promises;

        for (auto& p_file : physical_files) {
            promises.push_back(std::async(std::launch::async, [this, &p_file]() {
                this->Init(p_file.first, p_file.second);
            }));
        }

        for (auto& promise : promises) {
            promise.get();
        }
    }

    template<size_t max_files, size_t page_size, typename stream>
    File* VFS<max_files, page_size, stream>::Open(const char* name) {
        if (!name) {
            return nullptr;
        }

        fs::path v_file_path(name);

        // Create absolute path
        if (!v_file_path.has_root_directory()) {
            v_file_path = vRoot / v_file_path;
        }

        if (v_file_path == vRoot || v_file_path.parent_path() == vRoot) {
            return nullptr;
        }

        // Guard whole big section with mutex cause we don't want anyone to open file with the wrong mode
        std::lock_guard<std::mutex> lock(editing_files_mutex);

        // There are may be many writers
        if (virtual_files.count(v_file_path)) {
            auto& file = virtual_files.at(v_file_path);
            if (file.status != FileStatus::openR) {
                return nullptr;
            }

            ++file.readers;
            return &file;
        }

        fs::path parent_path = v_file_path.parent_path();

        // If stack with directories is not empty then the file doesn't exist
        if (OpenExistingDirectories(parent_path).size()) {
            return nullptr;
        }

        auto& p_file = physical_files.at(virtual_dirs.at(parent_path).p_file);

        bool file_found = false;
        if (v_file_path.parent_path() != vRoot) {
            file_found = OpenExistingFile(p_file, v_file_path);
        }

        if (file_found) {
            auto& file = virtual_files.at(v_file_path);

            Page buf;
            if (p_file.Read(buf.data(), page_size, file.page * page_size + start) != page_size) {
                throw FileReadingError();
            }
            file.data_len = GetFileLength(buf);

            file.status = FileStatus::openR;
            ++file.readers;
            return &file;
        }

        return nullptr;
    }

    template<size_t max_files, size_t page_size, typename stream>
    File* VFS<max_files, page_size, stream>::Create(const char* name) {
        if (!name) {
            return nullptr;
        }

        fs::path v_file_path(name);

        // Create absolute path
        if (!v_file_path.has_root_directory()) {
            v_file_path = vRoot / v_file_path;
        }

        if (v_file_path == vRoot || v_file_path.parent_path() == vRoot) {
            return nullptr;
        }

        // Guard whole big section with mutex cause we don't want anyone to open file with the wrong mode
        std::lock_guard<std::mutex> lock(editing_files_mutex);

        // There is may be only one writer
        if (virtual_files.count(v_file_path)) {
            return nullptr;
        }

        fs::path parent_path = v_file_path.parent_path();
        auto dirs = OpenExistingDirectories(parent_path);

        // If parent_path equals root then we need to create all directories in path. Thus, it is possible to
        // find physical file with less size
        auto& p_file = parent_path == vRoot ? FindSmallestPFile() : physical_files.at(virtual_dirs.at(parent_path).p_file);

        bool file_found = false;
        if (!dirs.size() && v_file_path.parent_path() != vRoot) {
            file_found = OpenExistingFile(p_file, v_file_path);
        }

        std::lock_guard<std::recursive_mutex> write_lock(io_mutexes.at(p_file.GetPath()));

        // Create all non-existing directories
        while (dirs.size()) {
            parent_path = dirs.top();
            dirs.pop();

            CreateFile(parent_path.parent_path(), parent_path, p_file, p_file.GetPath(), FileType::dir);
        }

        if (!file_found) {
            CreateFile(v_file_path.parent_path(), v_file_path, p_file, p_file.GetPath());
        } else {
            Page buf;
            if (p_file.Read(buf.data(), page_size, virtual_files.at(v_file_path).page * page_size + start) != page_size) {
                throw FileReadingError();
            }
            virtual_files.at(v_file_path).data_len = GetFileLength(buf);
        }

        virtual_files.at(v_file_path).status = FileStatus::openW;

        return &virtual_files.at(v_file_path);
    }

    template<size_t max_files, size_t page_size, typename stream>
    size_t VFS<max_files, page_size, stream>::Read(File* f, char* buff, size_t len) {
        if (!f || !buff || !len || f->status != FileStatus::openR) {
            return 0;
        }

        auto& p_file = physical_files.at(f->p_file);

        len = len <= f->data_len ? len : f->data_len;

        if (len <= page_size - st_size - st_size) {
            return p_file.Read(buff, len, f->page * page_size + start + st_size);
        }

        size_t read_bytes = p_file.Read(buff, page_size - st_size - st_size, f->page * page_size + start + st_size);
        size_t page = f->page;

        while (read_bytes != len) {
            p_file.Read(reinterpret_cast<char*>(&page), st_size, (page + 1) * page_size - st_size + start);
            read_bytes += p_file.Read(buff + read_bytes, std::min(page_size - st_size, len - read_bytes), f->page * page_size + start + st_size);
        }

        return read_bytes;
    }

    template<size_t max_files, size_t page_size, typename stream>
    size_t VFS<max_files, page_size, stream>::Write(File* f, char* buff, size_t len) {
        if (!f || !buff || !len || f->status != FileStatus::openW) {
            return 0;
        }

        auto& p_file = physical_files.at(f->p_file);
        auto written_bytes = WriteToFile(buff, p_file, f->p_file, len, f->page, (f->data_len % (page_size - st_size)) + st_size);

        f->data_len += written_bytes;
        SetFileLength(p_file, f->page, f->data_len);

        return written_bytes;
    }

    template<size_t max_files, size_t page_size, typename stream>
    void VFS<max_files, page_size, stream>::Close(File* f) {
        if (!f || !virtual_files.count(f->name)) {
            return;
        }

        if (f->readers) {
            if (--(f->readers)) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(editing_files_mutex);
        virtual_files.erase(f->name);
    }

    template<size_t max_files, size_t page_size, typename stream>
    void VFS<max_files, page_size, stream>::Init(const fs::path& path, PFile& file) {
        // Read number of files in physical file
        std::array<char, st_size> st_buf{};
        if (st_size != file.Read(st_buf.data(), st_size, 0)) {
            throw FileReadingError();
        }

        // If there is no files inside there is nothing to find
        if (!(*reinterpret_cast<size_t*>(st_buf.data()))) {
            return;
        }

        num_of_files += *reinterpret_cast<size_t*>(st_buf.data());

        Page buf{0};
        size_t page = 0;

        // Find all directories in root directory inside physical file
        do {
            if (file.Read(buf.data(), page_size, page * page_size + start) != page_size) {
                throw FileReadingError();
            }

            size_t pos = 0;

            do {
                auto [info, new_pos] = ReadFileInfo(buf, pos);

                switch (FileType(info.type)) {
                    case FileType::dir: {
                        InsertDir(std::move(info.name), info.page, path);
                    }

                    case FileType::file:

                    default: {
                        break;
                    }
                }

                pos = new_pos;
            } while (pos > 0 && pos < page_size - st_size);

            page = GetNextPage(buf);
        } while (page > 0);
    }

    template<size_t max_files, size_t page_size, typename stream>
    auto VFS<max_files, page_size, stream>::ReadFileInfo(const Page& page, size_t pos) const {
        struct Info {
            char type{};
            std::string name{};
            size_t page{};
        };

        struct Result {
            Info info;
            size_t pos{};
        };

        std::string_view str_buf(page.data(), page_size);

        auto type = *str_buf.substr(pos++, 1).data();

        if (!type) {
            return Result {Info{}, 0};
        }

        auto name_size = *reinterpret_cast<const size_t*>(str_buf.substr(pos, st_size).data());
        std::string name(str_buf.substr(pos + st_size, name_size));
        auto page_num = *reinterpret_cast<const size_t*>(str_buf.substr(pos + st_size + name_size, st_size).data());

        return Result {
                Info {type, std::move(name), page_num},
                pos + st_size + name_size + st_size
        };
    }

    template<size_t max_files, size_t page_size, typename stream>
    inline size_t VFS<max_files, page_size, stream>::GetNextPage(const Page& page) const {
        return *reinterpret_cast<const size_t*>(std::string_view(page.data() + page_size - st_size, st_size).data());
    }

    template<size_t max_files, size_t page_size, typename stream>
    inline void VFS<max_files, page_size, stream>::SetNextPage(PhysicalFile <stream>& p_file, size_t page,
                                                               size_t next_page) {
        p_file.Write(reinterpret_cast<const char*>(&next_page), st_size, (page + 1) * page_size - st_size + start);
    }

    template<size_t max_files, size_t page_size, typename stream>
    void VFS<max_files, page_size, stream>::InsertDir(std::string name, size_t page, const fs::path& path) {
        const std::lock_guard<std::mutex> lock(v_dirs_mutex);

        if (virtual_dirs.count(name) == 0) {
            virtual_dirs.insert({std::move(name), {path, page}});
        } else {
            throw DirAlreadyExists();
        }
    }

    template<size_t max_files, size_t page_size, typename stream>
    void VFS<max_files, page_size, stream>::InsertFile(std::string name, size_t page, const fs::path& path) {
        const std::lock_guard<std::mutex> lock(v_files_mutex);

        if (virtual_files.count(name) == 0) {
            virtual_files.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(name),
                                  std::forward_as_tuple(path, name, page));
        } else {
            throw FileAlreadyExists();
        }
    }

    template<size_t max_files, size_t page_size, typename stream>
    inline PhysicalFile<stream>& VFS<max_files, page_size, stream>::FindSmallestPFile() {
        return std::min_element(physical_files.begin(), physical_files.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second.GetSize() < rhs.second.GetSize();
        })->second;
    }

    template<size_t max_files, size_t page_size, typename stream>
    size_t VFS<max_files, page_size, stream>::FindFileInPage(const Page& page, const fs::path& file_name,
                                                             FileType type) const {
        std::string_view page_view(page.data(), page_size);
        auto name_size = file_name.string().size();

        auto name_size_str = reinterpret_cast<char *>(&name_size);

        std::string query(reinterpret_cast<char *>(&type), 1);
        query += std::string(name_size_str, st_size);
        query += file_name.string();

        return page_view.find(query);
    }

    template<size_t max_files, size_t page_size, typename stream>
    void VFS<max_files, page_size, stream>::CreateFile(const fs::path& parent_dir, const fs::path& file,
                                                       PFile& p_file, const fs::path& p_file_path, FileType type) {
        CreateEmptyPage(p_file);

        size_t page = parent_dir == vRoot ? 0 : virtual_dirs.at(parent_dir).page;

        Page buf{};
        p_file.Read(buf.data(), page_size, page_size * page + start);
        for (size_t next_page = GetNextPage(buf); next_page != 0; next_page = GetNextPage(buf)) {
            page = next_page;
            p_file.Read(buf.data(), page_size, page_size * next_page + start);
        }

        size_t name_size = file.string().size();
        size_t next_page = (p_file.GetSize() - st_size) / page_size - 1;

        std::string file_info;
        file_info += type;
        file_info += std::string(reinterpret_cast<char*>(&name_size), st_size);
        file_info += file;
        file_info += std::string(reinterpret_cast<char*>(&next_page), st_size);

        WriteToFile(file_info.c_str(), p_file,
                    p_file_path,
                    1 + st_size + name_size + st_size,
                    page,
                    FindPageEnd(buf),
                    false);

        switch (type) {
            case FileType::file: {
                InsertFile(file, next_page, p_file_path);
                IncrementNumOfFiles(p_file);
                break;
            }

            case FileType::dir: {
                InsertDir(file, next_page, p_file_path);
                break;
            }
        }
    }

    template<size_t max_files, size_t page_size, typename stream>
    size_t VFS<max_files, page_size, stream>::WriteToFile(const char* buf, PhysicalFile <stream>& p_file,
                                                   const fs::path& p_file_path, size_t len,
                                                   size_t page, size_t pos, bool carry) {
        std::lock_guard<std::recursive_mutex> write_lock(io_mutexes.at(p_file_path));

        size_t written_bytes = 0;

        if (len <= page_size - pos - st_size) {
            return p_file.Write(buf, len, page * page_size + pos + start);
        }


        if (carry) {
            written_bytes = p_file.Write(buf, page_size - pos - st_size, page * page_size + pos + start);
        }

        while (len - written_bytes) {
            SetNextPage(p_file, page, (p_file.GetSize() - st_size) / page_size);
            written_bytes += p_file.Write(buf + written_bytes, std::min(page_size - st_size, len - written_bytes));
        }

        p_file.Write(zeroes_sequence.data(), page_size - (written_bytes % page_size));

        return written_bytes;
    }

    template<size_t max_files, size_t page_size, typename stream>
    void VFS<max_files, page_size, stream>::CreateEmptyPage(PhysicalFile <stream>& p_file) {
        p_file.Write(zeroes_sequence.data(), page_size);
    }

    template<size_t max_files, size_t page_size, typename stream>
    size_t VFS<max_files, page_size, stream>::FindPageEnd(const Page& page) const {
        std::string_view page_view(page.data(), page_size);
        size_t pos = 0;
        size_t name_size = 0;

        while (page[pos] != 0 && pos < page_size) {
            name_size = *reinterpret_cast<const size_t*>(page_view.substr(++pos, st_size).data());
            pos += name_size + st_size + st_size;
        }

        return std::min(pos, page_size - st_size);
    }

    template<size_t max_files, size_t page_size, typename stream>
    void VFS<max_files, page_size, stream>::IncrementNumOfFiles(PhysicalFile <stream>& p_file) {
        std::array<char, st_size> buf{};

        if (st_size != p_file.Read(buf.data(), st_size, 0)) {
            throw FileReadingError();
        }

        auto counter = reinterpret_cast<size_t*>(buf.data());
        ++(*counter);

        if (st_size != p_file.Write(buf.data(), st_size, 0)) {
            throw FileWritingError();
        }

        ++num_of_files;
    }

    template<size_t max_files, size_t page_size, typename stream>
    inline size_t VFS<max_files, page_size, stream>::GetFileLength(const Page& page) const {
        return *reinterpret_cast<const size_t*>(std::string_view(page.data(), st_size).data());
    }

    template<size_t max_files, size_t page_size, typename stream>
    inline void VFS<max_files, page_size, stream>::SetFileLength(PhysicalFile <stream>& p_file, size_t page,
                                                          size_t len) {
        p_file.Write(reinterpret_cast<const char*>(&len), st_size, (page * page_size) + start);
    }

    template<size_t max_files, size_t page_size, typename stream>
    std::stack<fs::path> VFS<max_files, page_size, stream>::OpenExistingDirectories(fs::path& file_path) {
        std::stack<fs::path> dirs;

        // Look for open virtual directories and add them into stack
        if (file_path != vRoot) {
            for (; !virtual_dirs.count(file_path) && file_path != vRoot;
                 dirs.push(file_path), file_path = file_path.parent_path());
        }

        // Trying to find and open remaining directories
        if (!dirs.empty() && file_path != vRoot) {
            auto open_dir = virtual_dirs.at(file_path);

            Page buf{};

            auto page = open_dir.page;


            while (!dirs.empty()) {
                do {
                    if (page_size != physical_files.at(open_dir.p_file).Read(buf.data(), page_size, page_size * page + start)) {
                        throw FileReadingError();
                    }

                    auto pos = FindFileInPage(buf, dirs.top(), FileType::dir);
                    if (pos == std::string::npos) {
                        page = GetNextPage(buf);
                    } else {
                        auto [info, new_pos] = ReadFileInfo(buf, pos);
                        page = info.page;

                        InsertDir(info.name, info.page, open_dir.p_file);
                        break;
                    }
                } while (page);

                if (!page) {
                    break;
                }

                file_path = dirs.top();
                dirs.pop();
            }
        }

        return dirs;
    }

    template<size_t max_files, size_t page_size, typename stream>
    bool VFS<max_files, page_size, stream>::OpenExistingFile(PFile& p_file, const fs::path& file_path) {
        Page buf{};

        auto page = virtual_dirs.at(file_path.parent_path()).page;

        do {
            if (page_size != p_file.Read(buf.data(), page_size, page_size * page + start)) {
                throw FileReadingError();
            }

            auto pos = FindFileInPage(buf, file_path);
            if (pos == std::string::npos) {
                page = GetNextPage(buf);
            } else {
                auto [info, new_pos] = ReadFileInfo(buf, pos);

                InsertFile(file_path, info.page, p_file.GetPath());
                return true;
            }
        } while (page != 0);

        return false;
    }
}

#endif //TEST_CASE_VFS_VFS_H
