#include "stdafx.h"
#pragma hdrstop

#include "ModelPool.h"
#include <xr_ini.h>

#ifndef _EDITOR
#include "../../xr_3da/IGame_Persistent.h"
#include "../../xr_3da/fmesh.h"
#include "fhierrarhyvisual.h"
#include "SkeletonAnimated.h"
#include "fvisual.h"
#include "fprogressive.h"
#include "fskinned.h"
#include "flod.h"
#include "ftreevisual.h"
#include "ParticleGroup.h"
#include "ParticleEffect.h"
#else
#include "fmesh.h"
#include "fvisual.h"
#include "fprogressive.h"
#include "ParticleEffect.h"
#include "ParticleGroup.h"
#include "fskinned.h"
#include "fhierrarhyvisual.h"
#include "SkeletonAnimated.h"
#include "IGame_Persistent.h"
#endif

CInifile* vis_prefetch = nullptr;
bool now_prefetch1 = false;
bool now_prefetch2 = false;

dxRender_Visual* CModelPool::Instance_Create(u32 type)
{
    dxRender_Visual* V = NULL;

    // Check types
    switch (type)
    {
    case MT_NORMAL: // our base visual
        V = xr_new<Fvisual>();
        break;
    case MT_HIERRARHY: V = xr_new<FHierrarhyVisual>(); break;
    case MT_PROGRESSIVE: // dynamic-resolution visual
        V = xr_new<FProgressive>();
        break;
    case MT_SKELETON_ANIM: V = xr_new<CKinematicsAnimated>(); break;
    case MT_SKELETON_RIGID: V = xr_new<CKinematics>(); break;
    case MT_SKELETON_GEOMDEF_PM: V = xr_new<CSkeletonX_PM>(); break;
    case MT_SKELETON_GEOMDEF_ST: V = xr_new<CSkeletonX_ST>(); break;
    case MT_PARTICLE_EFFECT: V = xr_new<PS::CParticleEffect>(); break;
    case MT_PARTICLE_GROUP: V = xr_new<PS::CParticleGroup>(); break;
#ifndef _EDITOR
    case MT_LOD: V = xr_new<FLOD>(); break;
    case MT_TREE_ST: V = xr_new<FTreeVisual_ST>(); break;
    case MT_TREE_PM: V = xr_new<FTreeVisual_PM>(); break;
#endif
    default: FATAL("Unknown visual type"); break;
    }
    R_ASSERT(V);
    V->Type = type;
    return V;
}

dxRender_Visual* CModelPool::Instance_Duplicate(dxRender_Visual* V)
{
    R_ASSERT(V);
    dxRender_Visual* N = Instance_Create(V->Type);
    N->Copy(V);
    N->Spawn();
    // inc ref counter
    for (xr_vector<ModelDef>::iterator I = Models.begin(); I != Models.end(); I++)
        if (I->model == V)
        {
            I->refs++;
            break;
        }
    return N;
}

dxRender_Visual* CModelPool::Instance_Load(const char* N, BOOL allow_register)
{
    dxRender_Visual* V;
    string_path fn;
    string_path name;

    // Add default ext if no ext at all
    if (0 == strext(N))
        strconcat(sizeof(name), name, N, ".ogf");
    else
        xr_strcpy(name, sizeof(name), N);

    // Load data from MESHES or LEVEL
    if (!FS.exist(N))
    {
        if (!FS.exist(fn, "$level$", name))
            if (!FS.exist(fn, "$game_meshes$", name))
            {
#ifdef _EDITOR
                Msg("!Can't find model file '%s'.", name);
                return 0;
#else
                Debug.fatal(DEBUG_INFO, "Can't find model file '%s'.", name);
#endif
            }
    }
    else
    {
        xr_strcpy(fn, N);
    }

    // Actual loading
#ifdef DEBUG
    if (bLogging)
        Msg("- Uncached model loading: %s", fn);
#endif // DEBUG

    IReader* data = FS.r_open(fn);
    ogf_header H;
    data->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
    V = Instance_Create(H.type);
    V->Load(N, data, 0);
    FS.r_close(data);
    g_pGamePersistent->RegisterModel(V);

    // Registration
    if (allow_register)
        Instance_Register(N, V);

    return V;
}

