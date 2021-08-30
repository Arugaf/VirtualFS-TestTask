#include <iostream>
#include <chrono>
#include <thread>

#include <VFS.h>

int main() {
    TestTask::VFS vfs({"1.vfs", "2.vfs", "3.vfs", "4.vfs", "5.vfs"});
    auto v_file = vfs.Create("/new_dir/new_file");

    char input[] = "Hello world!";
    std::cout << "Number of written bytes: " << vfs.Write(v_file, input, 12) << std::endl;

    vfs.Close(v_file);

    char output[13]{};
    v_file = vfs.Open("/new_dir/new_file");
    std::cout << "Number of read bytes: " << vfs.Read(v_file, output, 12) << std::endl << output << std::endl;

    vfs.Close(v_file);

    return 0;
}
