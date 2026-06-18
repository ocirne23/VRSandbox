export module File.FileSystem;

import File.fwd;

import Core;

export class FileSystem final
{
public:

    static bool initialize();

    static std::string readFileStr(const std::string& path);

    // Writes (creating/truncating) a text file. Returns false if the file could not be opened.
    static bool writeFileStr(const std::string& path, const std::string& content);

private:
    FileSystem() {};
    ~FileSystem() {};
};