dxRender_Visual* CModelPool::Instance_Load(LPCSTR name, IReader* data, BOOL allow_register)
{
    dxRender_Visual* V;

    ogf_header H;
    data->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
    V = Instance_Create(H.type);
    V->Load(name, data, 0);

    // Registration
    if (allow_register)
        Instance_Register(name, V);
    return V;
}

void CModelPool::Instance_Register(LPCSTR N, dxRender_Visual* V)
{
    // Registration
    ModelDef M;
    M.name = N;
    M.model = V;
    Models.push_back(M);
}

void CModelPool::Destroy()
{
    // Pool
    Pool.clear();

    // Registry
    while (!Registry.empty())
    {
        REGISTRY_IT it = Registry.begin();
        dxRender_Visual* V = (dxRender_Visual*)it->first;
#ifdef DEBUG
        Msg("ModelPool: Destroy object: '%s'", *V->dbg_name);
#endif
        DeleteInternal(V, TRUE);
    }

    // Base/Reference
    xr_vector<ModelDef>::iterator I = Models.begin();
    xr_vector<ModelDef>::iterator E = Models.end();
    for (; I != E; I++)
    {
        I->model->Release();
        xr_delete(I->model);
    }

    Models.clear();

    // cleanup motions container
    g_pMotionsContainer->clean(false);

    if (vis_prefetch)
        vis_prefetch->save_as();
    m_prefetched.clear();
}

CModelPool::CModelPool()
{
    bLogging = TRUE;
    bForceDiscard = FALSE;
    bAllowChildrenDuplicate = TRUE;
    g_pMotionsContainer = xr_new<motions_container>();

    if (!strstr(Core.Params, "-noprefetch") && !vis_prefetch)
    {
        string_path fname;
        FS.update_path(fname, "$app_data_root$", "vis_prefetch.ltx");
        vis_prefetch = xr_new<CInifile>(fname, FALSE);
        process_vis_prefetch();
    }
}

CModelPool::~CModelPool()
{
    Destroy();
    xr_delete(g_pMotionsContainer);

    if (vis_prefetch)
        xr_delete(vis_prefetch);
}

dxRender_Visual* CModelPool::Instance_Find(LPCSTR N)
{
    dxRender_Visual* Model = 0;
    xr_vector<ModelDef>::iterator I;
    for (I = Models.begin(); I != Models.end(); I++)
    {
        if (I->name[0] && (0 == xr_strcmp(*I->name, N)))
        {
            Model = I->model;
            break;
        }
    }
    return Model;
}

dxRender_Visual* CModelPool::Create(const char* name, IReader* data)
{
#ifdef _EDITOR
    if (!name || !name[0])
        return 0;
#endif
    string_path low_name;
    VERIFY(xr_strlen(name) < sizeof(low_name));
    xr_strcpy(low_name, name);
    strlwr(low_name);
    if (strext(low_name))
        *strext(low_name) = 0;
    //	Msg						("-CREATE %s",low_name);

    // 0. Search POOL
    POOL_IT it = Pool.find(low_name);
    if (it != Pool.end())
    {
        // 1. Instance found
        dxRender_Visual* Model = it->second;
        Model->Spawn();
        Pool.erase(it);

        refreshPrefetchModel(low_name);

        return Model;
    }
    else
    {
        // 1. Search for already loaded model (reference, base model)
        dxRender_Visual* Base = Instance_Find(low_name);

        if (0 == Base)
        {
            // 2. If not found
            bAllowChildrenDuplicate = FALSE;
            if (data)
                Base = Instance_Load(low_name, data, TRUE);
            else
                Base = Instance_Load(low_name, TRUE);
            bAllowChildrenDuplicate = TRUE;
#ifdef _EDITOR
            if (!Base)
                return 0;
#endif
        }
        // 3. If found - return (cloned) reference
        dxRender_Visual* Model = Instance_Duplicate(Base);
        Registry.insert(mk_pair(Model, low_name));

        refreshPrefetchModel(low_name);

        return Model;
    }
}

void CModelPool::refreshPrefetchModel(LPCSTR low_name)
{
    refreshPrefetchSect("prefetch", low_name, [](std::string lowName) -> bool {
        shared_str fname;
        return !!FS.exist("$game_meshes$",
                          *fname.sprintf("%s.ogf", lowName.c_str()));
    });
}

