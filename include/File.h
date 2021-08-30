//
// Created by arugaf on 26.08.2021.
//

#ifndef TEST_CASE_VFS_FILE_H
#define TEST_CASE_VFS_FILE_H

#include "VFS.h"

namespace TestTask {
    namespace fs = std::filesystem;

    enum class FileStatus {
        openW,
        openR,
        closed
    };

    struct File {
        explicit File(const fs::path& p_file, const fs::path& name, size_t page) : p_file(p_file), name(name), page(page), readers(0), data_len(0) {};

        const fs::path& p_file;
        const fs::path name;

        size_t data_len = 0;
        size_t page = 0;

        FileStatus status = FileStatus::closed;

        std::atomic<size_t> readers = 0;
    };
}
#endif //TEST_CASE_VFS_FILE_H
