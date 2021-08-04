#include "stdafx.h"
#include "physicobject.h"
#include "PhysicsShell.h"
#include "Physics.h"
#include "xrserver_objects_alife.h"
#include "Level.h"
#include "..\Include/xrRender/Kinematics.h"
#include "..\Include/xrRender/KinematicsAnimated.h"
#include "../xr_3da/xr_collide_form.h"
#include "PHSynchronize.h"
#include "game_object_space.h"
#include "ExtendedGeom.h"

#include "PhysicsShellAnimator.h"


CPhysicObject::CPhysicObject(void)
    : m_type(epotBox), m_mass(10.f), m_collision_hit_callback(nullptr), m_just_after_spawn(false), m_activated(false), m_is_ai_obstacle(true) {}

CPhysicObject::~CPhysicObject(void) {}
BOOL CPhysicObject::net_Spawn(CSE_Abstract* DC)
{
    CSE_Abstract* e = (CSE_Abstract*)(DC);
    CSE_ALifeObjectPhysic* po = smart_cast<CSE_ALifeObjectPhysic*>(e);
    R_ASSERT(po);
    m_type = EPOType(po->type);
    m_mass = po->mass;
    m_collision_hit_callback = NULL;
    inherited::net_Spawn(DC);

    create_collision_model();

    CPHSkeleton::Spawn(e);
    setVisible(TRUE);
    setEnabled(TRUE);

    if (!PPhysicsShell()->isBreakable() && !CScriptBinder::object() && !CPHSkeleton::IsRemoving())
        SheduleUnregister();

    // if (PPhysicsShell()->Animated())
    //{
    //	processing_activate();
    //}

    if ( Visual() ) {
      IKinematics *K = Visual()->dcast_PKinematics();
      VERIFY( K );
      u16 door_bone = K->LL_BoneID( "door" );
      if ( door_bone != BI_NONE )
        m_is_ai_obstacle = false;
    }
    m_is_ai_obstacle = READ_IF_EXISTS( pSettings, r_bool, cNameSect(), "is_ai_obstacle", m_is_ai_obstacle );

    m_just_after_spawn = true;
    m_activated = false;

    // приложить небольшую силу для того, чтобы объект начал падать
    PPhysicsShell()->applyImpulse( Fvector().set( 0.f, -1.0f, 0.f ), 0.5f * PPhysicsShell()->getMass() );

    return TRUE;
}
void CPhysicObject::create_collision_model()
{
  xr_delete(collidable.model);
  collidable.model = xr_new<CCF_Skeleton>(this);
}

static CPhysicsShellHolder* retrive_collide_object(bool bo1, dContact& c)
{
    CPhysicsShellHolder* collide_obj = 0;

    dxGeomUserData* ud = 0;
    if (bo1)
        ud = PHRetrieveGeomUserData(c.geom.g2);
    else
        ud = PHRetrieveGeomUserData(c.geom.g1);

    if (ud)
        collide_obj = static_cast<CPhysicsShellHolder*>(ud->ph_ref_object);
    else
        collide_obj = 0;
    return collide_obj;
}
static void door_ignore(bool& do_collide, bool bo1, dContact& c, SGameMtl* /*material_1*/, SGameMtl* /*material_2*/)
{
    CPhysicsShellHolder* collide_obj = retrive_collide_object(bo1, c);
    if (!collide_obj || collide_obj->cast_actor())
        return;

    CPhysicsShell* ph_shell = collide_obj->PPhysicsShell();
    if (!ph_shell)
    {
        do_collide = false; //? must be AI
        return;
    }
    VERIFY(ph_shell);

    if (ph_shell->HasTracedGeoms())
        return;

    do_collide = false;
}

void CPhysicObject::set_door_ignore_dynamics()
{
    R_ASSERT(PPhysicsShell());
    PPhysicsShell()->remove_ObjectContactCallback(door_ignore);
    PPhysicsShell()->add_ObjectContactCallback(door_ignore);
    // PPhysicsShell()->
}
void CPhysicObject::unset_door_ignore_dynamics()
{
    R_ASSERT(PPhysicsShell());
    PPhysicsShell()->remove_ObjectContactCallback(door_ignore);
}

