#include "stdafx.h"

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

CSoundRender_Source* CSoundRender_Core::i_create_source(LPCSTR name)
{
    // Search
    string256 id;
    xr_strcpy(id, name);
    strlwr(id);
    if (strext(id))
        *strext(id) = 0;
    std::string s(id);

    CSoundRender_Source* S;
    {
        std::scoped_lock<std::mutex> lock(s_sources_mutex);
        auto it = s_sources.find(s);
        if (it == s_sources.end())
        {
            S = xr_new<CSoundRender_Source>();
            s_sources[s] = S;
        }
        else
            S = it->second;
    }

    // Load a _new one
    S->load(id);

    return S;
}

void CSoundRender_Core::i_destroy_source(CSoundRender_Source* S)
{
    // No actual destroy at all
}
