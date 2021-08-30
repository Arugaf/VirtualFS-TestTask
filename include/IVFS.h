//
// Created by arugaf on 26.08.2021.
//

#ifndef TEST_CASE_VFS_IVFS_H
#define TEST_CASE_VFS_IVFS_H

#include <cstddef>

namespace TestTask {
    struct File; // Вы имеете право как угодно задать содержимое этой структуры

    struct IVFS {
        virtual File* Open(const char* name) = 0; // Открыть файл в readonly режиме. Если нет такого файла - вернуть nullptr
        virtual File* Create(const char* name) = 0; // Открыть или создать файл в writeonly режиме. Если нужно, то создать все нужные поддиректории, упомянутые в пути
        virtual size_t Read(File* f, char* buff, size_t len) = 0; // Прочитать данные из файла. Возвращаемое значение - сколько реально байт удалось прочитать
        virtual size_t Write(File* f, char* buff, size_t len) = 0; // Записать данные в файл. Возвращаемое значение - сколько реально байт удалось записать
        virtual void Close(File* f) = 0; // Закрыть файл
    };
}

#endif //TEST_CASE_VFS_IVFS_H
