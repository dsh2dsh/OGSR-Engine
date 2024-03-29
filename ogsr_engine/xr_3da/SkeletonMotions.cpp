//---------------------------------------------------------------------------
#include "stdafx.h"

#include "SkeletonMotions.h"
//#include 	"SkeletonAnimated.h"
#include "Fmesh.h"
#include "motion.h"
#include "..\Include\xrRender\Kinematics.h"

motions_container* g_pMotionsContainer = 0;

u16 CPartition::part_id(const shared_str& name) const
{
    for (u16 i = 0; i < MAX_PARTS; ++i)
    {
        const CPartDef& pd = part(i);
        if (pd.Name == name)
            return i;
    }
    Msg("!there is no part named [%s]", name.c_str());
    return u16(-1);
}

void CPartition::load(IKinematics* V, LPCSTR model_name)
{
    CInifile* ini = V->LL_UserData();
    if (!ini)
        return;

    if (ini->sections().empty() || !ini->section_exist("part_0"))
        return;

    static const shared_str part_name = "partition_name";
    for (u32 i = 0; i < MAX_PARTS; ++i)
    {
        string64 buff;
        xr_sprintf(buff, sizeof(buff), "part_%d", i);

        auto& S = ini->r_section(buff);
        auto it = S.Data.begin();
        auto it_e = S.Data.end();
        if (S.Data.size())
        {
            P[i].bones.clear_not_free();
        }
        for (; it != it_e; ++it)
        {
            const CInifile::Item& I = *it;
            if (I.first == part_name)
            {
                P[i].Name = I.second;
            }
            else
            {
                u32 bid = V->LL_BoneID(I.first.c_str());
                P[i].bones.push_back(bid);
            }
        }
    }
}

u16 find_bone_id(vecBones* bones, shared_str nm)
{
    for (u16 i = 0; i < (u16)bones->size(); i++)
        if (bones->at(i)->name == nm)
            return i;
    return BI_NONE;
}

