#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace fs {
struct FS;
}

struct File {
    std::string *data = nullptr;
    size_t offset = 0;
    explicit operator bool() const { return data != nullptr; }
    int available() const { return data && offset < data->size(); }
    size_t readBytesUntil(char stop, char *buf, size_t n)
    {
        size_t i = 0;
        while (i < n && offset < data->size() && (*data)[offset] != stop)
            buf[i++] = (*data)[offset++];
        if (offset < data->size() && (*data)[offset] == stop)
            ++offset;
        return i;
    }
    size_t printf(const char *fmt, ...)
    {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n < 0 || n >= (int)sizeof(buf))
            return 0;
        *data += buf;
        return (size_t)n;
    }
    void flush() {}
    void close() {}
};

namespace fs {
struct FS {
    std::map<std::string, std::string> files;
    File open(const char *name, const char *mode)
    {
        if (!strcmp(mode, "w")) {
            files[name] = "";
            return File{&files[name], 0};
        }
        auto it = files.find(name);
        return it == files.end() ? File{} : File{&it->second, 0};
    }
    bool rename(const char *from, const char *to)
    {
        auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second;
        files.erase(it);
        return true;
    }
};
}
extern fs::FS LittleFS;
