#include "stdafx.h"
#include "EffectorBobbing.h"

#include "actor.h"
#include "actor_defs.h"

#define BOBBING_SECT "bobbing_effector"

#define CROUCH_FACTOR 0.75f
#define SPEED_REMINDER 5.f

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CEffectorBobbing::CEffectorBobbing() : CEffectorCam(eCEBobbing, 10000.f)
{
    fTime = 0;
    fReminderFactor = 0;
    m_cur_state = 0;
    m_cur_amp = 0.f;

    m_fAmplitudeRun = pSettings->r_float(BOBBING_SECT, "run_amplitude");
    m_fAmplitudeWalk = pSettings->r_float(BOBBING_SECT, "walk_amplitude");
    m_fAmplitudeLimp = pSettings->r_float(BOBBING_SECT, "limp_amplitude");

    m_fSpeedRun = pSettings->r_float(BOBBING_SECT, "run_speed");
    m_fSpeedWalk = pSettings->r_float(BOBBING_SECT, "walk_speed");
    m_fSpeedLimp = pSettings->r_float(BOBBING_SECT, "limp_speed");
}

CEffectorBobbing::~CEffectorBobbing() {}

void CEffectorBobbing::SetState(u32 mstate, bool limping, bool ZoomMode)
{
    dwMState = mstate;

    if (isActorAccelerated(mstate, ZoomMode))
        m_cur_state = 1;
    else if (limping)
        m_cur_state = 2;
    else
        m_cur_state = 3;
}

BOOL CEffectorBobbing::ProcessCam(SCamEffectorInfo& info)
{
    if (dwMState & ACTOR_DEFS::mcAnyMove)
    {
        if (fReminderFactor < 1.f)
            fReminderFactor += SPEED_REMINDER * Device.fTimeDelta;
        else
            fReminderFactor = 1.f;
    }
    else
    {
        if (fReminderFactor > 0.f)
            fReminderFactor -= SPEED_REMINDER * Device.fTimeDelta;
        else
            fReminderFactor = 0.f;
    }
    clamp(fReminderFactor, 0.f, 1.f);

    if (fReminderFactor > 0.f)
    {
        Fmatrix M;
        M.identity();
        M.j.set(info.n);
        M.k.set(info.d);
        M.i.crossproduct(info.n, info.d);
        M.c.set(info.p);

        // apply footstep bobbing effect
        Fvector dangle;
        float k = ((dwMState & ACTOR_DEFS::mcCrouch) ? CROUCH_FACTOR : 1.f);

        float A, speed;
        switch (m_cur_state)
        {
        case 1:
            A = m_fAmplitudeRun * k;
            speed = m_fSpeedRun;
            break;
        case 2:
            A = m_fAmplitudeLimp * k;
            speed = m_fSpeedLimp;
            break;
        default:
            A = m_fAmplitudeWalk * k;
            speed = m_fSpeedWalk;
            break;
        }
        float dt = Device.fTimeDelta * speed * k;
        fTime += dt;
        m_cur_amp = m_cur_amp * (1.f - dt) + A * dt;

        float _sinA = _abs(_sin(fTime) * m_cur_amp) * fReminderFactor;
        float _cosA = _cos(fTime) * m_cur_amp * fReminderFactor;

        info.p.y += _sinA;
        dangle.x = _cosA;
        dangle.z = _cosA;
        dangle.y = _sinA;

        Fmatrix R;
        R.setHPB(dangle.x, dangle.y, dangle.z);

        Fmatrix mR;
        mR.mul(M, R);

        info.d.set(mR.k);
        info.n.set(mR.j);
    }
    else
    {
        fTime = 0.f;
        m_cur_amp = 0.f;
    }

    return TRUE;
}