void CModelPool::refreshPrefetchSect(
    const std::string sectName, const std::string lowName,
    const std::function<bool(std::string fn)> testFn)
{
    if (now_prefetch2)
        return;

    if (m_prefetched.find(lowName) != m_prefetched.end())
        return;

    if (now_prefetch1)
        m_prefetched.emplace(lowName, true);
    else if (vis_prefetch && testFn && testFn(lowName))
        vis_prefetch->w_float(sectName.c_str(), lowName.c_str(), 1.f);
}

dxRender_Visual* CModelPool::CreateChild(LPCSTR name, IReader* data)
{
    string256 low_name;
    VERIFY(xr_strlen(name) < 256);
    xr_strcpy(low_name, name);
    strlwr(low_name);
    if (strext(low_name))
        *strext(low_name) = 0;

    // 1. Search for already loaded model
    dxRender_Visual* Base = Instance_Find(low_name);
    //.	if (0==Base) Base	 	= Instance_Load(name,data,FALSE);
    if (0 == Base)
    {
        if (data)
            Base = Instance_Load(low_name, data, FALSE);
        else
            Base = Instance_Load(low_name, FALSE);
    }

    dxRender_Visual* Model = bAllowChildrenDuplicate ? Instance_Duplicate(Base) : Base;
    return Model;
}

extern BOOL ENGINE_API g_bRendering;
void CModelPool::DeleteInternal(dxRender_Visual*& V, BOOL bDiscard)
{
    VERIFY(!g_bRendering);
    if (!V)
        return;
    V->Depart();
    if (bDiscard || bForceDiscard)
    {
        Discard(V, TRUE);
    }
    else
    {
        //
        REGISTRY_IT it = Registry.find(V);
        if (it != Registry.end())
        {
            // Registry entry found - move it to pool
            Pool.insert(mk_pair(it->second, V));
        }
        else
        {
            // Registry entry not-found - just special type of visual / particles / etc.
            xr_delete(V);
        }
    }
    V = NULL;
}

void CModelPool::Delete(dxRender_Visual*& V, BOOL bDiscard)
{
    if (NULL == V)
        return;
    if (g_bRendering)
    {
        VERIFY(!bDiscard);
        ModelsToDelete.push_back(V);
    }
    else
    {
        DeleteInternal(V, bDiscard);
    }
    V = NULL;
}

void CModelPool::DeleteQueue()
{
    for (u32 it = 0; it < ModelsToDelete.size(); it++)
        DeleteInternal(ModelsToDelete[it]);
    ModelsToDelete.clear();
}

void CModelPool::Discard(dxRender_Visual*& V, BOOL b_complete)
{
    //
    REGISTRY_IT it = Registry.find(V);
    if (it != Registry.end())
    {
        // Pool - OK

        // Base
        const shared_str& name = it->second;
        xr_vector<ModelDef>::iterator I = Models.begin();
        xr_vector<ModelDef>::iterator I_e = Models.end();

        for (; I != I_e; ++I)
        {
            if (I->name == name)
            {
                if (b_complete || strchr(*name, '#'))
                {
                    VERIFY(I->refs > 0);
                    I->refs--;
                    if (0 == I->refs)
                    {
                        bForceDiscard = TRUE;
                        I->model->Release();
                        xr_delete(I->model);
                        Models.erase(I);
                        bForceDiscard = FALSE;
                    }
                    break;
                }
                else
                {
                    if (I->refs > 0)
                        I->refs--;
                    break;
                }
            }
        }
        // Registry
        xr_delete(V);
        //.		xr_free			(name);
        Registry.erase(it);
    }
    else
    {
        // Registry entry not-found - just special type of visual / particles / etc.
        xr_delete(V);
    }
    V = NULL;
}

void CModelPool::Prefetch()
{
    Logging(FALSE);

    CTimer timer;
    timer.Start();

    prefetchVisuals();
    if (vis_prefetch)
    {
        now_prefetch2 = true;
        prefetchModels();
        prefetchParticles("pe");
        prefetchParticles("pg");
        now_prefetch2 = false;
    }

    Msg("* [%s]: all prefetching time: [%.3f s.]", __FUNCTION__,
        timer.GetElapsed_sec());

    Logging(TRUE);
}

