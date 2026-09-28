#pragma once
#include <vector>
#include "../src/D3D9/ScopedRenderState.h"

// Compile and draw the exact shipped field shader at deterministic times.
// This checks animation/anchoring/wake independently of raymarch and history.
void TestLocalFogField(IDirect3DDevice9* d)
{
 ScopedRenderState saved(d);
 std::ifstream input("src/Effects/DirectionalVolumetricLighting.cpp");
 std::string text((std::istreambuf_iterator<char>(input)),{});
 const std::string marker="const char* kLocalFogFieldSource = R\"HLSL(";
 auto begin=text.find(marker);Require(begin!=std::string::npos,"field shader source found");begin+=marker.size();
 auto source=text.substr(begin,text.find(")HLSL\";",begin)-begin);
 ComPtr<ID3DBlob> code,errors,assembly;ComPtr<IDirect3DPixelShader9> shader;
 Check(D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.GetAddressOf(),errors.GetAddressOf()));
 Check(d->CreatePixelShader(static_cast<DWORD*>(code->GetBufferPointer()),shader.GetAddressOf()));
 Check(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,assembly.GetAddressOf()));
 std::string listing(static_cast<char*>(assembly->GetBufferPointer()),assembly->GetBufferSize());
 auto slotsAt=listing.find("approximately ");Require(slotsAt!=std::string::npos,"field instruction count available");
 Require(std::stoi(listing.substr(slotsAt+14))<=512,"field stays within 512 ps_3_0 slots");
 ComPtr<IDirect3DTexture9> noise,state,target;ComPtr<IDirect3DSurface9> surface,read;
 Check(d->CreateTexture(128,128,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,noise.GetAddressOf(),nullptr));
 Check(d->CreateTexture(2,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,state.GetAddressOf(),nullptr));
 Check(d->CreateTexture(128,128,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,target.GetAddressOf(),nullptr));
 Check(target->GetSurfaceLevel(0,surface.GetAddressOf()));
 Check(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,read.GetAddressOf(),nullptr));
 D3DLOCKED_RECT lock{};Check(noise->LockRect(0,&lock,nullptr,0));
 for(int y=0;y<128;++y)for(int x=0;x<128;++x){
  float a=6.2831853f*x/128,b=6.2831853f*y/128;
  DWORD r=DWORD(128+55*sinf(a)*cosf(b)+35*sinf(3*a+2*b));
  DWORD g=DWORD(128+55*cosf(2*a-b)+35*sinf(a+3*b));
  reinterpret_cast<DWORD*>(static_cast<BYTE*>(lock.pBits)+y*lock.Pitch)[x]=0xff000000|(r<<16)|(g<<8);
 }Check(noise->UnlockRect(0));
 auto draw=[&](float time,float wake,float strength,float originX=0.f){
  float actor[8]={0,0,0,wake,4,0,0,0};Check(state->LockRect(0,&lock,nullptr,0));memcpy(lock.pBits,actor,sizeof(actor));Check(state->UnlockRect(0));
  Check(d->SetDepthStencilSurface(nullptr));Check(d->SetRenderTarget(0,surface.Get()));
  D3DVIEWPORT9 vp{0,0,128,128,0,1};Check(d->SetViewport(&vp));
  for(auto rs:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE})Check(d->SetRenderState(rs,FALSE));
  Check(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
  Check(d->SetTexture(0,noise.Get()));Check(d->SetTexture(1,state.Get()));
  for(DWORD s=0;s<2;++s){
   Check(d->SetSamplerState(s,D3DSAMP_MINFILTER,s?D3DTEXF_POINT:D3DTEXF_LINEAR));Check(d->SetSamplerState(s,D3DSAMP_MAGFILTER,s?D3DTEXF_POINT:D3DTEXF_LINEAR));
   Check(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));Check(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));
   Check(d->SetSamplerState(s,D3DSAMP_ADDRESSU,s?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP));Check(d->SetSamplerState(s,D3DSAMP_ADDRESSV,s?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP));
  }
  float constants[2][4]={{-64+originX,-64,128,time},{5,18,strength,0}};
  Check(d->SetPixelShaderConstantF(0,constants[0],2));Check(d->SetPixelShader(shader.Get()));Check(d->SetVertexShader(nullptr));
  struct V{float x,y,z,w,u,v;};V q[]={{-.5f,-.5f,0,1,0,0},{127.5f,-.5f,0,1,1,0},{-.5f,127.5f,0,1,0,1},{127.5f,127.5f,0,1,1,1}};
  Check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));Check(d->BeginScene());Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V)));Check(d->EndScene());
  Check(d->GetRenderTargetData(surface.Get(),read.Get()));Check(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
  std::vector<DWORD> pixels(128*128);for(int y=0;y<128;++y)memcpy(pixels.data()+128*y,static_cast<BYTE*>(lock.pBits)+y*lock.Pitch,128*4);Check(read->UnlockRect());return pixels;
 };
 auto base=draw(0,0,0),later=draw(12,0,0),repeat=draw(0,0,0),moved=draw(0,0,0,8),wake=draw(0,1,1),disabled=draw(0,1,0);
 size_t evolved=0,disturbed=0,escaped=0,anchorError=0;int minBase=255,maxBase=0;
 for(int y=0;y<128;++y)for(int x=0;x<128;++x){int i=y*128+x;
  evolved+=abs(int((base[i]>>16)&255)-int((later[i]>>16)&255))>5;
  disturbed+=base[i]!=wake[i];
  if(abs(x-64)>22||abs(y-64)>12)escaped+=base[i]!=wake[i];
  if(x<120)anchorError+=base[i+8]!=moved[i];
  minBase=std::min(minBase,int((base[i]>>16)&255));maxBase=std::max(maxBase,int((base[i]>>16)&255));
 }
 Require(maxBase-minBase>100,"field has dense banks and clear gaps");
 Require(evolved>1000,"stationary field evolves with zero wake");
 Require(base==repeat,"field is deterministic without frame-random flicker");
 Require(anchorError==0,"moving field patch preserves world-space density");
 Require(disturbed>20&&escaped==0,"wake disturbs only the actor neighborhood");
 Require(base==disabled,"zero wake strength preserves autonomous flow");
 // Correlation after ANY modest XY shift stays imperfect: evolution is not
 // a rigid scroll of one planar texture.
 double bestError=1e9;
 for(int dy=-12;dy<=12;++dy)for(int dx=-12;dx<=12;++dx){double error=0;
  for(int y=16;y<112;y+=2)for(int x=16;x<112;x+=2)
   error+=abs(int((base[(y+dy)*128+x+dx]>>16)&255)-int((later[y*128+x]>>16)&255));
  bestError=std::min(bestError,error/(48*48));
 }
 Require(bestError>3,"time evolution changes shape beyond planar translation");
}
