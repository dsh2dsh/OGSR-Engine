#include "stdafx.h"

#include "PHSoundPlayer.h"
#include "PhysicsShellHolder.h"

CPHSoundPlayer::CPHSoundPlayer( CPhysicsShellHolder* obj ) {
	Init( obj );
}

CPHSoundPlayer::~CPHSoundPlayer()
{
	m_sound.stop();
	m_object=NULL;
}

void CPHSoundPlayer::Init( CPhysicsShellHolder* obj ) {
	m_object = obj;
}

void CPHSoundPlayer::Play( SGameMtlPair* mtl_pair, const Fvector& pos, bool check_vel, float* vol ) {
	if ( m_sound._feedback() )
		return;

	if ( check_vel ) {
		Fvector vel;
		m_object->PHGetLinearVell( vel );
		if ( vel.square_magnitude() <= 0.01f )
			return;
	}

	CLONE_MTL_SOUND( m_sound, mtl_pair, CollideSounds );
	m_sound.play_at_pos( smart_cast<CPhysicsShellHolder*>( m_object ), pos );
	if ( vol )
		m_sound._feedback()->set_volume( *vol );
}
