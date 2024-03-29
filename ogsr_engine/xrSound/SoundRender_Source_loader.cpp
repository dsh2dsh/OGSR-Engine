#include "stdafx.h"

#include <msacm.h>

#include "soundrender_core.h"
#include "soundrender_source.h"

//	SEEK_SET	0	File beginning
//	SEEK_CUR	1	Current file pointer position
//	SEEK_END	2	End-of-file
int ov_seek_func(void* datasource, s64 offset, int whence)
{
    switch (whence)
    {
    case SEEK_SET: ((IReader*)datasource)->seek((int)offset); break;
    case SEEK_CUR: ((IReader*)datasource)->advance((int)offset); break;
    case SEEK_END: ((IReader*)datasource)->seek((int)offset + ((IReader*)datasource)->length()); break;
    }
    return 0;
}
size_t ov_read_func(void* ptr, size_t size, size_t nmemb, void* datasource)
{
    IReader* F = (IReader*)datasource;
    size_t exist_block = _max(0ul, iFloor(F->elapsed() / (float)size));
    size_t read_block = _min(exist_block, nmemb);
    F->r(ptr, (int)(read_block * size));
    return read_block;
}
int ov_close_func(void* datasource) { return 0; }
long ov_tell_func(void* datasource) { return ((IReader*)datasource)->tell(); }

void CSoundRender_Source::decompress(u32 line, OggVorbis_File* ovf)
{
    VERIFY(ovf);
    // decompression of one cache-line
    u32 line_size = SoundRender->cache.get_linesize();
    char* dest = (char*)SoundRender->cache.get_dataptr(CAT, line);
    u32 buf_offs = (line * line_size) / 2 / m_wformat.nChannels;
    u32 left_file = dwBytesTotal - buf_offs;
    u32 left = (u32)_min(left_file, line_size);

    // seek
    u32 cur_pos = u32(ov_pcm_tell(ovf));
    if (cur_pos != buf_offs)
        ov_pcm_seek(ovf, buf_offs);

    // decompress
    i_decompress_fr(ovf, dest, left);
}

void CSoundRender_Source::LoadWave(LPCSTR pName)
{
    pname = pName;

    // Load file into memory and parse WAV-format
    OggVorbis_File ovf;
    ov_callbacks ovc = {ov_read_func, ov_seek_func, ov_close_func, ov_tell_func};
    IReader* wave = FS.r_open(pname.c_str());
    R_ASSERT3(wave && wave->length(), "Can't open wave file:", pname.c_str());
    ov_open_callbacks(wave, &ovf, NULL, 0, ovc);

    vorbis_info* ovi = ov_info(&ovf, -1);
    // verify
    R_ASSERT3(ovi, "Invalid source info:", pName);
    R_ASSERT3(ovi->rate == 44100, "Invalid source rate:", pName);

#ifdef DEBUG
    if (ovi->channels == 2)
    {
        Msg("stereo sound source [%s]", pName);
    }
#endif // #ifdef DEBUG

    ZeroMemory(&m_wformat, sizeof(WAVEFORMATEX));

    m_wformat.nSamplesPerSec = (ovi->rate); // 44100;
    m_wformat.wFormatTag = WAVE_FORMAT_PCM;
    m_wformat.nChannels = u16(ovi->channels);
    m_wformat.wBitsPerSample = 16;

    m_wformat.nBlockAlign = m_wformat.wBitsPerSample / 8 * m_wformat.nChannels;
    m_wformat.nAvgBytesPerSec = m_wformat.nSamplesPerSec * m_wformat.nBlockAlign;

    s64 pcm_total = ov_pcm_total(&ovf, -1);
    dwBytesTotal = u32(pcm_total * m_wformat.nBlockAlign);
    fTimeTotal = s_f_def_source_footer + dwBytesTotal / float(m_wformat.nAvgBytesPerSec);

    vorbis_comment* ovm = ov_comment(&ovf, -1);
    if (ovm->comments)
    {
        IReader F(ovm->user_comments[0], ovm->comment_lengths[0]);
        u32 vers = F.r_u32();
        if (vers == 0x0001)
        {
            m_fMinDist = F.r_float();
            m_fMaxDist = F.r_float();
            m_fBaseVolume = 1.f;
            m_uGameType = F.r_u32();
            m_fMaxAIDist = m_fMaxDist;
        }
        else if (vers == 0x0002)
        {
            m_fMinDist = F.r_float();
            m_fMaxDist = F.r_float();
            m_fBaseVolume = F.r_float();
            m_uGameType = F.r_u32();
            m_fMaxAIDist = m_fMaxDist;
        }
        else if (vers == OGG_COMMENT_VERSION)
        {
            m_fMinDist = F.r_float();
            m_fMaxDist = F.r_float();
            m_fBaseVolume = F.r_float();
            m_uGameType = F.r_u32();
            m_fMaxAIDist = F.r_float();
        }
        else
        {
            Log("! Invalid ogg-comment version, file: ", pName);
        }
    }
    else
    {
        Log("! Missing ogg-comment, file: ", pName);
    }
    R_ASSERT3((m_fMaxAIDist >= 0.1f) && (m_fMaxDist >= 0.1f), "Invalid max distance.", pName);

    ov_clear(&ovf);
    FS.r_close(wave);
}

