#pragma once
#include "../xr_3da/GameMtlLib.h"

class CPhysicsShellHolder;

class CPHSoundPlayer
{
    ref_sound m_sound;
    CPhysicsShellHolder* m_object;
    u32 m_next_snd_time;
    SGameMtlPair* m_last_mtl_pair;

public:
    void Init(CPhysicsShellHolder* m_object);
    void Play(SGameMtlPair* mtl_pair, const Fvector& pos, bool check_vel = true, float* vol = nullptr);
    void PlayNext(SGameMtlPair* mtl_pair, Fvector* pos, float* vol = nullptr);

    CPHSoundPlayer() : m_last_mtl_pair(nullptr), m_next_snd_time(0), m_object(nullptr){};
    CPHSoundPlayer(CPhysicsShellHolder* m_object);
    virtual ~CPHSoundPlayer();

private:
};
