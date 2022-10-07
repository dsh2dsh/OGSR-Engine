#pragma once

#include "gameobject.h"
#include "physicsshellholder.h"
#include "physicsskeletonobject.h"
#include "PHSkeleton.h"
#include "xrserver_objects_alife.h"
#include "script_export_space.h"

class CSE_ALifeObjectPhysic;
class CPhysicsElement;

struct SPHNetState;
typedef CSE_ALifeObjectPhysic::mask_num_items mask_num_items;

struct net_update_PItem
{
    u32 dwTimeStamp;
    SPHNetState State;
};

struct net_updatePhData
{
    xr_deque<net_update_PItem> NET_IItem;
    /// spline coeff /////////////////////
    // float			SCoeff[3][4];
    /*Fvector			IStartPos;
    Fquaternion		IStartRot;
    Fvector			IRecPos;
    Fquaternion		IRecRot;
    Fvector			IEndPos;
    Fquaternion		IEndRot;	*/

    //	SPHNetState		LastState;
    //	SPHNetState		RecalculatedState;

    //	SPHNetState		PredictedState;

    u32 m_dwIStartTime;
    u32 m_dwIEndTime;
    // u32				m_dwILastUpdateTime;
};

class CPhysicObject : public CPhysicsShellHolder, public CPHSkeleton
{
protected:
    using inherited = CPhysicsShellHolder;

private:
    EPOType m_type;
    float m_mass;
    ICollisionHitCallback* m_collision_hit_callback;
    bool m_is_ai_obstacle;

private:
    // Creating
    void CreateBody(CSE_ALifeObjectPhysic* po);
    void CreateSkeleton(CSE_ALifeObjectPhysic* po);
    void AddElement(CPhysicsElement* root_e, int id);
    void create_collision_model();

public:
    void set_door_ignore_dynamics();
    void unset_door_ignore_dynamics();

public:
    bool get_door_vectors(Fvector& closed, Fvector& open) const;

public:
    CPhysicObject(void);
    virtual ~CPhysicObject(void);

    virtual BOOL net_Spawn(CSE_Abstract* DC);
    virtual void CreatePhysicsShell(CSE_Abstract* e);
    virtual void net_Destroy();
    virtual void Load(LPCSTR section);
    virtual void shedule_Update(u32 dt); //
    virtual void UpdateCL();
    virtual void net_Save(NET_Packet& P);
    virtual BOOL net_SaveRelevant();
    virtual BOOL UsedAI_Locations();
    virtual ICollisionHitCallback* get_collision_hit_callback();
    virtual void set_collision_hit_callback(ICollisionHitCallback* cc);

    virtual bool is_ai_obstacle() const;
    void set_ai_obstacle(bool flag) { m_is_ai_obstacle = flag; }

    virtual void net_Export(CSE_Abstract* E);

protected:
    virtual void SpawnInitPhysics(CSE_Abstract* D);
    virtual void RunStartupAnim(CSE_Abstract* D);
    virtual CPhysicsShellHolder* PPhysicsShellHolder() { return PhysicsShellHolder(); }
    virtual CPHSkeleton* PHSkeleton() { return this; }
    virtual void InitServerObject(CSE_Abstract* po);
    virtual void PHObjectPositionUpdate();

    bool m_just_after_spawn;
    bool m_activated;

    DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CPhysicObject)
#undef script_type_list
#define script_type_list save_type_list(CPhysicObject)
