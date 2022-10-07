#pragma once
#include "../xr_3da/GameMtlLib.h"

class CPhysicsShellHolder;

class CPHSoundPlayer
{
		ref_sound					m_sound																		;
		CPhysicsShellHolder			*m_object;

public:
	void Init( CPhysicsShellHolder* m_object );
	void Play( SGameMtlPair* mtl_pair, const Fvector& pos, bool check_vel = true, float* vol = nullptr );

	CPHSoundPlayer() : m_object( nullptr) {};
									CPHSoundPlayer			(CPhysicsShellHolder *m_object)									;
virtual								~CPHSoundPlayer			()													;


private:
};
