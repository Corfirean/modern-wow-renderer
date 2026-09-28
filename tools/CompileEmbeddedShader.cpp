#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: CompileEmbeddedShader <cpp-file> <symbol>\n";
        return 2;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 3;
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    const std::wstring wideSymbol(argv[2]);
    std::string symbol;
    symbol.reserve(wideSymbol.size());
    for (wchar_t character : wideSymbol)
        symbol.push_back(static_cast<char>(character));
    const std::string marker = "const char* " + symbol + " = R\"HLSL(";
    const size_t begin = text.find(marker);
    if (begin == std::string::npos) return 4;
    const size_t sourceBegin = begin + marker.size();
    const size_t sourceEnd = text.find(")HLSL\";", sourceBegin);
    if (sourceEnd == std::string::npos) return 5;
    const std::string source = text.substr(sourceBegin, sourceEnd - sourceBegin);

    ID3DBlob* shader = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
                                  "main", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                  0, &shader, &errors);
    if (errors)
    {
        std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize());
        errors->Release();
    }
    if (FAILED(hr) || !shader) return 6;
    std::cout << symbol << ": compiled, bytecode=" << shader->GetBufferSize() << " bytes\n";
    shader->Release();
    return 0;
}