//-----------------------------------------------------------------------
BOOL motions_value::load(LPCSTR N, IReader* data, vecBones* bones)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    if (m_loaded)
        return m_load_ok;
    else
        m_loaded = true;

    m_id = N;

    m_load_ok = true;
    // Load definitions
    U16Vec rm_bones(bones->size(), BI_NONE);
    IReader* MP = data->open_chunk(OGF_S_SMPARAMS);

    if (MP)
    {
        u16 vers = MP->r_u16();
        u16 part_bone_cnt = 0;
        string128 buf;
        R_ASSERT3(vers <= xrOGF_SMParamsVersion, "Invalid OGF/OMF version:", N);

        // partitions
        u16 part_count;
        part_count = MP->r_u16();

        for (u16 part_i = 0; part_i < part_count; part_i++)
        {
            CPartDef& PART = m_partition[part_i];
            MP->r_stringZ(buf, sizeof(buf));
            PART.Name = _strlwr(buf);
            PART.bones.resize(MP->r_u16());

            for (xr_vector<u32>::iterator b_it = PART.bones.begin(); b_it < PART.bones.end(); b_it++)
            {
                MP->r_stringZ(buf, sizeof(buf));
                u16 m_idx = u16(MP->r_u32());
                *b_it = find_bone_id(bones, buf);

#ifdef _EDITOR

                if (*b_it == BI_NONE)
                {
                    m_load_ok = false;
                    Msg("!Can't find bone: '%s'", buf);
                }

                if (rm_bones.size() <= m_idx)
                {
                    m_load_ok = false;
                    Msg("!Can't load: '%s' invalid bones count", N);
                }

#else

                VERIFY3(*b_it != BI_NONE, "Can't find bone:", buf);

#endif

                if (m_load_ok)
                    rm_bones[m_idx] = u16(*b_it);
            }
            part_bone_cnt = u16(part_bone_cnt + (u16)PART.bones.size());
        }

#ifdef _EDITOR

        if (part_bone_cnt != (u16)bones->size())
        {
            m_load_ok = false;
            Msg("!Different bone count[%s] [Object: '%d' <-> Motions: '%d']", N, bones->size(), part_bone_cnt);
        }

#else

        VERIFY3(part_bone_cnt == (u16)bones->size(), "Different bone count '%s'", N);

#endif

        if (m_load_ok)
        {
            // motion defs (cycle&fx)
            u16 mot_count = MP->r_u16();
            m_mdefs.resize(mot_count);

            for (u16 mot_i = 0; mot_i < mot_count; mot_i++)
            {
                MP->r_stringZ(buf, sizeof(buf));
                shared_str nm = _strlwr(buf);
                u32 dwFlags = MP->r_u32();
                CMotionDef& D = m_mdefs[mot_i];
                D.Load(MP, dwFlags, vers);
                //.             m_mdefs.push_back	(D);

                if (dwFlags & esmFX)
                    m_fx.insert(mk_pair(nm, mot_i));
                else
                    m_cycle.insert(mk_pair(nm, mot_i));

                m_motion_map.emplace(nm, mot_i);
            }
        }
        MP->close();
    }
    else
    {
        Debug.fatal(DEBUG_INFO, "Old skinned model version unsupported! (%s)", N);
    }

    if (!m_load_ok)
        return m_load_ok;

    // Load animation
    IReader* MS = data->open_chunk(OGF_S_MOTIONS);
    if (!MS)
    {
        m_load_ok = false;
        return m_load_ok;
    }

    u32 dwCNT = 0;
    MS->r_chunk_safe(0, &dwCNT, sizeof(dwCNT));
    VERIFY(dwCNT < 0x3FFF); // MotionID 2 bit - slot, 14 bit - motion index

    // set per bone motion size
    for (u32 i = 0; i < bones->size(); i++)
        m_motions[bones->at(i)->name].resize(dwCNT);

    // load motions
    for (u16 m_idx = 0; m_idx < (u16)dwCNT; m_idx++)
    {
        string128 mname;
        R_ASSERT(MS->find_chunk(m_idx + 1));
        MS->r_stringZ(mname, sizeof(mname));

#ifdef DEBUG

        // sanity check
        xr_strlwr(mname);
        auto I = m_motion_map.find(mname);
        VERIFY3(I != m_motion_map.end(), "Can't find motion:", mname);
        VERIFY3(I->second == m_idx, "Invalid motion index:", mname);

#endif

        u32 dwLen = MS->r_u32();
        for (u32 i = 0; i < bones->size(); i++)
        {
            u16 bone_id = rm_bones[i];
            VERIFY2(bone_id != BI_NONE, "Invalid remap index.");
            CMotion& M = m_motions[bones->at(bone_id)->name][m_idx];
            M.set_count(dwLen);
            M.set_flags(MS->r_u8());

            if (M.test_flag(flRKeyAbsent))
            {
                CKeyQR* r = (CKeyQR*)MS->pointer();
                u32 crc_q = crc32(r, sizeof(CKeyQR));
                M._keysR.create(crc_q, 1, r);
                MS->advance(1 * sizeof(CKeyQR));
            }
            else
            {
                u32 crc_q = MS->r_u32();
                M._keysR.create(crc_q, dwLen, (CKeyQR*)MS->pointer());
                MS->advance(dwLen * sizeof(CKeyQR));
            }
            if (M.test_flag(flTKeyPresent))
            {
                u32 crc_t = MS->r_u32();
                if (M.test_flag(flTKey16IsBit))
                {
                    M._keysT16.create(crc_t, dwLen, (CKeyQT16*)MS->pointer());
                    MS->advance(dwLen * sizeof(CKeyQT16));
                }
                else
                {
                    M._keysT8.create(crc_t, dwLen, (CKeyQT8*)MS->pointer());
                    MS->advance(dwLen * sizeof(CKeyQT8));
                };

                MS->r_fvector3(M._sizeT);
                MS->r_fvector3(M._initT);
            }
            else
            {
                MS->r_fvector3(M._initT);
            }
        }
    }
    //	Msg("Motions %d/%d %4d/%4d/%d, %s",p_cnt,m_cnt, m_load,m_total,m_r,N);
    MS->close();

    return m_load_ok;
}

MotionVec* motions_value::bone_motions(shared_str bone_name)
{
    auto I = m_motions.find(bone_name);
    if (I == m_motions.end())
        return 0;

    return &(*I).second;
}

u32 motions_value::decReference()
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    m_dwReference--;
    return m_dwReference;
}

u32 motions_value::incReference()
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    m_dwReference++;
    return m_dwReference;
}

u32 motions_value::getReference()
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    return m_dwReference;
}

bool motions_value::hasLoadedOK()
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    return m_load_ok;
}

//-----------------------------------
motions_container::motions_container() {}
// extern shared_str s_bones_array_const;
motions_container::~motions_container()
{
    //	clean	(false);
    //	clean	(true);
    //	dump	();
    VERIFY(container.empty());
    //	Igor:
    // s_bones_array_const = 0;
}

bool motions_container::has(shared_str key)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    auto it = container.find(key);
    if (it == container.end())
        return false;
    return it->second->hasLoadedOK();
}

motions_value* motions_container::dock(shared_str key, IReader* data, vecBones* bones)
{
    motions_value* result = 0;
    {
        std::scoped_lock<std::mutex> lock(m_mutex);
        auto I = container.find(key);
        if (I == container.end())
        {
            result = xr_new<motions_value>();
            container.emplace(key, result);
        }
        else
            result = I->second;
    }

    // loading motions
    VERIFY(data);
    return result->load(key.c_str(), data, bones) ? result : 0;
}

