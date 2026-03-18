#pragma once

#include "DXUtils.hpp"

#include <plugin.h>
#include <wrl/client.h>

namespace SSO
{
    enum class Style { BLUR, SOLID };

    class Manager final
    {
    private:
        struct OutlineEnt final {
            CEntity *ent{};
            CRGBA    clr{};
        };

        struct OutlineObj final {
            RwObject *obj{};
            CRGBA     clr{};
            RwMatrix  mtx{};
            bool      has_mtx{};
        };

    private:
        Microsoft::WRL::ComPtr<IDirect3DStateBlock9>  m_state_block{};
        Microsoft::WRL::ComPtr<IDirect3DPixelShader9> m_ps_solid{};
        Microsoft::WRL::ComPtr<IDirect3DPixelShader9> m_ps_blur{};
        Microsoft::WRL::ComPtr<IDirect3DPixelShader9> m_ps_outline{};
        Microsoft::WRL::ComPtr<IDirect3DTexture9>     m_rt_tex_base{};
        Microsoft::WRL::ComPtr<IDirect3DTexture9>     m_rt_tex_blur{};
        Microsoft::WRL::ComPtr<IDirect3DSurface9>     m_msaa_surf_base{};
        Microsoft::WRL::ComPtr<IDirect3DSurface9>     m_msaa_surf_stencil{};
        Microsoft::WRL::ComPtr<IDirect3DSurface9>     m_res_surf_base{};
        Microsoft::WRL::ComPtr<IDirect3DSurface9>     m_surf_blur{};

    private:
        Style                   m_style{};
        bool                    m_initialized{};
        DXUtils::Buffer         m_skin_buf{};
        std::vector<OutlineEnt> m_ents{};
        std::vector<OutlineObj> m_objs{};

    public:
        bool Initialize(const Style style = Style::BLUR);
        void Shutdown();
        void Render();
        void AddEntity(CEntity *const ent, const CRGBA &clr);

    private:
        void AddObject(RwObject *const obj, const CRGBA &clr, const RwMatrix *const mtx = nullptr);
        void AddPedWeapons(CPed *const ped, const CRGBA &clr);
    };
}