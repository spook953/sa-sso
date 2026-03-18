#pragma once

#include <d3d9.h>

namespace SSO::DXUtils
{
    HRESULT CompilePixelShader(IDirect3DDevice9 *const device, const char *const ps_src, IDirect3DPixelShader9 **const shader);
    void    DrawQuad(IDirect3DDevice9 *const device, const float w, const float h);

    class StateBlockGuard final
    {
    private:
        IDirect3DStateBlock9 *m_state_block{};

    public:
        StateBlockGuard(IDirect3DStateBlock9 *const sb);
        ~StateBlockGuard();
    };

    class Buffer final
    {
    private:
        IDirect3DVertexBuffer9 *m_vb{};
        UINT                    m_size{};

    public:
        Buffer() = default;
        ~Buffer() { Release(); }

    public:
        Buffer(const Buffer &)            = delete;
        Buffer &operator=(const Buffer &) = delete;

    public:
        bool Init(IDirect3DDevice9 *const device, const UINT bytes);
        void Release();

    public:
        IDirect3DVertexBuffer9 *Get() const { return m_vb; }
    };
}