void CPhysicObject::SpawnInitPhysics(CSE_Abstract* D)
{
    CreatePhysicsShell(D);
    RunStartupAnim(D);
}

void CPhysicObject::RunStartupAnim(CSE_Abstract *D)
{
	if(Visual()&&smart_cast<IKinematics*>(Visual()))
	{
		//		CSE_PHSkeleton	*po	= smart_cast<CSE_PHSkeleton*>(D);
		IKinematicsAnimated*	PKinematicsAnimated=NULL;
		R_ASSERT			(Visual()&&smart_cast<IKinematics*>(Visual()));
		PKinematicsAnimated	=smart_cast<IKinematicsAnimated*>(Visual());
		if(PKinematicsAnimated)
		{
			CSE_Visual					*visual = smart_cast<CSE_Visual*>(D);
			R_ASSERT					(visual);
			R_ASSERT2					(*visual->startup_animation,"no startup animation");
			PKinematicsAnimated->PlayCycle(*visual->startup_animation);
		}
		smart_cast<IKinematics*>(Visual())->CalculateBones_Invalidate();
		smart_cast<IKinematics*>(Visual())->CalculateBones	();

	}
}

void CPhysicObject::net_Destroy()
{
    // if (PPhysicsShell()->Animated())
    //{
    //	processing_deactivate();
    //}

    inherited::net_Destroy();
    CPHSkeleton::RespawnInit();
}

void CPhysicObject::net_Save(NET_Packet& P)
{
    inherited::net_Save(P);
    CPHSkeleton::SaveNetState(P);
}
void CPhysicObject::CreatePhysicsShell(CSE_Abstract* e)
{
    CSE_ALifeObjectPhysic* po = smart_cast<CSE_ALifeObjectPhysic*>(e);
    CreateBody(po);
}

void CPhysicObject::CreateSkeleton(CSE_ALifeObjectPhysic* po)
{
    if (m_pPhysicsShell)
        return;
    if (!Visual())
        return;
    LPCSTR fixed_bones = *po->fixed_bones;
    m_pPhysicsShell = P_build_Shell(this, !po->_flags.test(CSE_PHSkeleton::flActive), fixed_bones);
    ApplySpawnIniToPhysicShell(&po->spawn_ini(), m_pPhysicsShell, fixed_bones[0] != '\0');
    ApplySpawnIniToPhysicShell(
        smart_cast<IKinematics*>(Visual())->LL_UserData(), m_pPhysicsShell, fixed_bones[0] != '\0');
}

void CPhysicObject::Load(LPCSTR section)
{
    inherited::Load(section);
    CPHSkeleton::Load(section);
}

void CPhysicObject::shedule_Update(u32 dt)
{
    inherited::shedule_Update(dt);
    CPHSkeleton::Update(dt);
}

void CPhysicObject::UpdateCL()
{
    inherited::UpdateCL();

    //Если наш физический объект анимированный, то
    //двигаем объект за анимацией
    if (m_pPhysicsShell->PPhysicsShellAnimator())
    {
        m_pPhysicsShell->AnimatorOnFrame();
    }

    PHObjectPositionUpdate();
}
void CPhysicObject::PHObjectPositionUpdate()
{
    if (m_pPhysicsShell)
    {
        if (m_type == epotBox)
        {
            m_pPhysicsShell->Update();
            XFORM().set(m_pPhysicsShell->mXFORM);
        }
        else if (m_pPhysicsShell->PPhysicsShellAnimator())
        {
            Fmatrix m;
            m_pPhysicsShell->InterpolateGlobalTransform(&m);
            XFORM().set(m);
        }
        else
            m_pPhysicsShell->InterpolateGlobalTransform(&XFORM());
    }
}

