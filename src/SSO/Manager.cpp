#include "Manager.hpp"
#include "RWUtils.hpp"
#include "Shaders.hpp"

#include <d3dx9.h>
#include <ePedBones.h>

using Microsoft::WRL::ComPtr;

bool SSO::Manager::Initialize(const Style style)
{
    if (m_initialized) {
        return true;
    }

    IDirect3DDevice9 *const device{ reinterpret_cast<IDirect3DDevice9 *>(GetD3DDevice()) };

    if (!device) {
        return false;
    }

    m_style = style;

    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &m_state_block))) {
        return false;
    }

    if (FAILED(DXUtils::CompilePixelShader(device, Shaders::ps_solid, &m_ps_solid))) {
        return false;
    }

    switch (m_style)
    {
        case Style::BLUR:
        {
            if (FAILED(DXUtils::CompilePixelShader(device, Shaders::ps_blur_outline, &m_ps_blur))) {
                return false;
            }

            break;
        }

        case Style::SOLID:
        {
            if (FAILED(DXUtils::CompilePixelShader(device, Shaders::ps_solid_outline, &m_ps_outline))) {
                return false;
            }

            break;
        }
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

    if (m_style == Style::BLUR)
    {
        if (FAILED(device->CreateTexture(
            desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_rt_tex_blur, nullptr))) {
            return false;
        }

        if (FAILED(m_rt_tex_blur->GetSurfaceLevel(0, &m_surf_blur))) {
            return false;
        }
    }

    m_initialized = true;

    return true;
}

void SSO::Manager::Shutdown()
{
    m_state_block.Reset();
    m_ps_solid.Reset();
    m_ps_blur.Reset();
    m_ps_outline.Reset();
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

            RWUtils::RenderObject(device, obj.obj, m_skin_buf, ENTITY_TYPE_NOTHING, obj.has_mtx ? &obj.mtx : nullptr);
        }

        m_objs.clear();

        device->StretchRect(m_msaa_surf_base.Get(), nullptr, m_res_surf_base.Get(), nullptr, D3DTEXF_NONE);
    }

    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    if (m_style == Style::BLUR)
    {
        const float dir_h[4]{ 1.0f / rt_w, 0, 0, 0 };
        const float p_h[4]{ 1.0f, 0.0f, 0, 0 };

        const float dir_v[4]{ 0, 1.0f / rt_h, 0, 0 };
        const float p_v[4]{ 2.0f, 1.0f, 0, 0 };

        device->SetPixelShader(m_ps_blur.Get());

        device->SetRenderTarget(0, m_surf_blur.Get());
        {
            device->SetDepthStencilSurface(nullptr);

            device->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.0f, 0);

            device->SetTexture(0, m_rt_tex_base.Get());

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
    }

    else
    {
        const float texel[4]{ 1.0f / rt_w, 1.0f / rt_h, 0.0f, 0.0f };
        const float params[4]{ 2.0f, 0.0f, 0.0f, 0.0f };

        device->SetPixelShader(m_ps_outline.Get());

        device->SetRenderTarget(0, rt.Get());
        {
            device->SetDepthStencilSurface(m_msaa_surf_stencil.Get());

            device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
            device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
            device->SetRenderState(D3DRS_STENCILREF, 0);

            device->SetTexture(0, m_rt_tex_base.Get());

            device->SetPixelShaderConstantF(1, texel, 1);
            device->SetPixelShaderConstantF(2, params, 1);

            device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

            DXUtils::DrawQuad(device, rt_w, rt_h);
        }
    }

    device->SetDepthStencilSurface(ds.Get());
}

void SSO::Manager::AddEntity(CEntity *const ent, const CRGBA &clr)
{
    if (!ent || clr.a == 0) {
        return;
    }
    
    m_ents.emplace_back(OutlineEnt{ ent, clr });

    if (ent->m_nType == ENTITY_TYPE_PED) {
        AddPedWeapons(static_cast<CPed *>(ent), clr);
    }
}

void SSO::Manager::AddObject(RwObject *const obj, const CRGBA &clr, const RwMatrix *const mtx)
{
    if (!obj || clr.a == 0) {
        return;
    }

    OutlineObj entry{ obj, clr, {}, mtx != nullptr };

    if (mtx) {
        entry.mtx = *mtx;
    }

    m_objs.emplace_back(entry);
}

void SSO::Manager::AddPedWeapons(CPed *const ped, const CRGBA &clr)
{
    if (!ped || clr.a == 0) {
        return;
    }

    RwObject *const weapon{ ped->m_pWeaponObject };

    if (!weapon) {
        return;
    }

    RpClump *const wep_clump{ reinterpret_cast<RpClump *>(weapon) };

    if (!wep_clump) {
        return;
    }

    RwFrame *const wep_frame{ RpClumpGetFrame(wep_clump) };

    if (!wep_frame) {
        return;
    }

    const RwMatrix cur_ltm{ *RwFrameGetLTM(wep_frame) };

    const CWeapon &active{ ped->m_aWeapons[ped->m_nSelectedWepSlot] };

    CWeaponInfo *const info{ CWeaponInfo::GetWeaponInfo(active.m_eWeaponType, ped->GetWeaponSkill()) };

    if (!info) {
        return;
    }

    if (info && info->m_nFlags.bTwinPistol)
    {
        AddObject(weapon, clr, &cur_ltm);

        if (RpHAnimHierarchy *const hier{ GetAnimHierarchyFromSkinClump(ped->m_pRwClump) }) {
            const int bone_id{ active.m_eWeaponType != WEAPONTYPE_PARACHUTE ? BONE_RIGHTWRIST : BONE_SPINE1 };
            RwMatrix *const hand{ &RpHAnimHierarchyGetMatrixArray(hier)[RpHAnimIDGetIndex(hier, bone_id)] };
            AddObject(weapon, clr, hand);
        }
    }

    else {
        AddObject(weapon, clr, &cur_ltm);
    }
}