#pragma once

#include "DXUtils.hpp"

#include <plugin.h>

namespace SSO::RWUtils
{
    void RenderEntity(IDirect3DDevice9 *const device, CEntity *const entity, DXUtils::Buffer &skin_buf);
    void RenderObject(IDirect3DDevice9 *const device, RwObject *const obj, DXUtils::Buffer &skin_buf,
        const eEntityType type = ENTITY_TYPE_NOTHING, const RwMatrix *const mtx_override = nullptr);
}