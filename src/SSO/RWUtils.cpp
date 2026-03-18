#include "RWUtils.hpp"

#include <CVisibilityPlugins.h>

namespace SSO::RWUtils
{
    struct RenderCtx final {
        IDirect3DDevice9 *device{};
        DXUtils::Buffer *skin_buf{};
        eEntityType type{};
    };

    bool      ShouldRenderAtomic(RpAtomic *const atomic, const RenderCtx *const ctx);
    void      RenderUnskinnedAtomic(IDirect3DDevice9 *const device, RpAtomic *const atomic);
    void      RenderSkinnedAtomic(IDirect3DDevice9 *const device, RpAtomic *const atomic, DXUtils::Buffer &skin_buf);
    RpAtomic *RenderAtomicCB(RpAtomic *const atomic, void *const data);
}

bool SSO::RWUtils::ShouldRenderAtomic(RpAtomic *const atomic, const RenderCtx *const ctx)
{
    if (!atomic || !ctx || !(RpAtomicGetFlags(atomic) & rpATOMICRENDER)) {
        return false;
    }

    if (ctx->type == ENTITY_TYPE_VEHICLE)
    {
        const uintptr_t rcb{ reinterpret_cast<uintptr_t>(atomic->renderCallBack) };

        if (!rcb) {
            return false;
        }

        // todo : regular planes have a propeller that should be filtered out
        static constexpr uintptr_t heli_rotor_cbs[]{ 0x7340B0, 0x734170 };

        if (std::ranges::contains(heli_rotor_cbs, rcb)) {
            return false;
        }

        static constexpr uintptr_t lo_cbs[]{ 0x7331E0, 0x732820, 0x7334F0 };
        static constexpr uintptr_t hi_cbs[]{ 0x733240, 0x733F80, 0x733420, 0x734370, 0x733550, 0x7344A0 };

        const bool is_lo{ std::ranges::contains(lo_cbs, rcb) };
        const bool is_hi{ !is_lo && std::ranges::contains(hi_cbs, rcb) };

        if (!is_lo && !is_hi) {
            return true;
        }

        const float dist{ CVisibilityPlugins::gVehicleDistanceFromCamera };
        const float lod{ CVisibilityPlugins::ms_vehicleLod0Dist };

        return is_hi ? (dist < lod) : (dist >= lod);
    }

    return true;
}

void SSO::RWUtils::RenderUnskinnedAtomic(IDirect3DDevice9 *const device, RpAtomic *const atomic)
{
    if (!device || !atomic) {
        return;
    }

    RpGeometry *const geom{ atomic->geometry };

    if (!geom) {
        return;
    }

    RwResEntry *const entry{ geom->repEntry ? geom->repEntry : atomic->repEntry };
    
    if (!entry) {
        return;
    }

    RxD3D9ResEntryHeader *const header{ reinterpret_cast<RxD3D9ResEntryHeader *>(entry + 1) };
    RxD3D9InstanceData *const instances{ reinterpret_cast<RxD3D9InstanceData *>(header + 1) };

    D3DMATRIX world{};

    if (RwMatrix *const ltm{ RwFrameGetLTM(RpAtomicGetFrame(atomic)) }) {
        std::memcpy(&world, ltm, sizeof(D3DMATRIX));
    }

    world._14 = 0.0f;
    world._24 = 0.0f;
    world._34 = 0.0f;
    world._44 = 1.0f;

    device->SetTransform(D3DTS_WORLD, &world);
    device->SetVertexShader(nullptr);
    device->SetVertexDeclaration(reinterpret_cast<IDirect3DVertexDeclaration9 *>(header->vertexDeclaration));

    for (int n{}; n < 2; n++)
    {
        const RxD3D9VertexStream &vs{ header->vertexStream[n] };

        device->SetStreamSource(
            n,
            reinterpret_cast<IDirect3DVertexBuffer9 *>(vs.vertexBuffer),
            header->useOffsets ? vs.offset : 0,
            vs.stride
        );
    }

    device->SetIndices(reinterpret_cast<IDirect3DIndexBuffer9 *>(header->indexBuffer));

    for (RwUInt32 n{}; n < header->numMeshes; n++)
    {
        device->DrawIndexedPrimitive(
            static_cast<D3DPRIMITIVETYPE>(header->primType),
            instances[n].baseIndex,
            instances[n].minVert,
            instances[n].numVertices,
            instances[n].startIndex,
            instances[n].numPrimitives
        );
    }
}

