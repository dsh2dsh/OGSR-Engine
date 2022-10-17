#include "stdafx.h"

#include "PHSoundPlayer.h"
#include "PhysicsShellHolder.h"

CPHSoundPlayer::CPHSoundPlayer(CPhysicsShellHolder* obj) { Init(obj); }

CPHSoundPlayer::~CPHSoundPlayer()
{
    m_sound.stop();
    m_object = NULL;
    m_last_mtl_pair = nullptr;
}

void CPHSoundPlayer::Init(CPhysicsShellHolder* obj)
{
    m_last_mtl_pair = nullptr;
    m_next_snd_time = 0;
    m_object = obj;
}

void CPHSoundPlayer::Play(SGameMtlPair* mtl_pair, const Fvector& pos, bool check_vel, float* vol)
{
    if (m_sound._feedback())
        return;

    if (check_vel)
    {
        Fvector vel;
        m_object->PHGetLinearVell(vel);
        if (vel.square_magnitude() <= 0.01f)
            return;
    }

    CLONE_MTL_SOUND(m_sound, mtl_pair, CollideSounds);
    m_sound.play_at_pos(smart_cast<CPhysicsShellHolder*>(m_object), pos);
    if (vol)
        m_sound._feedback()->set_volume(*vol);

    Fvector2 dist = m_object->CollideSndDist();
    if (dist.x >= 0.f || dist.y >= 0.f)
    {
        if (dist.x < 0.f)
            dist.x = m_sound.get_params()->min_distance;
        if (dist.y < 0.f)
            dist.y = m_sound.get_params()->max_distance;
        m_sound._feedback()->set_range(dist.x, dist.y);
    }
}

void CPHSoundPlayer::PlayNext(SGameMtlPair* mtl_pair, Fvector* pos, float* vol)
{
    if (m_next_snd_time > Device.dwTimeGlobal && m_last_mtl_pair == mtl_pair)
        return;

    auto snd = GET_RANDOM(mtl_pair->CollideSounds);
    // Половина от времени звука, поэтому умножаем не на 1000, а на 500.
    m_next_snd_time = Device.dwTimeGlobal + iFloor(snd.get_length_sec() * 500.0f);

    Fvector2* range = nullptr;
    Fvector2 dist = m_object->CollideSndDist();
    if (dist.x >= 0.f || dist.y >= 0.f)
    {
        if (dist.x < 0.f)
            dist.x = snd.get_params()->min_distance;
        if (dist.y < 0.f)
            dist.y = snd.get_params()->max_distance;
        range = &dist;
    }

    snd.play_no_feedback(nullptr, 0, 0, pos, vol, nullptr, range);
    m_last_mtl_pair = mtl_pair;
}
