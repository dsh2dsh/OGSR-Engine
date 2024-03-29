// WeaponHUD.cpp:	HUD для оружия и прочих предметов, которые
//					могут держать в руках персонажи, также используется
//					для синхронизации анимаций с видом от 3-го лица
//////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "WeaponHUD.h"
#include "Weapon.h"
#include "../xr_3da/Motion.h"
#include "..\Include/xrRender/Kinematics.h"
#include "..\Include/xrRender/KinematicsAnimated.h"
#include "level.h"
#include "MathUtils.h"
#include "actor.h"
#include "ActorCondition.h"
#include "ActorEffector.h"

weapon_hud_container* g_pWeaponHUDContainer = 0;

BOOL weapon_hud_value::load(const shared_str& section, CHudItem* owner)
{
    m_adjust_mode = false;

    // Geometry and transform
    Fvector ypr;
    ypr = pSettings->r_fvector3(section, "orientation");
    ypr.mul(PI / 180.f);

    m_offset.setHPB(ypr.x, ypr.y, ypr.z);
    m_position = pSettings->r_fvector3(section, "position");
    if (pSettings->line_exist(section, "position_add"))
        m_position_add = pSettings->r_fvector3(section, "position_add");
    else
        m_position_add.set(0.f, 0.f, 0.f);
    m_position_add_scope = READ_IF_EXISTS(pSettings, r_fvector3, section, "position_add_scope", m_position_add);
    SetScope(false);

    // Visual
    LPCSTR visual_name = pSettings->r_string(section, "visual");
    m_animations = smart_cast<IKinematicsAnimated*>(::Render->model_Create(visual_name));

    m_bAllowBobbing = true;

    if (Core.Features.test(xrCore::Feature::wpn_bobbing))
    {
        if (pSettings->line_exist(section, "allow_bobbing"))
            m_bAllowBobbing = pSettings->r_bool(section, "allow_bobbing");
    }

    // fire bone
    if (smart_cast<CWeapon*>(owner))
    {
        LPCSTR fire_bone = pSettings->r_string(section, "fire_bone");
        m_fire_bone = m_animations->dcast_PKinematics()->LL_BoneID(fire_bone);
        if (m_fire_bone >= m_animations->dcast_PKinematics()->LL_BoneCount())
            Debug.fatal(DEBUG_INFO, "There is no '%s' bone for weapon '%s'.", fire_bone, *section);
        m_fp_offset = pSettings->r_fvector3(section, "fire_point");
        if (pSettings->line_exist(section, "fire_point2"))
            m_fp2_offset = pSettings->r_fvector3(section, "fire_point2");
        else
            m_fp2_offset = m_fp_offset;
        if (pSettings->line_exist(owner->object().cNameSect(), "shell_particles"))
            m_sp_offset = pSettings->r_fvector3(section, "shell_point");
        else
            m_sp_offset.set(0, 0, 0);
    }
    else
    {
        m_fire_bone = -1;
        m_fp_offset.set(0, 0, 0);
        m_fp2_offset.set(0, 0, 0);
        m_sp_offset.set(0, 0, 0);
    }
    return TRUE;
}

void weapon_hud_value::SetScope(bool has_scope)
{
    if (m_adjust_mode)
        return;
    m_cur_position_add = has_scope ? m_position_add_scope : m_position_add;
    m_offset.translate_over(Fvector().add(m_position, m_cur_position_add));
}

void weapon_hud_value::EnableAdjustMode(bool mode)
{
    if (mode)
    {
        m_cur_position_add = Fvector().set(0.f, 0.f, 0.f);
        m_offset.translate_over(m_position);
    }
    m_adjust_mode = mode;
}

weapon_hud_value::~weapon_hud_value()
{
    IRenderVisual* v = m_animations->dcast_RenderVisual();
    ::Render->model_Delete(v);
    m_animations = nullptr;
}

u32 shared_weapon_hud::motion_length(MotionIDEx& M)
{
    IKinematicsAnimated* skeleton_animated = p_->m_animations;
    VERIFY(skeleton_animated);
    CMotionDef* motion_def = skeleton_animated->LL_GetMotionDef(M.m_MotionID);
    VERIFY(motion_def);

    if (motion_def->flags & esmStopAtEnd)
    {
        CMotion* motion = skeleton_animated->LL_GetRootMotion(M.m_MotionID);
        return iFloor(0.5f + 1000.f * motion->GetLength() / motion_def->Speed() * M.stop_k);
    }
    return 0;
}