void CSoundRender_Source::load(LPCSTR name)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    if (m_loaded)
        return;

    string_path fn, N;
    xr_strcpy(N, name);
    strlwr(N);
    if (strext(N))
        *strext(N) = 0;

    fname = N;

    strconcat(sizeof(fn), fn, N, ".ogg");
    if (!FS.exist("$level$", fn))
        FS.update_path(fn, "$game_sounds$", fn);

    ASSERT_FMT_DBG(FS.exist(fn), "! Can't find sound [%s.ogg]", N);
    if (!FS.exist(fn))
    {
        Msg("! Can't find sound [%s.ogg]", N);
        FS.update_path(fn, "$game_sounds$", "$no_sound.ogg");
    }

    LoadWave(fn);
    LoadWaveLtx(fn);
    SoundRender->cache.cat_create(CAT, dwBytesTotal);
    m_loaded = true;
}

void CSoundRender_Source::unload()
{
    SoundRender->cache.cat_destroy(CAT);
    fTimeTotal = 0.0f;
    dwBytesTotal = 0;
    m_loaded = false;
}

bool CSoundRender_Source::loaded()
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    return m_loaded;
}

#include <filesystem>
#include "..\COMMON_AI\ai_sounds.h"

void CSoundRender_Source::LoadWaveLtx(LPCSTR fn)
{
    std::filesystem::path path{fn};
    path.replace_extension(".ltx");

    IReader* ltx = FS.r_open(path.string().c_str());
    if (!ltx)
        return;
    auto iniData = xr_new<CInifile>(ltx, path.parent_path().string().c_str());
    ltx->close();

    static constexpr const char* sect = "comments";
    if (!iniData->section_exist(sect))
        return;

    if (iniData->line_exist(sect, "game_type"))
    {
        std::string gameType{iniData->r_string_wb(sect, "game_type").c_str()};
        get_token_id(
            anomaly_type_token, gameType.c_str(),
            [&](int id) { m_uGameType = u32(id); },
            [&, fname = __func__]() {
                Msg("![%s]: unknown game_type '%s' in %s", fname,
                    gameType.c_str(), path.string().c_str());
            });
    }

    m_fBaseVolume =
        READ_IF_EXISTS(iniData, r_float, sect, "volume", m_fBaseVolume);

    m_fMinDist = READ_IF_EXISTS(iniData, r_float, sect, "min_dist", m_fMinDist);
    m_fMaxDist = READ_IF_EXISTS(iniData, r_float, sect, "max_dist", m_fMaxDist);

    m_fMaxAIDist =
        READ_IF_EXISTS(iniData, r_float, sect, "max_ai_dist", m_fMaxAIDist);

    bool log = READ_IF_EXISTS(iniData, r_bool, sect, "log", false);
    if (log)
    {
        MsgIfDbg("[%s]: found %s", __func__, path.string().c_str());
        MsgIfDbg("game_type   = \"%s\"",
                 get_token_name(anomaly_type_token, m_uGameType));
        MsgIfDbg("volume      = %.2f", m_fBaseVolume);
        MsgIfDbg("min_dist    = %.2f", m_fMinDist);
        MsgIfDbg("max_dist    = %.2f", m_fMaxDist);
        MsgIfDbg("max_ai_dist = %.2f", m_fMaxAIDist);
    }

    xr_delete(iniData);
}