void CPhysicObject::AddElement(CPhysicsElement* root_e, int id)
{
    IKinematics* K = smart_cast<IKinematics*>(Visual());

    CPhysicsElement* E = P_create_Element();
    CBoneInstance& B = K->LL_GetBoneInstance(u16(id));
    E->mXFORM.set(K->LL_GetTransform(u16(id)));
    Fobb bb = K->LL_GetBox(u16(id));

    if (bb.m_halfsize.magnitude() < 0.05f)
    {
        bb.m_halfsize.add(0.05f);
    }
    E->add_Box(bb);
    E->setMass(10.f);
    E->set_ParentElement(root_e);
    B.set_callback(bctPhysics, m_pPhysicsShell->GetBonesCallback(), E);
    m_pPhysicsShell->add_Element(E);
    if (!(m_type == epotFreeChain && root_e == 0))
    {
        CPhysicsJoint* J = P_create_Joint(CPhysicsJoint::full_control, root_e, E);
        J->SetAnchorVsSecondElement(0, 0, 0);
        J->SetAxisDirVsSecondElement(1, 0, 0, 0);
        J->SetAxisDirVsSecondElement(0, 1, 0, 2);
        J->SetLimits(-M_PI / 2, M_PI / 2, 0);
        J->SetLimits(-M_PI / 2, M_PI / 2, 1);
        J->SetLimits(-M_PI / 2, M_PI / 2, 2);
        m_pPhysicsShell->add_Joint(J);
    }

    CBoneData& BD = K->LL_GetData(u16(id));
    for (vecBonesIt it = BD.children.begin(); BD.children.end() != it; ++it)
    {
        AddElement(E, (*it)->GetSelfID());
    }
}

void CPhysicObject::CreateBody(CSE_ALifeObjectPhysic* po)
{
    if (m_pPhysicsShell)
        return;
    IKinematics* pKinematics = smart_cast<IKinematics*>(Visual());
    switch (m_type)
    {
    case epotBox:
    {
        m_pPhysicsShell = P_build_SimpleShell(this, m_mass, !po->_flags.test(CSE_ALifeObjectPhysic::flActive));
    }
    break;
    case epotFixedChain:
    case epotFreeChain:
    {
        m_pPhysicsShell = P_create_Shell();
        m_pPhysicsShell->set_Kinematics(pKinematics);
        AddElement(0, pKinematics->LL_GetBoneRoot());
        m_pPhysicsShell->setMass1(m_mass);
    }
    break;

    case epotSkeleton:
    {
        // pKinematics->LL_SetBoneRoot(0);
        CreateSkeleton(po);
    }
    break;

    default: {
    }
    break;
    }

    m_pPhysicsShell->mXFORM.set(XFORM());
    m_pPhysicsShell->SetAirResistance(0.001f, 0.02f);
    if (pKinematics)
    {
        SAllDDOParams disable_params;
        disable_params.Load(pKinematics->LL_UserData());
        m_pPhysicsShell->set_DisableParams(disable_params);
    }
    // m_pPhysicsShell->SetAirResistance(0.002f, 0.3f);
}

BOOL CPhysicObject::net_SaveRelevant()
{
    return TRUE; //! m_flags.test(CSE_ALifeObjectPhysic::flSpawnCopy);
}

BOOL CPhysicObject::UsedAI_Locations() { return (FALSE); }
void CPhysicObject::InitServerObject(CSE_Abstract* D)
{
    CPHSkeleton::InitServerObject(D);
    CSE_ALifeObjectPhysic* l_tpALifePhysicObject = smart_cast<CSE_ALifeObjectPhysic*>(D);
    if (!l_tpALifePhysicObject)
        return;
    l_tpALifePhysicObject->type = u32(m_type);
}
ICollisionHitCallback* CPhysicObject::get_collision_hit_callback() { return m_collision_hit_callback; }
void CPhysicObject::set_collision_hit_callback(ICollisionHitCallback* cc)
{
    xr_delete(m_collision_hit_callback);
    m_collision_hit_callback = cc;
}

