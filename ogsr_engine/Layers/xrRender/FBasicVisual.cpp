// dxRender_Visual.cpp: implementation of the dxRender_Visual class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#ifndef _EDITOR
#include "../../xr_3da/render.h"
#endif // #ifndef _EDITOR

#include <filesystem>
#include "fbasicvisual.h"
#include "../../xr_3da/fmesh.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

IRender_Mesh::~IRender_Mesh()
{
    _RELEASE(p_rm_Vertices);
    _RELEASE(p_rm_Indices);
}

dxRender_Visual::dxRender_Visual()
{
    Type = 0;
    shader = 0;
    vis.clear();
}

dxRender_Visual::~dxRender_Visual() {}

void dxRender_Visual::Release() {}

bool replaceShadersLine(const char* N, char* fnS, u32 fnS_size, LPCSTR item,
                        char* fnT)
{
    if (!pSettings->line_exist("vis_shaders_replace", item))
        return false;

    LPCSTR overrides = pSettings->r_string("vis_shaders_replace", item);
    if (xr_strcmp(overrides, "log") == 0)
    {
        MsgIfDbg("[%s]: N[%s], fnS[%s], fnT[%s]", __FUNCTION__, N, fnS, fnT);
        return true;
    }

    u32 cnt = _GetItemCount(overrides);
    ASSERT_FMT(cnt % 2 == 0,
               "[%s]: vis_shaders_replace: wrong format cnt = %u: %s = %s",
               __FUNCTION__, cnt, item, overrides);

    for (u32 i = 0; i < cnt; i += 2)
    {
        string256 s1, s2;
        _GetItem(overrides, i, s1);
        _GetItem(overrides, i + 1, s2);
        if (xr_strcmp(s1, fnS) == 0)
        {
            // MsgIfDbg( "[%s]: %s[%s]: %s -> %s", __FUNCTION__, N, fnT, fnS, s2 );
            xr_strcpy(fnS, fnS_size, s2);
            break;
        }
    }

    return true;
}

void replaceShaders(const char* N, char* fnS, u32 fnS_size, char* fnT)
{
    if (!pSettings->section_exist("vis_shaders_replace"))
        return;

    if (replaceShadersLine(N, fnS, fnS_size, N, fnT))
        return;

    if (strchr(N, ':'))
    {
        std::string s(N);
        s.erase(s.find(":"));
        if (replaceShadersLine(N, fnS, fnS_size, s.c_str(), fnT))
            return;
    }

    std::filesystem::path p = N;
    while (p.has_parent_path())
    {
        p = p.parent_path();
        if (replaceShadersLine(N, fnS, fnS_size, p.string().c_str(), fnT))
            return;
    }
}

// CStatTimer						tscreate;

void dxRender_Visual::Load(const char* N, IReader* data, u32)
{
    dbg_name = N;

    // header
    VERIFY(data);
    ogf_header hdr;
    if (data->r_chunk_safe(OGF_HEADER, &hdr, sizeof(hdr)))
    {
        R_ASSERT2(hdr.format_version == xrOGF_FormatVersion,
                  "Invalid visual version");
        Type = hdr.type;
        // if (hdr.shader_id)	shader	= ::Render->getShader	(hdr.shader_id);
        if (hdr.shader_id)
            shader = ::RImplementation.getShader(hdr.shader_id);
        vis.box.set(hdr.bb.min, hdr.bb.max);
        vis.sphere.set(hdr.bs.c, hdr.bs.r);
    }
    else
    {
        FATAL("Invalid visual");
    }

    // Shader
    if (data->find_chunk(OGF_TEXTURE))
    {
        string256 fnT, fnS;
        data->r_stringZ(fnT, sizeof(fnT));
        data->r_stringZ(fnS, sizeof(fnS));
        replaceShaders(N, fnS, sizeof fnS, fnT);
        shader.create(fnS, fnT);
    }

    // desc
#ifdef _EDITOR
    if (data->find_chunk(OGF_S_DESC))
        desc.Load(*data);
#endif
}

#define PCOPY(a) a = pFrom->a
void dxRender_Visual::Copy(dxRender_Visual* pFrom)
{
    PCOPY(Type);
    PCOPY(shader);
    PCOPY(vis);
    PCOPY(dbg_name);
}
