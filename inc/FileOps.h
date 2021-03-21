#pragma once
#include <iostream>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

inline bool FileExists(const std::string& filename)
{
    fs::path file_path = filename;
    return fs::exists(file_path);
}

inline uintmax_t FileSize(const std::string& filename)
{
    fs::path file_path = filename;
    return fs::file_size(file_path);
}

inline void MakeFile(const std::string& filename)
{
    if (FileExists(filename) && fs::is_regular_file(filename))
        return;
    fs::path file_path = filename;
    file_path = fs::absolute(file_path);
    std::ofstream newfile(file_path, std::ios::binary | std::ios::out);
    if (!newfile)
    {
        std::cout << "Failed to create file: " << file_path << std::endl;
    }
    newfile.close();
}

inline void ResizeFile(const std::string& filename, uintmax_t newsize)
{
    if (!(FileExists(filename) && fs::is_regular_file(filename)))
        return;

    fs::resize_file(filename, newsize);
}

inline std::string NewFileExtension(const std::string& filename, const std::string& extension)
{
    fs::path filepath = filename;
    filepath.replace_extension(extension);
    std::string return_string = filepath.string();
    return return_string;
}