void CModelPool::prefetchVisuals()
{
    begin_prefetch1(true);

    // prefetch visuals
    string256 section;
    strconcat(sizeof(section), section, "prefetch_visuals_",
              g_pGamePersistent->m_game_params.m_game_type);
    CInifile::Sect& sect = pSettings->r_section(section);

    CTimer timer;
    timer.Start();

    begin_prefetch1(true);
    for (auto I = sect.Data.begin(); I != sect.Data.end(); I++)
    {
        const CInifile::Item& item = *I;
        dxRender_Visual* V = Create(item.first.c_str());
        Delete(V, FALSE);
    }
    begin_prefetch1(false);

    Msg("* [%s]: [%s] prefetching time (%u): [%.3f s.]", __FUNCTION__, section,
        sect.Data.size(), timer.GetElapsed_sec());
}

void CModelPool::prefetchModels()
{
    if (!vis_prefetch->section_exist("prefetch"))
        return;

    CTimer timer;
    timer.Start();

    const auto& sect = vis_prefetch->r_section("prefetch");
    u32 cnt = 0;
    for (const auto& it : sect.Data)
    {
        const shared_str& low_name = it.first;
        if (!Instance_Find(low_name.c_str()))
        {
            shared_str fname;
            fname.sprintf("%s.ogf", low_name.c_str());
            if (FS.exist("$game_meshes$", fname.c_str()))
            {
                dxRender_Visual* V = Create(low_name.c_str());
                Delete(V, FALSE);
                cnt++;
            }
            else
                Msg("! [%s]: %s not found in $game_meshes$", __FUNCTION__,
                    fname.c_str());
        }
    }

    Msg("* [%s]: [prefetch] prefetching time (%u): [%.3f s.]", __FUNCTION__,
        cnt, timer.GetElapsed_sec());
}

void CModelPool::prefetchParticles(const std::string sectName)
{
    if (!vis_prefetch->section_exist(sectName.c_str()))
        return;

    CTimer timer;
    timer.Start();

    const auto& sect = vis_prefetch->r_section(sectName.c_str());
    u32 cnt = 0;
    for (const auto& it : sect.Data)
    {
        auto V = RImplementation.model_CreateParticles(it.first.c_str());
        if (V)
        {
            cnt++;
            Render->model_Delete(V);
        }
    }

    Msg("* [%s]: [%s] prefetching time (%u): [%.3f s.]", __FUNCTION__,
        sectName.c_str(), cnt, timer.GetElapsed_sec());
}

void CModelPool::ClearPool(BOOL b_complete)
{
    for (auto& I : Pool)
    {
        if (!b_complete && vis_prefetch)
        {
            std::string s(I.first.c_str());
            if (m_prefetched.find(s) == m_prefetched.end() &&
                !vis_prefetch->line_exist("prefetch", I.first.c_str()))
                b_complete = TRUE;
        }
        Discard(I.second, b_complete);
    }
    Pool.clear();
}

dxRender_Visual* CModelPool::CreatePE(PS::CPEDef* source)
{
    PS::CParticleEffect* V =
        (PS::CParticleEffect*)Instance_Create(MT_PARTICLE_EFFECT);
    V->Compile(source);
    refreshPrefetchSect("pe", source->Name());
    return V;
}

dxRender_Visual* CModelPool::CreatePG(PS::CPGDef* source)
{
    PS::CParticleGroup* V =
        (PS::CParticleGroup*)Instance_Create(MT_PARTICLE_GROUP);
    V->Compile(source);
    refreshPrefetchSect("pg", source->Name());
    return V;
}

void CModelPool::dump()
{
    Log("--- model pool --- begin:");
    u32 sz = 0;
    u32 k = 0;
    for (xr_vector<ModelDef>::iterator I = Models.begin(); I != Models.end(); I++)
    {
        CKinematics* K = PCKinematics(I->model);
        if (K)
        {
            u32 cur = K->mem_usage(false);
            sz += cur;
            Msg("#%3d: [%3d/%5d Kb] - %s", k++, I->refs, cur / 1024, I->name.c_str());
        }
    }
    Msg("--- models: %d, mem usage: %d Kb ", k, sz / 1024);
    sz = 0;
    k = 0;
    int free_cnt = 0;
    for (REGISTRY_IT it = Registry.begin(); it != Registry.end(); it++)
    {
        CKinematics* K = PCKinematics((dxRender_Visual*)it->first);
        VERIFY(K);
        if (K)
        {
            u32 cur = K->mem_usage(true);
            sz += cur;
            bool b_free = (Pool.find(it->second) != Pool.end());
            if (b_free)
                ++free_cnt;
            Msg("#%3d: [%s] [%5d Kb] - %s", k++, (b_free) ? "free" : "used", cur / 1024, it->second.c_str());
        }
    }
    Msg("--- instances: %d, free %d, mem usage: %d Kb ", k, free_cnt, sz / 1024);
    Log("--- model pool --- end.");
}