void SSO::RWUtils::RenderSkinnedAtomic(IDirect3DDevice9 *const device, RpAtomic *const atomic, SSO::DXUtils::Buffer &skin_buf)
{
    static constexpr int MAX_BONES{ 128 };

    if (!device || !atomic) {
        return;
    }

    RpGeometry *const geom{ atomic->geometry };

    if (!geom) {
        return;
    }

    RpSkin *const skin{ RpSkinGeometryGetSkin(geom) };

    if (!skin) {
        return;
    }

    RpHAnimHierarchy *const hier{ RpSkinAtomicGetHAnimHierarchy(atomic) };

    if (!hier || !hier->pMatrixArray) {
        return;
    }

    RwResEntry *const entry{ geom->repEntry ? geom->repEntry : atomic->repEntry };

    if (!entry) {
        return;
    }

    RxD3D9ResEntryHeader *const header{ reinterpret_cast<RxD3D9ResEntryHeader *>(entry + 1) };
    RxD3D9InstanceData *const instances{ reinterpret_cast<RxD3D9InstanceData *>(header + 1) };

    const RwUInt32 stride{ header->vertexStream[0].stride };
    const RwUInt32 num_verts{ header->totalNumVertex };
    const RwUInt32 vb_bytes{ stride * num_verts };

    if (!stride || !num_verts || !header->numMeshes) {
        return;
    }

    IDirect3DVertexBuffer9 *const og_vb{ reinterpret_cast<IDirect3DVertexBuffer9 *>(header->vertexStream[0].vertexBuffer) };

    if (!og_vb) {
        return;
    }

    RwMatrix *const bone_matrices{ RpHAnimHierarchyGetMatrixArray(hier) };
    const RwMatrix *const skin_to_bone{ RpSkinGetSkinToBoneMatrices(skin) };

    const RwMatrixWeights *const weights{ RpSkinGetVertexBoneWeights(skin) };
    const RwUInt32 *const bone_indices{ RpSkinGetVertexBoneIndices(skin) };

    const int num_bones{ std::min({ static_cast<int>(RpSkinGetNumBones(skin)), hier->numNodes, MAX_BONES }) };

    if (num_bones <= 0 || !bone_matrices || !skin_to_bone || !weights || !bone_indices) {
        return;
    }

    const RwV3d *const bind_verts{ geom->morphTarget->verts };
    const int geom_num_verts{ geom->numVertices };

    RwMatrix combined[MAX_BONES]{};

    for (int n{}; n < num_bones; n++) {
        RwMatrixMultiply(&combined[n], &skin_to_bone[n], &bone_matrices[n]);
    }

    for (int n{}; n < num_bones; n++)
    {
        const RwMatrix &m{ combined[n] };

        if (m.pos.x != m.pos.x || m.pos.y != m.pos.y || m.pos.z != m.pos.z
            || m.pos.x < -10000.0f || m.pos.x > 10000.0f
            || m.pos.y < -10000.0f || m.pos.y > 10000.0f
            || m.pos.z < -10000.0f || m.pos.z > 10000.0f)
        {
            return;
        }
    }

    RwUInt32 entry_base_vertex{ instances[0].baseIndex };

    for (RwUInt32 n{ 1 }; n < header->numMeshes; n++)
    {
        if (instances[n].baseIndex < entry_base_vertex) {
            entry_base_vertex = instances[n].baseIndex;
        }
    }

    const RwUInt32 vb_src_offset{ entry_base_vertex * stride };

    void *src_data{};

    if (FAILED(og_vb->Lock(vb_src_offset, vb_bytes, &src_data, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK))) {
        return;
    }

    if (!skin_buf.Init(device, vb_bytes)) {
        og_vb->Unlock();
        return;
    }

    void *dst_data{};

    if (FAILED(skin_buf.Get()->Lock(0, vb_bytes, &dst_data, D3DLOCK_DISCARD))) {
        skin_buf.Release();
        og_vb->Unlock();
        return;
    }

    std::memcpy(dst_data, src_data, vb_bytes);

    og_vb->Unlock();

    unsigned char *const dst{ static_cast<unsigned char *>(dst_data) };
    const int vert_count{ std::min(geom_num_verts, static_cast<int>(num_verts)) };

    for (int n{}; n < vert_count; n++)
    {
        const RwV3d &bp{ bind_verts[n] };
        const RwUInt32 packed{ bone_indices[n] };
        const RwMatrixWeights &w{ weights[n] };

        const int idx0{ static_cast<int>(packed & 0xFF) };
        const int idx1{ static_cast<int>((packed >> 8) & 0xFF) };
        const int idx2{ static_cast<int>((packed >> 16) & 0xFF) };
        const int idx3{ static_cast<int>((packed >> 24) & 0xFF) };

        float rx{};
        float ry{};
        float rz{};

        if (w.w0 > 0.0f && idx0 < num_bones) {
            const RwMatrix &m{ combined[idx0] };
            rx += w.w0 * (bp.x * m.right.x + bp.y * m.up.x + bp.z * m.at.x + m.pos.x);
            ry += w.w0 * (bp.x * m.right.y + bp.y * m.up.y + bp.z * m.at.y + m.pos.y);
            rz += w.w0 * (bp.x * m.right.z + bp.y * m.up.z + bp.z * m.at.z + m.pos.z);
        }

        if (w.w1 > 0.0f && idx1 < num_bones) {
            const RwMatrix &m{ combined[idx1] };
            rx += w.w1 * (bp.x * m.right.x + bp.y * m.up.x + bp.z * m.at.x + m.pos.x);
            ry += w.w1 * (bp.x * m.right.y + bp.y * m.up.y + bp.z * m.at.y + m.pos.y);
            rz += w.w1 * (bp.x * m.right.z + bp.y * m.up.z + bp.z * m.at.z + m.pos.z);
        }

        if (w.w2 > 0.0f && idx2 < num_bones) {
            const RwMatrix &m{ combined[idx2] };
            rx += w.w2 * (bp.x * m.right.x + bp.y * m.up.x + bp.z * m.at.x + m.pos.x);
            ry += w.w2 * (bp.x * m.right.y + bp.y * m.up.y + bp.z * m.at.y + m.pos.y);
            rz += w.w2 * (bp.x * m.right.z + bp.y * m.up.z + bp.z * m.at.z + m.pos.z);
        }

        if (w.w3 > 0.0f && idx3 < num_bones) {
            const RwMatrix &m{ combined[idx3] };
            rx += w.w3 * (bp.x * m.right.x + bp.y * m.up.x + bp.z * m.at.x + m.pos.x);
            ry += w.w3 * (bp.x * m.right.y + bp.y * m.up.y + bp.z * m.at.y + m.pos.y);
            rz += w.w3 * (bp.x * m.right.z + bp.y * m.up.z + bp.z * m.at.z + m.pos.z);
        }

        float *const dst_pos{ reinterpret_cast<float *>(dst + stride * n) };

        dst_pos[0] = rx;
        dst_pos[1] = ry;
        dst_pos[2] = rz;
    }

    skin_buf.Get()->Unlock();

    D3DMATRIX world{};

    const bool world_space_bones{
        (hier->flags & rpHANIMHIERARCHYUPDATELTMS) != 0 &&
        (hier->flags & rpHANIMHIERARCHYLOCALSPACEMATRICES) == 0
    };

    if (world_space_bones) {
        world._11 = 1.0f;
        world._22 = 1.0f;
        world._33 = 1.0f;
        world._44 = 1.0f;
    }

    else
    {
        RpClump *const clump{ atomic->clump };
        RwMatrix *const ltm{ clump ? RwFrameGetLTM(RpClumpGetFrame(clump)) : nullptr };

        if (ltm) {
            std::memcpy(&world, ltm, sizeof(D3DMATRIX));
        }

        world._14 = 0.0f;
        world._24 = 0.0f;
        world._34 = 0.0f;
        world._44 = 1.0f;
    }

    device->SetTransform(D3DTS_WORLD, &world);
    device->SetVertexShader(nullptr);
    device->SetVertexDeclaration(reinterpret_cast<IDirect3DVertexDeclaration9 *>(header->vertexDeclaration));

    device->SetStreamSource(0, skin_buf.Get(), 0, stride);
    device->SetStreamSource(1, nullptr, 0, 0);

    device->SetIndices(reinterpret_cast<IDirect3DIndexBuffer9 *>(header->indexBuffer));

    for (RwUInt32 n{}; n < header->numMeshes; n++)
    {
        device->DrawIndexedPrimitive(
            static_cast<D3DPRIMITIVETYPE>(header->primType),
            instances[n].baseIndex - entry_base_vertex,
            instances[n].minVert,
            instances[n].numVertices,
            instances[n].startIndex,
            instances[n].numPrimitives
        );
    }
}