MotionID shared_weapon_hud::motion_id(LPCSTR name) { return p_->m_animations->ID_Cycle_Safe(name); }

void shared_weapon_hud::SetScope(bool has_scope) { p_->SetScope(has_scope); }

void shared_weapon_hud::EnableAdjustMode(bool mode) { p_->EnableAdjustMode(mode); }

CWeaponHUD::CWeaponHUD(CHudItem* pHudItem)
{
    m_bVisible = false;
    m_pParentWeapon = pHudItem;
    m_bHidden = true;
    m_bStopAtEndAnimIsRunning = false;
    m_pCallbackItem = NULL;
    if (Core.Features.test(xrCore::Feature::wpn_bobbing))
        m_bobbing = xr_new<CWeaponBobbing>(pHudItem);
    m_Transform.identity();
}

CWeaponHUD::~CWeaponHUD()
{
    if (Core.Features.test(xrCore::Feature::wpn_bobbing))
        xr_delete(m_bobbing);
}

void CWeaponHUD::Load(LPCSTR section) { m_shared_data.create(section, m_pParentWeapon); }

void CWeaponHUD::Init()
{
    m_bStopAtEndAnimIsRunning = false;
    m_pCallbackItem = NULL;
}

void CWeaponHUD::net_DestroyHud()
{
    m_bStopAtEndAnimIsRunning = false;
    m_pCallbackItem = NULL;
    Visible(false);
}

void CWeaponHUD::UpdatePosition(const Fmatrix& trans)
{
    Fmatrix xform = trans;
    if (Core.Features.test(xrCore::Feature::wpn_bobbing) && m_shared_data.get_value()->m_bAllowBobbing)
    {
        m_bobbing->Update(xform);
    }
    m_Transform.mul(xform, m_shared_data.get_value()->m_offset);
    VERIFY(!fis_zero(DET(m_Transform)));
}

MotionID CWeaponHUD::animGet(LPCSTR name) { return m_shared_data.motion_id(name); }

void CWeaponHUD::animDisplay(const MotionIDEx& M, BOOL bMixIn)
{
    if (!m_bVisible)
        return;

    auto PKinematicsAnimated = smart_cast<IKinematicsAnimated*>(Visual());
    PKinematicsAnimated->PlayCycle(M.m_MotionID, bMixIn);
    PKinematicsAnimated->dcast_PKinematics()->CalculateBones_Invalidate();

    if (!M.eff_name)
        return;

    auto current_actor = smart_cast<CActor*>(Level().CurrentControlEntity());
    if (current_actor && smart_cast<CWeapon*>(m_pParentWeapon)->H_Parent() == current_actor)
    {
        string_path ce_path, anm_name;
        xr_strconcat(anm_name, "camera_effects\\weapon\\", M.eff_name, ".anm");
        ASSERT_FMT(FS.exist(ce_path, "$game_anims$", anm_name), "!![%s] Can't find [%s]", __FUNCTION__, anm_name);

        current_actor->Cameras().RemoveCamEffector(eCEWeaponAction);

        auto e = xr_new<CAnimatorCamEffector>();
        e->SetType(eCEWeaponAction);
        e->SetHudAffect(false);
        e->SetCyclic(false);
        e->Start(anm_name);
        current_actor->Cameras().AddCamEffector(e);
    }
}

void CWeaponHUD::animPlay(MotionIDEx& M, BOOL bMixIn, CHudItem* W, u32 state)
{
    m_startedAnimState = state;
    Show();
    animDisplay(M, bMixIn);
    u32 anim_time = m_shared_data.motion_length(M);
    if (anim_time > 0)
    {
        m_bStopAtEndAnimIsRunning = true;
        m_pCallbackItem = W;
        m_dwAnimEndTime = Device.dwTimeGlobal + anim_time;
    }
    else
    {
        m_pCallbackItem = NULL;
    }
}

void CWeaponHUD::Update()
{
    if (m_bStopAtEndAnimIsRunning && Device.dwTimeGlobal > m_dwAnimEndTime)
        StopCurrentAnim();
    if (m_bVisible)
        smart_cast<IKinematicsAnimated*>(Visual())->UpdateTracks();
}

