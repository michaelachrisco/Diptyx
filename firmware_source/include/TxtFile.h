#pragma once

#include <cstdio>
#include <string>

class TxtFile
{
public:
    explicit TxtFile(const std::string& path);
    ~TxtFile();

    bool load();
    bool rewind();
    bool seek(long offset);

    bool readLine(
        std::string& line,
        long* lineStartOffset = nullptr,
        long* nextOffset = nullptr
    );

    const std::string& get_path() const { return m_path; }
    const std::string& get_title() const { return m_title; }
    const std::string& get_author() const { return m_author; }
    long get_file_size() const { return m_fileSize; }

    static bool isTxtPath(const std::string& path);

private:
    std::string m_path;
    std::string m_title;
    std::string m_author;

    FILE* m_file = nullptr;
    long m_fileSize = 0;
};