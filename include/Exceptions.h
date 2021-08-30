//
// Created by arugaf on 29.08.2021.
//

#ifndef TEST_CASE_VFS_EXCEPTIONS_H
#define TEST_CASE_VFS_EXCEPTIONS_H

struct NoFiles : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "No files";
    }
};

struct TooManyFiles : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "Too many files";
    }
};

struct RootIsNotDirectory : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "Root is not directory";
    }
};

struct RootDoesNotExist : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "Root does not exist";
    }
};

struct FileWritingError : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "File writing error";
    }
};

struct FileReadingError : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "File reading error";
    }
};

struct FileAlreadyExists : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "File already exists";
    }
};

struct DirAlreadyExists : public std::exception {
    [[nodiscard]] const char* what() const noexcept override {
        return "Directory already exists";
    }
};

#endif //TEST_CASE_VFS_EXCEPTIONS_H
