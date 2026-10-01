#pragma once
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <cmath>
#pragma comment(lib,"gdi32.lib")
namespace tuningoverlay {
// System font rasterized once into a managed atlas. No redistributed font,
// no D3DX dependency, and no GDI calls per glyph/per frame.
class OverlayFont {
    struct GlyphInfo {float u=0,v=0,w=0,h=0,advance=0;};
    struct Vertex {float x,y,z,w;DWORD color;float u,v;};
    Microsoft::WRL::ComPtr<IDirect3DTexture9> texture;
    std::array<GlyphInfo,352> glyphs{};
    IDirect3DDevice9* owner=nullptr;
    int pixels=0;
    static wchar_t Character(size_t i) {return wchar_t(i<96?32+i:0x400+i-96);}
public:
    void Reset() {texture.Reset();owner=nullptr;pixels=0;}
    bool Ensure(IDirect3DDevice9* device,int size) {
        if(owner==device&&pixels==size)return bool(texture);
        Reset();owner=device;pixels=size;
        const int cell=size*2+4;int width=1,height=1;while(width<cell*16)width*=2;while(height<cell*22)height*=2;
        HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        HFONT font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!bitmap||!font) {if(bitmap)DeleteObject(bitmap);if(font)DeleteObject(font);DeleteDC(dc);return false;}
        auto oldBitmap=SelectObject(dc,bitmap);auto oldFont=SelectObject(dc,font);
        SetBkColor(dc,RGB(0,0,0));SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,OPAQUE);
        memset(bits,0,size_t(width)*height*4);
        for(size_t i=0;i<glyphs.size();++i) {
            wchar_t c=Character(i);SIZE extent{};GetTextExtentPoint32W(dc,&c,1,&extent);
            int x=int(i%16)*cell,y=int(i/16)*cell;TextOutW(dc,x,y,&c,1);
            glyphs[i]={float(x)/width,float(y)/height,float(std::min(cell,int(extent.cx)+1)),float(std::min(cell,int(extent.cy))),float(extent.cx)};
        }
        bool ok=SUCCEEDED(device->CreateTexture(width,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,texture.GetAddressOf(),nullptr));
        D3DLOCKED_RECT locked{};
        if(ok) {
            ok=SUCCEEDED(texture->LockRect(0,&locked,nullptr,0));
            if(ok) {
                auto source=static_cast<DWORD*>(bits);
                for(int y=0;y<height;++y) {auto destination=reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch);for(int x=0;x<width;++x)destination[x]=((source[y*width+x]&255)<<24)|0xffffff;}
                texture->UnlockRect(0);
            }
        }
        SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);
        if(!ok)texture.Reset();return ok;
    }
    void Draw(IDirect3DDevice9* device,float x,float y,const std::string& utf8,DWORD color,float right) const {
        if(!texture)return;
        int count=MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),nullptr,0);
        std::wstring text(size_t(count),L' ');MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),text.data(),count);
        std::vector<Vertex> vertices;vertices.reserve(text.size()*6);
        D3DSURFACE_DESC desc{};texture->GetLevelDesc(0,&desc);
        for(wchar_t c:text) {
            size_t i=c>=32&&c<128?size_t(c-32):c>=0x400&&c<0x500?96+size_t(c-0x400):size_t('?' -32);
            const auto& g=glyphs[i];if(x+g.w>right)break;
            float r=g.u+g.w/desc.Width,b=g.v+g.h/desc.Height;
            Vertex a{x-.5f,y-.5f,0,1,color,g.u,g.v},bb{x+g.w-.5f,y-.5f,0,1,color,r,g.v},cc{x-.5f,y+g.h-.5f,0,1,color,g.u,b},dd{x+g.w-.5f,y+g.h-.5f,0,1,color,r,b};
            vertices.insert(vertices.end(),{a,bb,cc,cc,bb,dd});x+=g.advance;
        }
        if(vertices.empty())return;
        device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);device->SetTexture(0,texture.Get());
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(Vertex));
    }
};
}
