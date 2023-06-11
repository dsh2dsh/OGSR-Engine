///////////////////////////////////////////////////////////////
// GraviArtifact.cpp
// GraviArtefact - гравитационный артефакт, прыгает на месте
// и неустойчиво парит над землей
///////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "GraviArtifact.h"
#include "PhysicsShell.h"
#include "level.h"
#include "xrmessages.h"
#include "game_cl_base.h"
#include "../Include/xrRender/Kinematics.h"
#include "phworld.h"
#include "../xr_3da/gamemtllib.h"

extern CPHWorld* ph_world;
#define CHOOSE_MAX(x, inst_x, y, inst_y, z, inst_z) \
    if (x > y) \
        if (x > z) \
        { \
            inst_x; \
        } \
        else \
        { \
            inst_z; \
        } \
    else if (y > z) \
    { \
        inst_y; \
    } \
    else \
    { \
        inst_z; \
    }

CGraviArtefact::CGraviArtefact(void)
{
    shedule.t_min = 20;
    shedule.t_max = 50;

    m_fJumpHeight = 0;
    m_fEnergy = 1.f;
    m_jump_min_height = 0;
}

CGraviArtefact::~CGraviArtefact(void) {}

void CGraviArtefact::Load(LPCSTR section)
{
    inherited::Load(section);

    if (pSettings->line_exist(section, "jump_height"))
        m_fJumpHeight = pSettings->r_float(section, "jump_height");
    //	m_fEnergy = pSettings->r_float(section,"energy");
    m_jump_min_height = READ_IF_EXISTS(pSettings, r_float, section, "jump_min_height", 0.f);
    m_jump_under_speed = READ_IF_EXISTS(pSettings, r_float, section, "jump_under_speed", 500.f);
    m_jump_raise_speed = READ_IF_EXISTS(pSettings, r_float, section, "jump_raise_speed", 50.f);
    m_jump_keep_speed = READ_IF_EXISTS(pSettings, r_float, section, "jump_keep_speed", 25.f);
    m_jump_time = READ_IF_EXISTS(pSettings, r_u32, section, "jump_time", 0) * 1000;
    m_jump_debug = READ_IF_EXISTS(pSettings, r_bool, section, "jump_debug", false);

    m_jump_ignore_passable = READ_IF_EXISTS(pSettings, r_bool, section, "jump_ignore_passable", true);
    if (pSettings->line_exist(section, "jump_ignore_materials"))
    {
        LPCSTR item_section = pSettings->r_string(section, "jump_ignore_materials");
        if (item_section && item_section[0])
        {
            int count = _GetItemCount(item_section);
            for (int i = 0; i < count; i++)
            {
                string128 item;
                _GetItem(item_section, i, item);
                u32 id = GMLib.GetMaterialID(item);
                ASSERT_FMT(id != GAMEMTL_NONE_ID,
                           "%s [%s]: jump_ignore_materials contains "
                           "unknown material: %s",
                           cName().c_str(), section, item);
                m_jump_ignore_materials.emplace_back(GMLib.GetMaterialIdx(id));
            }
        }
    }
}

void CGraviArtefact::UpdateCLChild()
{
    VERIFY(!ph_world->Processing());
    if (getVisible() && m_pPhysicsShell)
    {
        if (m_fJumpHeight)
        {
            process_gravity();
            process_jump();
        }
    }
    else if (H_Parent())
    {
        XFORM().set(H_Parent()->XFORM());
    };
}

BOOL CGraviArtefact::net_Spawn(CSE_Abstract* DC)
{
    BOOL result = inherited::net_Spawn(DC);

    if (result)
    {
        m_jump_jump = m_keep = false;
        m_jump_time_end = 0;
        m_raise = true;
    }

    return result;
}

void CGraviArtefact::OnH_B_Independent(bool just_before_destroy)
{
    inherited::OnH_B_Independent(just_before_destroy);
    m_jump_jump = m_keep = false;
    m_raise = true;
    if (m_jump_time)
        m_jump_time_end = Device.dwTimeGlobal + m_jump_time;
}

struct ray_query_param
{
    CGraviArtefact* m_holder;
    float m_range;
    bool m_valid;
    ray_query_param(CGraviArtefact* holder)
    {
        m_holder = holder;
        m_valid = false;
    }
};

static BOOL trace_callback(collide::rq_result& result, LPVOID params)
{
    ray_query_param* param = (ray_query_param*)params;
    if (!result.O)
    {
        // получить треугольник и узнать его материал
        CDB::TRI* T = Level().ObjectSpace.GetStaticTris() + result.element;
        if (T->material < GMLib.CountMaterial())
        {
            CGraviArtefact* holder = param->m_holder;
            auto mtl = GMLib.GetMaterialByIdx(T->material);
            if (mtl->Flags.is(SGameMtl::flPassable) &&
                holder->m_jump_ignore_passable)
                return TRUE;
            if (!holder->m_jump_ignore_materials.empty())
            {
                const auto it = std::find(
                    holder->m_jump_ignore_materials.begin(),
                    holder->m_jump_ignore_materials.end(), T->material);
                if (it != holder->m_jump_ignore_materials.end())
                    return TRUE;
            }
        }
    }
    param->m_range = result.range;
    param->m_valid = true;
    return FALSE;
}

