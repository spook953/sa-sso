#include "Manager.hpp"
#include "RWUtils.hpp"
#include "Shaders.hpp"

#include <d3dx9.h>

using Microsoft::WRL::ComPtr;

bool SSO::Manager::Initialize()
{
    if (m_initialized) {
        return true;
    }

    IDirect3DDevice9 *const device{ reinterpret_cast<IDirect3DDevice9 *>(GetD3DDevice()) };

    if (!device) {
        return false;
    }

    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &m_state_block))) {
        return false;
    }

    if (FAILED(DXUtils::CompilePixelShader(device, Shaders::ps_solid, &m_ps_solid))) {
        return false;
    }

    if (FAILED(DXUtils::CompilePixelShader(device, Shaders::ps_blur, &m_ps_blur))) {
        return false;
    }

    ComPtr<IDirect3DSurface9> rt{};

    if (FAILED(device->GetRenderTarget(0, rt.GetAddressOf()))) {
        return false;
    }

    D3DSURFACE_DESC desc{};

    if (FAILED(rt->GetDesc(&desc))) {
        return false;
    }

    if (FAILED(device->CreateRenderTarget(
        desc.Width, desc.Height, D3DFMT_A8R8G8B8, desc.MultiSampleType, desc.MultiSampleQuality, FALSE, &m_msaa_surf_base, nullptr))) {
        return false;
    }

    if (FAILED(device->CreateDepthStencilSurface(
        desc.Width, desc.Height, D3DFMT_D24S8, desc.MultiSampleType, desc.MultiSampleQuality, TRUE, &m_msaa_surf_stencil, nullptr))) {
        return false;
    }

    if (FAILED(device->CreateTexture(
        desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_rt_tex_base, nullptr))) {
        return false;
    }

    if (FAILED(m_rt_tex_base->GetSurfaceLevel(0, &m_res_surf_base))) {
        return false;
    }

    if (FAILED(device->CreateTexture(
        desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_rt_tex_blur, nullptr))) {
        return false;
    }

    if (FAILED(m_rt_tex_blur->GetSurfaceLevel(0, &m_surf_blur))) {
        return false;
    }

    m_initialized = true;

    return true;
}

void SSO::Manager::Shutdown()
{
    m_state_block.Reset();
    m_ps_solid.Reset();
    m_ps_blur.Reset();
    m_rt_tex_base.Reset();
    m_rt_tex_blur.Reset();
    m_msaa_surf_base.Reset();
    m_msaa_surf_stencil.Reset();
    m_res_surf_base.Reset();
    m_surf_blur.Reset();

    m_skin_buf.Release();

    m_ents.clear();
    m_objs.clear();

    m_initialized = false;
}

void SSO::Manager::Render()
{
    if (!m_initialized || (m_ents.empty() && m_objs.empty())) {
        return;
    }

    IDirect3DDevice9 *const device{ reinterpret_cast<IDirect3DDevice9 *>(GetD3DDevice()) };

    if (!device) {
        return;
    }

    const DXUtils::StateBlockGuard sbg{ m_state_block.Get() };

    ComPtr<IDirect3DSurface9> rt{};
    ComPtr<IDirect3DSurface9> ds{};

    if (FAILED(device->GetRenderTarget(0, rt.GetAddressOf()))) {
        return;
    }

    device->GetDepthStencilSurface(ds.GetAddressOf());

    D3DSURFACE_DESC desc{};

    if (FAILED(rt->GetDesc(&desc))) {
        return;
    }

    const float rt_w{ static_cast<float>(desc.Width) };
    const float rt_h{ static_cast<float>(desc.Height) };

    const float dir_h[4]{ 1.0f / rt_w, 0, 0, 0 };
    const float p_h[4]{ 1.0f, 0.0f, 0, 0 };

    const float dir_v[4]{ 0, 1.0f / rt_h, 0, 0 };
    const float p_v[4]{ 2.0f, 1.0f, 0, 0 };

    const D3DVIEWPORT9 vp{
        .X = 0,
        .Y = 0,
        .Width = static_cast<DWORD>(rt_w),
        .Height = static_cast<DWORD>(rt_h),
        .MinZ = 0.0f,
        .MaxZ = 1.0f
    };

    device->SetViewport(&vp);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

    device->SetRenderTarget(0, m_msaa_surf_base.Get());
    {
        device->SetDepthStencilSurface(m_msaa_surf_stencil.Get());

        device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);

        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
        device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
        device->SetRenderState(D3DRS_STENCILREF, 1);

        device->SetPixelShader(m_ps_solid.Get());

        for (const OutlineEnt &ent : m_ents)
        {
            const D3DXVECTOR4 clr{
                static_cast<float>(ent.clr.r) / 255.0f,
                static_cast<float>(ent.clr.g) / 255.0f,
                static_cast<float>(ent.clr.b) / 255.0f,
                static_cast<float>(ent.clr.a) / 255.0f
            };

            device->SetPixelShaderConstantF(0, const_cast<float *>(&clr.x), 1);

            RWUtils::RenderEntity(device, ent.ent, m_skin_buf);
        }

        m_ents.clear();

        for (const OutlineObj &obj : m_objs)
        {
            const D3DXVECTOR4 clr{
                static_cast<float>(obj.clr.r) / 255.0f,
                static_cast<float>(obj.clr.g) / 255.0f,
                static_cast<float>(obj.clr.b) / 255.0f,
                static_cast<float>(obj.clr.a) / 255.0f
            };

            device->SetPixelShaderConstantF(0, const_cast<float *>(&clr.x), 1);

            RWUtils::RenderObject(device, obj.obj, m_skin_buf);
        }

        m_objs.clear();

        device->StretchRect(m_msaa_surf_base.Get(), nullptr, m_res_surf_base.Get(), nullptr, D3DTEXF_NONE);
    }

    device->SetPixelShader(m_ps_blur.Get());

    device->SetRenderTarget(0, m_surf_blur.Get());
    {
        device->SetDepthStencilSurface(nullptr);

        device->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.0f, 0);

        device->SetTexture(0, m_rt_tex_base.Get());

        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

        device->SetPixelShaderConstantF(1, dir_h, 1);
        device->SetPixelShaderConstantF(2, p_h, 1);

        DXUtils::DrawQuad(device, rt_w, rt_h);
    }

    device->SetRenderTarget(0, rt.Get());
    {
        device->SetDepthStencilSurface(m_msaa_surf_stencil.Get());

        device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
        device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
        device->SetRenderState(D3DRS_STENCILREF, 0);

        device->SetTexture(0, m_rt_tex_blur.Get());

        device->SetPixelShaderConstantF(1, dir_v, 1);
        device->SetPixelShaderConstantF(2, p_v, 1);

        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

        DXUtils::DrawQuad(device, rt_w, rt_h);
    }

    device->SetDepthStencilSurface(ds.Get());
}

void SSO::Manager::AddEntity(CEntity *const ent, const CRGBA &clr)
{
    if (!ent || clr.a == 0) {
        return;
    }
    
    m_ents.emplace_back(OutlineEnt{ ent, clr });
}

void SSO::Manager::AddObject(RwObject *const obj, const CRGBA &clr)
{
    if (!obj || clr.a == 0) {
        return;
    }

    m_objs.emplace_back(OutlineObj{ obj, clr });
}