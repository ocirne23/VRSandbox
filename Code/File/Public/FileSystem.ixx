export module File.FileSystem;
extern "C++" {

import Core;

export class FileSystem final
{
public:

    static bool initialize();

    static std::string readFileStr(const std::string& path);

private:
    FileSystem() {};
    ~FileSystem() {};
};
} // extern "C++"