void CGraviArtefact::process_gravity()
{
    Fvector dir = {0, -1, 0};
    Fvector P;
    Center(P);
    Fbox level_box = Level().ObjectSpace.GetBoundingVolume();
    collide::ray_defs RD(P, dir, _abs(P.y - level_box.y1) - 1.f, 0,
                         collide::rqtBoth);
    collide::rq_results RQR;
    ray_query_param params(this);
    Level().ObjectSpace.RayQuery(RQR, RD, trace_callback, &params, NULL, this);
    float raise_speed = m_jump_raise_speed;
    if (!params.m_valid)
    {
        m_jump_jump = false;
        m_keep = false;
        m_raise = true;
        raise_speed = m_jump_under_speed;
    }
    else
    {
        dir.y = -1.f;
        // проверить высоту артефакта
        float range = params.m_range - Radius();
        if (m_jump_min_height && range < m_jump_min_height)
        {
            m_jump_jump = false;
            m_keep = false;
            m_raise = true;
            if (m_jump_debug)
                Msg("[%s]: raising %s range[%f]", __FUNCTION__, cName().c_str(), range);
        }
        else
        {
            if (m_jump_min_height && fsimilar(m_fJumpHeight, m_jump_min_height))
            {
                m_jump_jump = false;
                if (m_keep)
                {
                    if (fsimilar(range, m_keep_height, 0.01f))
                        dir.y = 0;
                    else
                    {
                        m_keep = false;
                        m_raise = true;
                        if (m_jump_debug)
                            Msg("[%s]: lowering %s range[%f]", __FUNCTION__, cName().c_str(), range);
                        raise_speed = -m_jump_keep_speed;
                    }
                }
                else if (m_raise)
                {
                    if (fsimilar(range, m_jump_min_height, 0.01f))
                    {
                        m_keep = true;
                        m_keep_height = range;
                        dir.y = 0;
                    }
                    else
                    {
                        if (m_jump_debug)
                            Msg("[%s]: lowering %s range[%f]", __FUNCTION__, cName().c_str(), range);
                        raise_speed = -m_jump_keep_speed;
                    }
                }
            }
            else
            {
                m_jump_jump = (range < m_fJumpHeight);
                m_keep = false;
                if (m_jump_min_height && m_jump_time)
                {
                    if (m_raise || m_jump_time_end == 0)
                        m_jump_time_end = Device.dwTimeGlobal + m_jump_time;
                    else if (m_jump_time_end < Device.dwTimeGlobal)
                    {
                        if (m_jump_jump && m_jump_time_end + m_jump_time < Device.dwTimeGlobal)
                        {
                            m_jump_time_end = Device.dwTimeGlobal + m_jump_time;
                        }
                        else
                        {
                            if (m_jump_debug)
                                Msg("[%s]: lowering %s range[%f]", __FUNCTION__, cName().c_str(), range);
                            m_jump_jump = false;
                            m_keep = true;
                            dir.y = -m_jump_keep_speed;
                        }
                    }
                }
            }
            m_raise = false;
        }
    }

    if (m_raise || m_keep)
    {
        if (m_pPhysicsShell->get_ApplyByGravity())
            m_pPhysicsShell->set_ApplyByGravity(FALSE);
        Fvector vel;
        m_pPhysicsShell->get_LinearVel(vel);
        vel.y = 0.f;
        m_pPhysicsShell->set_LinearVel(vel);
        if (m_raise)
            dir.y = raise_speed;
        m_pPhysicsShell->applyGravityAccel(dir);
    }
    else if (!m_pPhysicsShell->get_ApplyByGravity())
        m_pPhysicsShell->set_ApplyByGravity(TRUE);
}

void CGraviArtefact::process_jump()
{
    Fvector dir = {0, -1.f, 0};
    if (!m_jump_min_height)
    {
        collide::rq_result RQ;
        // проверить высоту артефакта
        Fvector P = Position();
        P.y += Radius();
        if (Level().ObjectSpace.RayPick(P, dir, m_fJumpHeight + Radius(), collide::rqtBoth, RQ, this))
            m_jump_jump = true;
        else
            m_jump_jump = false;
    }
    if (m_jump_jump)
    {
        dir.y = 1.f;
        m_pPhysicsShell->applyImpulse(dir, 30.f * Device.fTimeDelta * m_pPhysicsShell->getMass());
    }
}