void CWeaponHUD::StopCurrentAnim()
{
    m_dwAnimEndTime = 0;
    m_bStopAtEndAnimIsRunning = false;
    if (m_pCallbackItem)
        m_pCallbackItem->OnAnimationEnd(m_startedAnimState);
}

void CWeaponHUD::StopCurrentAnimWithoutCallback()
{
    m_dwAnimEndTime = 0;
    m_bStopAtEndAnimIsRunning = false;

    m_pCallbackItem = NULL;
}

void CWeaponHUD::CreateSharedContainer()
{
    VERIFY(0 == g_pWeaponHUDContainer);
    g_pWeaponHUDContainer = xr_new<weapon_hud_container>();
}
void CWeaponHUD::DestroySharedContainer() { xr_delete(g_pWeaponHUDContainer); }
void CWeaponHUD::CleanSharedContainer()
{
    VERIFY(g_pWeaponHUDContainer);
    g_pWeaponHUDContainer->clean(false);
}

Fvector CWeaponHUD::ZoomOffset()
{
    Fvector cur_add = m_shared_data.get_value()->m_cur_position_add;
    cur_add.y = cur_add.y * _cos(_abs(m_fZoomRotateX));
    return Fvector().sub(m_fZoomOffset, cur_add);
}

void CWeaponHUD::SetZoomOffset(const Fvector& zoom_offset) { m_fZoomOffset = zoom_offset; }

void CWeaponHUD::SetScope(bool has_scope) { m_shared_data.SetScope(has_scope); }

void CWeaponHUD::EnableHUDAdjustMode(bool mode) { m_shared_data.EnableAdjustMode(mode); }

MotionIDEx& random_anim(MotionSVec& v) { return v[Random.randI(v.size())]; }

CWeaponBobbing::CWeaponBobbing(CHudItem* pHudItem)
{
    m_pParentWeapon = pHudItem;
    Load();
}

CWeaponBobbing::~CWeaponBobbing() {}

void CWeaponBobbing::Load()
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

    m_fCrouchFactor = READ_IF_EXISTS(pSettings, r_float, BOBBING_SECT,
                                     "crouch_k", CROUCH_FACTOR);
    m_fZoomFactor =
        READ_IF_EXISTS(pSettings, r_float, BOBBING_SECT, "zoom_k", 1.f);
    m_fScopeZoomFactor = READ_IF_EXISTS(pSettings, r_float, BOBBING_SECT,
                                        "scope_zoom_k", m_fZoomFactor);
}

void CWeaponBobbing::Update(Fmatrix& m)
{
    dwMState = Actor()->get_state();
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
        Fvector dangle;
        Fmatrix R, mR;
        float k = (dwMState & ACTOR_DEFS::mcCrouch) ? m_fCrouchFactor : 1.f;
        float k2 = k;

        bool m_bZoomMode = Actor()->IsZoomAimingMode();
        if (m_bZoomMode)
        {
            auto wpn = smart_cast<CWeapon*>(m_pParentWeapon);
            bool has_scope = wpn->IsScopeAttached() && !wpn->IsGrenadeMode();
            float zoom_factor = has_scope ? m_fScopeZoomFactor : m_fZoomFactor;
            k2 *= zoom_factor;
        }

        float A, speed;
        if (isActorAccelerated(dwMState, m_bZoomMode))
        {
            A = m_fAmplitudeRun * k2;
            speed = m_fSpeedRun;
        }
        else if (Actor()->conditions().IsLimping())
        {
            A = m_fAmplitudeLimp * k2;
            speed = m_fSpeedLimp;
        }
        else
        {
            A = m_fAmplitudeWalk * k2;
            speed = m_fSpeedWalk;
        }
        float dt = Device.fTimeDelta * speed * k;
        fTime += dt;
        m_cur_amp = m_cur_amp * (1.f - dt) + A * dt;

        float _sinA = _abs(_sin(fTime) * m_cur_amp) * fReminderFactor;
        float _cosA = _cos(fTime) * m_cur_amp * fReminderFactor;

        m.c.y += _sinA;
        dangle.x = _cosA;
        dangle.z = _cosA;
        dangle.y = _sinA;

        R.setHPB(dangle.x, dangle.y, dangle.z);

        mR.mul(m, R);

        m.k.set(mR.k);
        m.j.set(mR.j);
    }
    else
    {
        fTime = 0.f;
        m_cur_amp = 0.f;
    }
}