void motions_container::clean(bool force_destroy)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    auto it = container.begin();
    auto _E = container.end();
    if (force_destroy)
    {
        for (; it != _E; it++)
        {
            motions_value* sv = it->second;
            xr_delete(sv);
        }
        container.clear();
    }
    else
    {
        for (; it != _E;)
        {
            motions_value* sv = it->second;
            if (sv->getReference() == 0)
            {
                auto i_current = it;
                auto i_next = ++it;
                xr_delete(sv);
                container.erase(i_current);
                it = i_next;
            }
            else
            {
                it++;
            }
        }
    }
}
void motions_container::dump()
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    auto it = container.begin();
    auto _E = container.end();
    Log("--- motion container --- begin:");
    u32 sz = sizeof(*this);
    for (u32 k = 0; it != _E; k++, it++)
    {
        sz += it->second->mem_usage();
        Msg("#%3d: [%3d/%5d Kb] - %s", k, it->second->getReference(), it->second->mem_usage() / 1024, it->first.c_str());
    }
    Msg("--- items: %d, mem usage: %d Kb ", container.size(), sz / 1024);
    Log("--- motion container --- end.");
}

//////////////////////////////////////////////////////////////////////////
// High level control
void CMotionDef::Load(IReader* MP, u32 fl, u16 version)
{
    // params
    bone_or_part = MP->r_u16(); // bCycle?part_id:bone_id;
    motion = MP->r_u16(); // motion_id
    speed = MP->r_float(); // Quantize(MP->r_float());
    speed_k = 1.0f;
    power = Quantize(MP->r_float());
    accrue = Quantize(MP->r_float());
    falloff = Quantize(MP->r_float());
    flags = (u16)fl;
    if (!(flags & esmFX) && (falloff >= accrue))
        falloff = u16(accrue - 1);

    if (version >= 4)
    {
        u32 cnt = MP->r_u32();
        if (cnt > 0)
        {
            marks.resize(cnt);

            for (u32 i = 0; i < cnt; ++i)
                marks[i].Load(MP);
        }
    }
}

bool CMotionDef::StopAtEnd() { return !!(flags & esmStopAtEnd); }

bool shared_motions::create(shared_str key, IReader* data, vecBones* bones)
{
    motions_value* v = g_pMotionsContainer->dock(key, data, bones);
    if (0 != v)
        v->incReference();
    destroy();
    p_ = v;
    return (0 != v);
}

bool shared_motions::create(shared_motions const& rhs)
{
    motions_value* v = rhs.p_;
    if (0 != v)
        v->incReference();
    destroy();
    p_ = v;
    return (0 != v);
}

void shared_motions::destroy()
{
    if (p_ == 0)
        return;
    if (p_->decReference() == 0)
        p_ = 0;
}

const motion_marks::interval* motion_marks::pick_mark(const float& t) const
{
    C_ITERATOR it = intervals.begin();
    C_ITERATOR it_e = intervals.end();

    for (; it != it_e; ++it)
    {
        const interval& I = (*it);
        if (I.first <= t && I.second >= t)
            return &I;

        if (I.first > t)
            break;
    }
    return NULL;
}

bool motion_marks::is_mark_between(float const& t0, float const& t1) const
{
    VERIFY(t0 <= t1);

    C_ITERATOR i = intervals.begin();
    C_ITERATOR e = intervals.end();
    for (; i != e; ++i)
    {
        VERIFY((*i).first <= (*i).second);

        if ((*i).first == t0)
            return (true);

        if ((*i).first > t0)
        {
            if ((*i).second <= t1)
                return (true);

            if ((*i).first <= t1)
                return (true);

            return (false);
        }

        if ((*i).second < t0)
            continue;

        if ((*i).second == t0)
            return (true);

        return (true);
    }

    return (false);
}

float motion_marks::time_to_next_mark(float time) const
{
    C_ITERATOR i = intervals.begin();
    C_ITERATOR e = intervals.end();
    float result_dist = FLT_MAX;
    for (; i != e; ++i)
    {
        float dist = (*i).first - time;
        if (dist > 0.f && dist < result_dist)
            result_dist = dist;
    }
    return result_dist;
}

void ENGINE_API motion_marks::Load(IReader* R)
{
    xr_string tmp;
    R->r_string(tmp);
    name = tmp.c_str();
    u32 cnt = R->r_u32();
    intervals.resize(cnt);
    for (u32 i = 0; i < cnt; ++i)
    {
        interval& item = intervals[i];
        item.first = R->r_float();
        item.second = R->r_float();
    }
}
#ifdef _EDITOR
void motion_marks::Save(IWriter* W)
{
    W->w_string(name.c_str());
    u32 cnt = intervals.size();
    W->w_u32(cnt);
    for (u32 i = 0; i < cnt; ++i)
    {
        interval& item = intervals[i];
        W->w_float(item.first);
        W->w_float(item.second);
    }
}
#endif
