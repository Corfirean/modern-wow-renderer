#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
namespace renderer {
struct WoWClientContext {
    uintptr_t base=0;
    uint32_t imageSize=0,timestamp=0;
    std::string sha256;
    bool Detect();
    bool Read(uintptr_t rva,void* value,size_t size) const;
};
}
