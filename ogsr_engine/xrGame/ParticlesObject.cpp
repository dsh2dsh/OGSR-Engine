//----------------------------------------------------
// file: PSObject.cpp
//----------------------------------------------------
#include "stdafx.h"

#include "ParticlesObject.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/ParticleCustom.h"
#include "..\xr_3da\render.h"
#include "..\xr_3da\IGame_Persistent.h"

const Fvector zero_vel = {0.f, 0.f, 0.f};

CParticlesObject::CParticlesObject(LPCSTR p_name, BOOL bAutoRemove, bool destroy_on_game_load) : inherited(destroy_on_game_load) { Init(p_name, 0, bAutoRemove); }

void CParticlesObject::Init(LPCSTR p_name, IRender_Sector* S, BOOL bAutoRemove)
{
    m_bLooped = false;
    m_bStopping = false;
    m_bAutoRemove = bAutoRemove;
    float time_limit = 0.0f;

    // create visual
    renderable.visual = Render->model_CreateParticles(p_name);
    VERIFY(renderable.visual);
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    time_limit = V->GetTimeLimit();

    if (time_limit > 0.f)
    {
        m_iLifeTime = iFloor(time_limit * 1000.f);
    }
    else
    {
        if (bAutoRemove)
        {
            R_ASSERT3(!m_bAutoRemove, "Can't set auto-remove flag for looped particle system.", p_name);
        }
        else
        {
            m_iLifeTime = 0;
            m_bLooped = true;
        }
    }

    // spatial
    spatial.type = 0;
    spatial.sector = S;

    // sheduled
    shedule.t_min = 20;
    shedule.t_max = 200; // 50;
    shedule_register();

    dwLastTime = Device.dwTimeGlobal;
    mt_dt = 0;
    move_to_defer = false;
}

//----------------------------------------------------
CParticlesObject::~CParticlesObject()
{
    VERIFY(0 == mt_dt);

    //	we do not need this since CPS_Instance does it
    //	shedule_unregister		();
}

void CParticlesObject::UpdateSpatial()
{
    // spatial	(+ workaround occasional bug inside particle-system)
    if (_valid(renderable.visual->getVisData().sphere))
    {
        Fvector P;
        float R;
        renderable.xform.transform_tiny(P, renderable.visual->getVisData().sphere.P);
        R = renderable.visual->getVisData().sphere.R;
        if (0 == spatial.type)
        {
            // First 'valid' update - register
            spatial.type = STYPE_RENDERABLE;
            spatial.sphere.set(P, R);
            spatial_register();
        }
        else
        {
            BOOL bMove = FALSE;
            if (!P.similar(spatial.sphere.P, EPS_L * 10.f))
                bMove = TRUE;
            if (!fsimilar(R, spatial.sphere.R, 0.15f))
                bMove = TRUE;
            if (bMove)
            {
                spatial.sphere.set(P, R);
                spatial_move();
            }
        }
    }
}

const shared_str CParticlesObject::Name()
{
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    return (V) ? V->Name() : "";
}

//----------------------------------------------------
void CParticlesObject::Play()
{
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->Play();
    mt_dt = 0;
    m_bStopping = false;
    if (Device.is_second_thread_active())
        dwLastTime = Device.dwTimeGlobal;
    else
    {
        dwLastTime = Device.dwTimeGlobal - 33ul;
        PerformAllTheWork(0);
    }
}

void CParticlesObject::play_at_pos(const Fvector& pos, BOOL xform)
{
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    Fmatrix m;
    m.translate(pos);
    V->UpdateParent(m, zero_vel, xform);
    V->Play();
    mt_dt = 0;
    m_bStopping = false;
    if (Device.is_second_thread_active())
        dwLastTime = Device.dwTimeGlobal;
    else
    {
        dwLastTime = Device.dwTimeGlobal - 33ul;
        PerformAllTheWork(0);
    }
}

void CParticlesObject::Stop(BOOL bDefferedStop)
{
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->Stop(bDefferedStop);
    m_bStopping = true;
}

void CParticlesObject::shedule_Update(u32 _dt)
{
    if (move_to_defer)
        MoveTo(move_to_defer_pos, move_to_defer_vel, true);
    inherited::shedule_Update(_dt);

    // Update
    if (m_bDead)
        return;

    u32 dt = Device.dwTimeGlobal - dwLastTime;
    if (dt)
    {
        if constexpr (false)
        { //. AlexMX comment this line// NO UNCOMMENT - DON'T WORK PROPERLY
            mt_dt = dt;
            Device.add_to_seq_parallel(fastdelegate::MakeDelegate(this, &CParticlesObject::PerformAllTheWork_mt));
        }
        else
        {
            mt_dt = 0;
            IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
            VERIFY(V);
            V->OnFrame(dt);
        }
        dwLastTime = Device.dwTimeGlobal;
    }
    UpdateSpatial();
}

void CParticlesObject::PerformAllTheWork(u32 _dt)
{
    // Update
    u32 dt = Device.dwTimeGlobal - dwLastTime;
    if (dt)
    {
        IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
        VERIFY(V);
        V->OnFrame(dt);
        dwLastTime = Device.dwTimeGlobal;
    }
    UpdateSpatial();
}

void CParticlesObject::PerformAllTheWork_mt()
{
    if (0 == mt_dt)
        return; //???
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->OnFrame(mt_dt);
    mt_dt = 0;
}

void CParticlesObject::SetXFORM(const Fmatrix& m)
{
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->UpdateParent(m, zero_vel, TRUE);
    renderable.xform.set(m);
    UpdateSpatial();
}

void CParticlesObject::UpdateParent(const Fmatrix& m, const Fvector& vel)
{
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    V->UpdateParent(m, vel, FALSE);
    UpdateSpatial();
}

Fvector& CParticlesObject::Position() { return renderable.visual->getVisData().sphere.P; }

float CParticlesObject::shedule_Scale()
{
    return Device.vCameraPosition.distance_to(Position()) / 100.f; // 200.f;
}

void CParticlesObject::renderable_Render()
{
    VERIFY(renderable.visual);
    if (move_to_defer)
        MoveTo(move_to_defer_pos, move_to_defer_vel, true);
    u32 dt = Device.dwTimeGlobal - dwLastTime;
    if (dt)
    {
        IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
        VERIFY(V);
        V->OnFrame(dt);
        dwLastTime = Device.dwTimeGlobal;
    }
    ::Render->set_Transform(&renderable.xform);
    ::Render->add_Visual(renderable.visual);
}
bool CParticlesObject::IsAutoRemove()
{
    if (m_bAutoRemove)
        return true;
    else
        return false;
}
void CParticlesObject::SetAutoRemove(bool auto_remove)
{
    VERIFY(/*m_bStopping || */ !IsLooped());
    m_bAutoRemove = auto_remove;
}

//играются ли партиклы, отличается от PSI_Alive, тем что после
//остановки Stop партиклы могут еще доигрывать анимацию IsPlaying = true
bool CParticlesObject::IsPlaying()
{
    IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
    VERIFY(V);
    return !!V->IsPlaying();
}

void CParticlesObject::MoveTo(const Fvector pos, const Fvector vel, bool now)
{
    if (now || !Device.is_second_thread_active())
    {
        move_to_defer = false;
        Fmatrix XF;
        XF.translate(pos);
        UpdateParent(XF, vel);
    }
    else
    {
        move_to_defer = true;
        move_to_defer_pos = pos;
        move_to_defer_vel = vel;
    }
}
