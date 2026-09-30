#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../TuningOverlay.h"
using Microsoft::WRL::ComPtr;
void Check(HRESULT h) {if(FAILED(h))throw std::runtime_error("D3D9 error "+std::to_string(h));}
int main() try {
 wchar_t path[MAX_PATH]{};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\d3d9.dll");auto dll=LoadLibraryW(path);
 auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(dll,"Direct3DCreate9"));ComPtr<IDirect3D9> api;api.Attach(create(D3D_SDK_VERSION));
 HWND window=CreateWindowW(L"STATIC",L"Overlay regression",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 std::filesystem::create_directories("build/overlay-preview");
 for(auto height:{1080,1440,2160}) {
  int width=height*16/9;D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=width;pp.BackBufferHeight=height;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
  ComPtr<IDirect3DDevice9> device;Check(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,device.GetAddressOf()));
  tuningoverlay::visible=true;
  for(int page=0;page<1;++page) {
   Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff354052,1,0));device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW);Check(device->BeginScene());tuningoverlay::Draw(device.Get());
   if(tuningoverlay::PanelY()+tuningoverlay::panelHeight>height)throw std::runtime_error("Panel clipped vertically");
   for(int index=0;index<int(tuningoverlay::items.size());++index)if(tuningoverlay::items[index].kind!=tuningoverlay::KIND_HEADER){
    int column=tuningoverlay::ItemColumn(index),row=index-tuningoverlay::ColumnStarts()[column];
    float px=tuningoverlay::PanelX()+(column*tuningoverlay::columnWidth+40)*tuningoverlay::uiScale;
    float py=tuningoverlay::ControlsY()+(row+.5f)*tuningoverlay::controlRow*tuningoverlay::uiScale;
    if(tuningoverlay::HitItem(px,py)!=index)throw std::runtime_error("Control cannot be reached by mouse");
   }
   DWORD restored=0;device->GetRenderState(D3DRS_CULLMODE,&restored);if(restored!=D3DCULL_CW)throw std::runtime_error("Overlay leaks state");
   device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
   tuningoverlay::font.Draw(device.Get(),1120*tuningoverlay::uiScale,80*tuningoverlay::uiScale,"1234567890  0 O  1 I l  5 S  8 B",0xffffffff,float(width));
   tuningoverlay::font.Draw(device.Get(),1120*tuningoverlay::uiScale,110*tuningoverlay::uiScale,"MapID ZoneID AreaID Fog Density",0xffffffff,float(width));
   tuningoverlay::font.Draw(device.Get(),1120*tuningoverlay::uiScale,140*tuningoverlay::uiScale,"\xd0\x9b\xd0\xbe\xd0\xba\xd0\xb0\xd1\x86\xd0\xb8\xd1\x8f / Environment",0xffffffff,float(width));
   Check(device->EndScene());DWORD cull;device->GetRenderState(D3DRS_CULLMODE,&cull);if(cull!=D3DCULL_NONE)throw std::runtime_error("Unexpected sample state");
   ComPtr<IDirect3DSurface9> back,read;Check(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.GetAddressOf()));Check(device->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,read.GetAddressOf(),nullptr));Check(device->GetRenderTargetData(back.Get(),read.Get()));
   D3DLOCKED_RECT locked{};Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));std::ofstream output("build/overlay-preview/"+std::to_string(height)+"-"+std::to_string(page)+".ppm",std::ios::binary);output<<"P6\n"<<width<<' '<<height<<"\n255\n";
   for(int y=0;y<height;++y)for(int x=0;x<width;++x){DWORD pixel=reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x];char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};output.write(rgb,3);}read->UnlockRect();
  }
  tuningoverlay::font.Reset();Check(device->Reset(&pp));if(!tuningoverlay::font.Ensure(device.Get(),16))throw std::runtime_error("Font atlas reset failed");tuningoverlay::font.Reset();
  std::cout<<"PASS overlay "<<width<<'x'<<height<<" all controls, state restoration, font reset\n";
 }
 DestroyWindow(window);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