//////////////////////////////////////////////////////////////////////////
/*
DEFINE_MAP_PRED	(LPCSTR,	CPhysicsJoint*,	JOINT_P_MAP,	JOINT_P_PAIR_IT,	pred_str);

JOINT_P_MAP			*l_tpJointMap = new JOINT_P_MAP();

l_tpJointMap->insert(std::make_pair(bone_name,joint*));
JOINT_P_PAIR_IT		I = l_tpJointMap->find(bone_name);
if (l_tpJointMap->end()!=I){
//bone_name is found and is an pair_iterator
(*I).second
}

JOINT_P_PAIR_IT		I = l_tpJointMap->begin();
JOINT_P_PAIR_IT		E = l_tpJointMap->end();
for ( ; I != E; ++I) {
(*I).second->joint_method();
Msg("%s",(*I).first);
}

*/

//////////////////////////////////////////////////////////////////////////
bool CPhysicObject::is_ai_obstacle() const
{
  return m_is_ai_obstacle; //!!(READ_IF_EXISTS(pSettings, r_bool, cNameSect(), "is_ai_obstacle", true));
}

// network synchronization ----------------------------

void CPhysicObject::net_Export( CSE_Abstract* E ) {
  CSE_ALifeObjectPhysic* obj_ph = smart_cast<CSE_ALifeObjectPhysic*>( E );
  obj_ph->m_u8NumItems = 0;
};

bool CPhysicObject::get_door_vectors( Fvector& closed, Fvector& open ) const {
  VERIFY( Visual() );
  IKinematics *K = Visual()->dcast_PKinematics();
  VERIFY( K );
  u16 door_bone = K->LL_BoneID( "door" );
  if( door_bone == BI_NONE )
    return false;
  const CBoneData  &bd    = K->LL_GetData( door_bone );
  const SBoneShape &shape = bd.shape;
  if( shape.type != SBoneShape::stBox )
    return false;

  if( shape.flags.test( SBoneShape::sfNoPhysics ) )
    return false;

  Fmatrix start_bone_pos;
  K->Bone_GetAnimPos( start_bone_pos, door_bone, u8(-1), true );

  Fmatrix start_pos = Fmatrix().mul_43( XFORM(), start_bone_pos );

  const Fobb &box = shape.box;

  Fvector center_pos;
  start_pos.transform_tiny( center_pos, box.m_translate );

  Fvector door_dir;
  start_pos.transform_dir( door_dir, box.m_rotate.i );
  Fvector door_dir_local =  box.m_rotate.i ;
  //Fvector door_dir_bone; start_bone_pos.transform_dir(door_dir_bone, box.m_rotate.i );

  const Fvector det_vector = Fvector().sub( center_pos, start_pos.c  );

  if ( door_dir.dotproduct( det_vector ) < 0.f ) {
    door_dir.invert();
    door_dir_local.invert();
    //door_dir_bone.invert();
  }

  const SJointIKData &joint = bd.IK_data;

  if( joint.type != jtJoint )
    return false;
  Fvector2 limits = joint.limits[ 1 ].limit;

  if ( fsimilar( limits.x, limits.y ) ) {
    auto j = m_pPhysicsShell->get_Joint( door_bone );
    j->GetLimits( limits.y, limits.x, 0 );
  }

  //if( limits.y < EPS ) //limits.y - limits.x < EPS
  //      return false;

  if ( M_PI - limits.y < EPS && M_PI + limits.x < EPS )
    return false;

  Fmatrix to_hi = Fmatrix().rotateY( -limits.x );
  to_hi.transform_dir( open, door_dir_local );

  Fmatrix to_lo = Fmatrix().rotateY( -limits.y );
  to_lo.transform_dir( closed, door_dir_local );

  start_pos.transform_dir( open );
  start_pos.transform_dir( closed );

  return true;
}
