export module File.FileSystem;

import Core;
import File.fwd;

export class FileSystem final
{
public:

    static bool initialize();

    static std::string readFileStr(const std::string& path);

private:
    FileSystem() {};
    ~FileSystem() {};
};