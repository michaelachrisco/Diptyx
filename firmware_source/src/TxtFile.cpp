#include "TxtFile.h"

#include <cctype>
#include <cstring>

static constexpr size_t TXT_READ_BUFFER_SIZE = 256;

TxtFile::TxtFile(const std::string& path)
    : m_path(path),
      m_author("txt file")
{
    // The book title is the filename, exactly as it appears on disk.
    const size_t slash = m_path.find_last_of("/\\");

    if (slash == std::string::npos)
    {
        m_title = m_path;
    }
    else
    {
        m_title = m_path.substr(slash + 1);
    }
}

TxtFile::~TxtFile()
{
    if (m_file != nullptr)
    {
        fclose(m_file);
        m_file = nullptr;
    }
}

bool TxtFile::isTxtPath(const std::string& path)
{
    const size_t dot = path.find_last_of('.');

    if (dot == std::string::npos || dot + 1 >= path.size())
    {
        return false;
    }

    std::string extension = path.substr(dot);

    for (char& c : extension)
    {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))
        );
    }

    return extension == ".txt";
}

bool TxtFile::load()
{
    if (m_file != nullptr)
    {
        fclose(m_file);
        m_file = nullptr;
    }

    m_file = fopen(m_path.c_str(), "rb");

    if (m_file == nullptr)
    {
        m_fileSize = 0;
        return false;
    }

    if (fseek(m_file, 0, SEEK_END) != 0)
    {
        fclose(m_file);
        m_file = nullptr;
        m_fileSize = 0;
        return false;
    }

    const long size = ftell(m_file);

    if (size < 0)
    {
        fclose(m_file);
        m_file = nullptr;
        m_fileSize = 0;
        return false;
    }

    m_fileSize = size;

    return rewind();
}

bool TxtFile::rewind()
{
    if (m_file == nullptr)
    {
        return false;
    }

    clearerr(m_file);

    return fseek(m_file, 0, SEEK_SET) == 0;
}

bool TxtFile::seek(long offset)
{
    if (m_file == nullptr || offset < 0)
    {
        return false;
    }

    clearerr(m_file);

    return fseek(m_file, offset, SEEK_SET) == 0;
}

bool TxtFile::readLine(
    std::string& line,
    long* lineStartOffset,
    long* nextOffset)
{
    if (m_file == nullptr)
    {
        return false;
    }

    const long startOffset = ftell(m_file);

    if (startOffset < 0)
    {
        return false;
    }

    line.clear();

    char buffer[TXT_READ_BUFFER_SIZE];
    bool gotData = false;

    while (fgets(buffer, sizeof(buffer), m_file) != nullptr)
    {
        gotData = true;
        line.append(buffer);

        const size_t length = std::strlen(buffer);

        if (length > 0 && buffer[length - 1] == '\n')
        {
            break;
        }
    }

    if (!gotData)
    {
        return false;
    }

    // Normalize CRLF and LF.
    if (!line.empty() && line.back() == '\n')
    {
        line.pop_back();
    }

    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    // Remove UTF-8 BOM from the beginning of the file.
    if (startOffset == 0 &&
        line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
    {
        line.erase(0, 3);
    }

    if (lineStartOffset != nullptr)
    {
        *lineStartOffset = startOffset;
    }

    if (nextOffset != nullptr)
    {
        const long offset = ftell(m_file);

        if (offset < 0)
        {
            *nextOffset = startOffset;
        }
        else
        {
            *nextOffset = offset;
        }
    }

    return true;
}