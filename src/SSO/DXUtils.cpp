#include "DXUtils.hpp"

#include <d3dx9.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

HRESULT SSO::DXUtils::CompilePixelShader(IDirect3DDevice9 *const device, const char *const ps_src, IDirect3DPixelShader9 **const shader)
{
    if (!shader) {
        return E_POINTER;
    }

    *shader = nullptr;

    if (!device || !ps_src) {
        return E_INVALIDARG;
    }

    ComPtr<ID3DXBuffer> shader_code{};
    ComPtr<ID3DXBuffer> error_msgs{};

    const HRESULT hr{ D3DXCompileShader(
        ps_src,
        static_cast<UINT>(strlen(ps_src)),
        nullptr,
        nullptr,
        "main",
        "ps_2_0",
        0,
        shader_code.GetAddressOf(),
        error_msgs.GetAddressOf(),
        nullptr
    ) };

    if (FAILED(hr)) {
        return hr;
    }

    return device->CreatePixelShader(reinterpret_cast<DWORD *>(shader_code->GetBufferPointer()), shader);
}

void SSO::DXUtils::DrawQuad(IDirect3DDevice9 *const device, const float w, const float h)
{
    if (!device) {
        return;
    }

    struct Vertex final
    {
        float x{};
        float y{};
        float z{};
        float r{};
        float u{};
        float v{};
    };

    const Vertex verts[4]
    {
        { -0.5f, h - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
        { w - 0.5f, h - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
        { w - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f }
    };

    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(Vertex));
}

SSO::DXUtils::StateBlockGuard::StateBlockGuard(IDirect3DStateBlock9 *const sb)
    : m_state_block{ sb }
{
    // this alone will likely not suffice
    // todo : manually store/restore states alongside this?
    // edit : seems fine

    if (m_state_block) {
        m_state_block->Capture();
    }
}

SSO::DXUtils::StateBlockGuard::~StateBlockGuard()
{
    if (m_state_block) {
        m_state_block->Apply();
    }
}

bool SSO::DXUtils::Buffer::Init(IDirect3DDevice9 *const device, const UINT bytes)
{
    if (m_vb && m_size >= bytes) {
        return true;
    }

    Release();

    if (FAILED(device->CreateVertexBuffer(bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &m_vb, nullptr))) {
        return false;
    }

    m_size = bytes;

    return true;
}

void SSO::DXUtils::Buffer::Release()
{
    if (m_vb) {
        m_vb->Release();
        m_vb = nullptr;
    }

    m_size = 0;
}