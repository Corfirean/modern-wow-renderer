#include "WoWClientContext.h"
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#pragma comment(lib,"bcrypt.lib")
namespace renderer {
bool WoWClientContext::Read(uintptr_t rva,void* value,size_t size) const {
    if(!base||!size||rva>=imageSize||size>imageSize-rva)return false;
    MEMORY_BASIC_INFORMATION region{};
    const auto address=base+rva;
    if(VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))!=sizeof(region)||region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    if(!(region.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    if(size>region.RegionSize-(address-reinterpret_cast<uintptr_t>(region.BaseAddress)))return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),value,size,&copied)&&copied==size;
}
bool WoWClientContext::Detect() {
    base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));imageSize=4096;
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    if(!Read(0,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||!Read(dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC) {base=0;return false;}
    imageSize=nt.OptionalHeader.SizeOfImage;timestamp=nt.FileHeader.TimeDateStamp;
    wchar_t path[32768]{};if(!GetModuleFileNameW(nullptr,path,32768))return true;
    std::ifstream file{std::filesystem::path(path),std::ios::binary};
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return true;
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
        std::array<char,65536> buffer;bool ok=bool(file);
        while(file.read(buffer.data(),buffer.size())||file.gcount()) if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(file.gcount()),0)<0)ok=false;
        if(file.bad())ok=false;
        std::array<unsigned char,32> digest{};
        if(ok&&BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0) {
            std::ostringstream result;result<<std::hex<<std::setfill('0');for(auto b:digest)result<<std::setw(2)<<unsigned(b);sha256=result.str();
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm,0);return true;
}
}