void CModelPool::memory_stats(u32& vb_mem_video, u32& vb_mem_system, u32& ib_mem_video, u32& ib_mem_system)
{
    vb_mem_video = 0;
    vb_mem_system = 0;
    ib_mem_video = 0;
    ib_mem_system = 0;

    xr_vector<ModelDef>::iterator it = Models.begin();
    xr_vector<ModelDef>::const_iterator en = Models.end();

    for (; it != en; ++it)
    {
        dxRender_Visual* ptr = it->model;
        Fvisual* vis_ptr = dynamic_cast<Fvisual*>(ptr);

        if (vis_ptr == NULL)
            continue;
#if !defined(USE_DX10) && !defined(USE_DX11)
        D3DINDEXBUFFER_DESC IB_desc;
        D3DVERTEXBUFFER_DESC VB_desc;

        vis_ptr->m_fast->p_rm_Indices->GetDesc(&IB_desc);

        if (IB_desc.Pool == D3DPOOL_DEFAULT || IB_desc.Pool == D3DPOOL_MANAGED)
            ib_mem_video += IB_desc.Size;

        if (IB_desc.Pool == D3DPOOL_MANAGED || IB_desc.Pool == D3DPOOL_SCRATCH)
            ib_mem_system += IB_desc.Size;

        vis_ptr->m_fast->p_rm_Vertices->GetDesc(&VB_desc);

        if (VB_desc.Pool == D3DPOOL_DEFAULT || VB_desc.Pool == D3DPOOL_MANAGED)
            vb_mem_video += IB_desc.Size;

        if (VB_desc.Pool == D3DPOOL_MANAGED || VB_desc.Pool == D3DPOOL_SCRATCH)
            vb_mem_system += IB_desc.Size;

#else
        D3D_BUFFER_DESC IB_desc;
        D3D_BUFFER_DESC VB_desc;

        vis_ptr->m_fast->p_rm_Indices->GetDesc(&IB_desc);

        ib_mem_video += IB_desc.ByteWidth;
        ib_mem_system += IB_desc.ByteWidth;

        vis_ptr->m_fast->p_rm_Vertices->GetDesc(&VB_desc);

        vb_mem_video += IB_desc.ByteWidth;
        vb_mem_system += IB_desc.ByteWidth;

#endif
    }
}

#ifdef _EDITOR
IC bool _IsBoxVisible(dxRender_Visual* visual, const Fmatrix& transform)
{
    Fbox bb;
    bb.xform(visual->vis.box, transform);
    return ::Render->occ_visible(bb);
}
IC bool _IsValidShader(dxRender_Visual* visual, u32 priority, bool strictB2F)
{
    if (visual->shader)
        return (priority == visual->shader->E[0]->flags.iPriority) && (strictB2F == visual->shader->E[0]->flags.bStrictB2F);
    return false;
}