RpAtomic *SSO::RWUtils::RenderAtomicCB(RpAtomic *const atomic, void *const data)
{
    if (!atomic || !data) {
        return nullptr;
    }
    
    const RenderCtx *const ctx{ static_cast<const RenderCtx *>(data) };

    if (!ShouldRenderAtomic(atomic, ctx)) {
        return atomic;
    }

    if (atomic->geometry && RpSkinGeometryGetSkin(atomic->geometry)) {
        RenderSkinnedAtomic(ctx->device, atomic, *ctx->skin_buf);
    }

    else {
        RenderUnskinnedAtomic(ctx->device, atomic);
    }

    return atomic;
}

void SSO::RWUtils::RenderEntity(IDirect3DDevice9 *const device, CEntity *const entity, DXUtils::Buffer &skin_buf)
{
    if (!device || !entity) {
        return;
    }

    RwObject *const obj{ entity->m_pRwObject };

    if (!obj) {
        return;
    }

    if (entity->m_nType == ENTITY_TYPE_VEHICLE && RwObjectGetType(obj) == rpCLUMP) {
        CVisibilityPlugins::SetupVehicleVariables(entity->m_pRwClump);
    }

    RenderObject(device, obj, skin_buf, static_cast<eEntityType>(entity->m_nType));
}

void SSO::RWUtils::RenderObject(IDirect3DDevice9 *const device, RwObject *const obj, DXUtils::Buffer &skin_buf, const eEntityType type, const RwMatrix *const mtx_override)
{
    if (!device || !obj) {
        return;
    }

    RenderCtx ctx{ device, &skin_buf, type };

    if (RwObjectGetType(obj) == rpATOMIC) {
        RenderAtomicCB(reinterpret_cast<RpAtomic *>(obj), &ctx);
    }

    else if (RwObjectGetType(obj) == rpCLUMP)
    {
        RpClump *const clump{ reinterpret_cast<RpClump *>(obj) };

        if (mtx_override)
        {
            RwFrame *const frame{ RpClumpGetFrame(clump) };

            const RwMatrix saved_modelling{ frame->modelling };

            frame->modelling = *mtx_override;
            frame->modelling.flags = 0;

            RwFrameGetLTM(frame);
            RwFrameUpdateObjects(frame);

            RpClumpForAllAtomics(clump, RenderAtomicCB, &ctx);

            frame->modelling = saved_modelling;

            RwFrameUpdateObjects(frame);
        }

        else {
            RpClumpForAllAtomics(clump, RenderAtomicCB, &ctx);
        }
    }
}