void CModelPool::Render(dxRender_Visual* m_pVisual, const Fmatrix& mTransform, int priority, bool strictB2F, float m_fLOD)
{
    // render visual
    xr_vector<dxRender_Visual*>::iterator I, E;
    switch (m_pVisual->Type)
    {
    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID: {
        if (_IsBoxVisible(m_pVisual, mTransform))
        {
            CKinematics* pV = dynamic_cast<CKinematics*>(m_pVisual);
            VERIFY(pV);
            if (fis_zero(m_fLOD, EPS) && pV->m_lod)
            {
                if (_IsValidShader(pV->m_lod, priority, strictB2F))
                {
                    RCache.set_Shader(pV->m_lod->shader ? pV->m_lod->shader : EDevice.m_WireShader);
                    RCache.set_xform_world(mTransform);
                    pV->m_lod->Render(1.f);
                }
            }
            else
            {
                I = pV->children.begin();
                E = pV->children.end();
                for (; I != E; I++)
                {
                    if (_IsValidShader(*I, priority, strictB2F))
                    {
                        RCache.set_Shader((*I)->shader ? (*I)->shader : EDevice.m_WireShader);
                        RCache.set_xform_world(mTransform);
                        (*I)->Render(m_fLOD);
                    }
                }
            }
        }
    }
    break;
    case MT_HIERRARHY: {
        if (_IsBoxVisible(m_pVisual, mTransform))
        {
            FHierrarhyVisual* pV = dynamic_cast<FHierrarhyVisual*>(m_pVisual);
            VERIFY(pV);
            I = pV->children.begin();
            E = pV->children.end();
            for (; I != E; I++)
            {
                if (_IsValidShader(*I, priority, strictB2F))
                {
                    RCache.set_Shader((*I)->shader ? (*I)->shader : EDevice.m_WireShader);
                    RCache.set_xform_world(mTransform);
                    (*I)->Render(m_fLOD);
                }
            }
        }
    }
    break;
    case MT_PARTICLE_GROUP: {
        PS::CParticleGroup* pG = dynamic_cast<PS::CParticleGroup*>(m_pVisual);
        VERIFY(pG);
        //		if (_IsBoxVisible(m_pVisual,mTransform))
        {
            RCache.set_xform_world(mTransform);
            for (PS::CParticleGroup::SItemVecIt i_it = pG->items.begin(); i_it != pG->items.end(); i_it++)
            {
                xr_vector<dxRender_Visual*> visuals;
                i_it->GetVisuals(visuals);
                for (xr_vector<dxRender_Visual*>::iterator it = visuals.begin(); it != visuals.end(); it++)
                    Render(*it, Fidentity, priority, strictB2F, m_fLOD);
            }
        }
    }
    break;
    case MT_PARTICLE_EFFECT: {
        //		if (_IsBoxVisible(m_pVisual,mTransform))
        {
            if (_IsValidShader(m_pVisual, priority, strictB2F))
            {
                RCache.set_Shader(m_pVisual->shader ? m_pVisual->shader : EDevice.m_WireShader);
                RCache.set_xform_world(mTransform);
                m_pVisual->Render(m_fLOD);
            }
        }
    }
    break;
    default:
        if (_IsBoxVisible(m_pVisual, mTransform))
        {
            if (_IsValidShader(m_pVisual, priority, strictB2F))
            {
                RCache.set_Shader(m_pVisual->shader ? m_pVisual->shader : EDevice.m_WireShader);
                RCache.set_xform_world(mTransform);
                m_pVisual->Render(m_fLOD);
            }
        }
        break;
    }
}

void CModelPool::RenderSingle(dxRender_Visual* m_pVisual, const Fmatrix& mTransform, float m_fLOD)
{
    for (int p = 0; p < 4; p++)
    {
        Render(m_pVisual, mTransform, p, false, m_fLOD);
        Render(m_pVisual, mTransform, p, true, m_fLOD);
    }
}
void CModelPool::OnDeviceDestroy() { Destroy(); }
#endif

void CModelPool::save_vis_prefetch()
{
    if (vis_prefetch)
    {
        process_vis_prefetch();
        vis_prefetch->save_as();
    }
}

void CModelPool::process_vis_prefetch()
{
    processPrefetchSect("prefetch");
    processPrefetchSect("pe");
    processPrefetchSect("pg");
}

void CModelPool::processPrefetchSect(const std::string sectName)
{
    if (!vis_prefetch->section_exist(sectName.c_str()))
        return;

    std::vector<std::string> expired;
    auto& sect = vis_prefetch->r_section(sectName.c_str());
    for (auto& it : sect.Data)
    {
        std::string s{it.second.c_str()};
        float need = std::stof(s) * 0.5f; // делить пополам
        // -0.5..+0.5 - добавить случайность, чтобы не было общего выключения
        float rnd = Random.randF() - 0.5f;
        float val = need + rnd * 0.1f;
        if (val > 0.1f)
            vis_prefetch->w_float(sectName.c_str(), it.first.c_str(), val);
        else
            expired.emplace_back(it.first.c_str());
    }

    for (const auto& s : expired)
        vis_prefetch->remove_line(sectName.c_str(), s.c_str());
}

void CModelPool::begin_prefetch1(bool val) { now_prefetch1 = val; }
