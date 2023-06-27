#include "stdafx.h"

#include "SoundRender_Emitter.h"
#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

XRSOUND_API extern float psSoundCull;

inline u32 calc_cursor(const float& fTimeStarted, float& fTime, const float& fTimeTotal, const WAVEFORMATEX& wfx)
{
    if (fTime < fTimeStarted)
        fTime = fTimeStarted; // Андрюха посоветовал, ассерт что ниже вылетел из за паузы как то хитро
    R_ASSERT((fTime - fTimeStarted) >= 0.0f);
    while ((fTime - fTimeStarted) > fTimeTotal) // looped
    {
        fTime -= fTimeTotal;
    }
    u32 curr_sample_num = iFloor((fTime - fTimeStarted) * wfx.nSamplesPerSec);
    return curr_sample_num * (wfx.wBitsPerSample / 8) * wfx.nChannels;
}

void CSoundRender_Emitter::update(float dt)
{
    float fTime = SoundRender->fTimer_Value;
    float fDeltaTime = SoundRender->fTimer_Delta;

    VERIFY2(!!(owner_data) || (!(owner_data) && (m_current_state == stStopped)),
            "owner");
    VERIFY2(owner_data ? *(int*)(&owner_data->feedback) : 1, "owner");

    if (bRewind)
    {
        if (target)
            SoundRender->i_rewind(this);
        bRewind = FALSE;
    }

    switch (m_current_state)
    {
    case stStopped: break;
    case stStartingDelayed:
        if (iPaused)
            break;
        starting_delay -= dt;
        if (starting_delay <= 0)
            m_current_state = stStarting;
        break;
    case stStarting:
        if (iPaused)
            break;
        fTimeStarted = fTime;
        fTimeToStop = fTime + get_length_sec();
        fTimeToPropagade = fTime;
        initStartingVolumes();
        e_current = e_target = *SoundRender->get_environment(p_source.position);
        if (update_culling(dt))
        {
            m_current_state = stPlaying;
            set_cursor(0);
            SoundRender->i_start(this);
        }
        else
            m_current_state = stSimulating;
        break;
    case stStartingLoopedDelayed:
        if (iPaused)
            break;
        starting_delay -= dt;
        if (starting_delay <= 0)
            m_current_state = stStartingLooped;
        break;
    case stStartingLooped:
        if (iPaused)
            break;
        fTimeStarted = fTime;
        fTimeToStop = 0xffffffff;
        fTimeToPropagade = fTime;
        initStartingVolumes();
        e_current = e_target = *SoundRender->get_environment(p_source.position);
        if (update_culling(dt))
        {
            m_current_state = stPlayingLooped;
            set_cursor(0);
            SoundRender->i_start(this);
        }
        else
            m_current_state = stSimulatingLooped;
        break;
    case stPlaying:
        if (iPaused)
        {
            if (target)
            {
                SoundRender->i_stop(this);
                m_current_state = stSimulating;
            }
            fTimeStarted += fDeltaTime;
            fTimeToStop += fDeltaTime;
            fTimeToPropagade += fDeltaTime;
            break;
        }
        if (fTime >= fTimeToStop)
        {
            // STOP
            m_current_state = stStopped;
            SoundRender->i_stop(this);
        }
        else
        {
            if (!update_culling(dt))
            {
                // switch to: SIMULATE
                m_current_state = stSimulating; // switch state
                SoundRender->i_stop(this);
            }
            else
            {
                // We are still playing
                update_environment(dt);
            }
        }
        break;
    case stSimulating:
        if (iPaused)
        {
            fTimeStarted += fDeltaTime;
            fTimeToStop += fDeltaTime;
            fTimeToPropagade += fDeltaTime;
            break;
        }
        if (fTime >= fTimeToStop)
        {
            // STOP
            m_current_state = stStopped;
        }
        else
        {
            u32 ptr = calc_cursor(fTimeStarted, fTime, get_length_sec(),
                                  source()->m_wformat);
            set_cursor(ptr);

            if (update_culling(dt))
            {
                // switch to: PLAY
                m_current_state = stPlaying;
                /*
                                u32 ptr						= calc_cursor(
                   fTimeStarted, fTime, get_length_sec(), source()->m_wformat);
                                set_cursor					(ptr);
                */
                SoundRender->i_start(this);
            }
        }
        break;
    case stPlayingLooped:
        if (iPaused)
        {
            if (target)
            {
                SoundRender->i_stop(this);
                m_current_state = stSimulatingLooped;
            }
            fTimeStarted += fDeltaTime;
            fTimeToPropagade += fDeltaTime;
            break;
        }
        if (!update_culling(dt))
        {
            // switch to: SIMULATE
            m_current_state = stSimulatingLooped; // switch state
            SoundRender->i_stop(this);
        }
        else
        {
            // We are still playing
            update_environment(dt);
        }
        break;
    case stSimulatingLooped:
        if (iPaused)
        {
            fTimeStarted += fDeltaTime;
            fTimeToPropagade += fDeltaTime;
            break;
        }
        if (update_culling(dt))
        {
            // switch to: PLAY
            m_current_state = stPlayingLooped; // switch state
            u32 ptr = calc_cursor(fTimeStarted, fTime, get_length_sec(),
                                  source()->m_wformat);
            set_cursor(ptr);

            SoundRender->i_start(this);
        }
        break;
    }

    // if deffered stop active and volume==0 -> physically stop sound
    if (bStopping && fis_zero(fade_volume))
        i_stop();

    VERIFY2(!!(owner_data) || (!(owner_data) && (m_current_state == stStopped)),
            "owner");
    VERIFY2(owner_data ? *(int*)(owner_data->feedback) : 1, "owner");

    // footer
    bMoved = FALSE;
    if (m_current_state != stStopped)
    {
        if (fTime >= fTimeToPropagade)
            Event_Propagade();
    }
    else if (owner_data)
    {
        VERIFY(this == owner_data->feedback);
        owner_data->feedback = 0;
        owner_data = 0;
    }
}

IC void volume_lerp(float& c, float t, float s, float dt)
{
    float diff = t - c;
    float diff_a = _abs(diff);
    if (diff_a < EPS_S)
        return;
    float mot = s * dt;
    if (mot > diff_a)
        mot = diff_a;
    c += (diff / diff_a) * mot;
}

// #include "..\COMMON_AI\ai_sounds.h"
BOOL CSoundRender_Emitter::update_culling(float dt)
{
    if (b2D)
    {
        occluder_volume = 1.f;
        fade_volume += dt * psSoundFadeSpeed * (bStopping ? -1.f : 1.f);
    }
    else
    {
        // Update occlusion
        updateOccVolume(dt);

        // Calc attenuated volume
        float fade_dir =
            bStopping || (att() * applyOccVolume()) < psSoundCull ? -1.f : 1.f;
        fade_volume += dt * psSoundFadeSpeed * fade_dir;

        // Если звук уже звучит и мы от него отошли дальше его максимальной
        // дистанции, не будем обрывать его резко. Пусть громкость плавно
        // снизится до минимального и потом уже выключается. А вот если звук еще
        // даже и не начинал звучать, то можно сразу отсекать его по расстоянию.
        // Ничего внезапно не замолчит, т.к. еще ничего и не было слышно. Звучит
        // звук или не звучит - будем определять по наличию target, т.е. тому,
        // что обеспечивает физическое звучание.
        if (!target && fade_dir < 0.f)
        {
            smooth_volume = 0;
            return FALSE;
        }
    }
    clamp(fade_volume, 0.f, 1.f);
    // Update smoothing
    smooth_volume = .9f * smooth_volume + .1f * applyOccVolume() * fade_volume;
    SmoothHfVolume =
        .9f * SmoothHfVolume + .1f * applyOccHfVolume() * fade_volume;
    if (smooth_volume < psSoundCull)
        return FALSE; // allow volume to go up
    // Here we has enought "PRIORITY" to be soundable
    // If we are playing already, return OK
    // --- else check availability of resources
    if (target)
        return TRUE;
    else
        return SoundRender->i_allow_play(this);
}

float CSoundRender_Emitter::priority()
{
    return smooth_volume * att() * priority_scale;
}

/*
The AL_INVERSE_DISTANCE_CLAMPED model works according to the following formula:

distance = max(distance,AL_REFERENCE_DISTANCE);
distance = min(distance,AL_MAX_DISTANCE);
gain = AL_REFERENCE_DISTANCE / (AL_REFERENCE_DISTANCE +
 AL_ROLLOFF_FACTOR *
 (distance – AL_REFERENCE_DISTANCE));
*/
float CSoundRender_Emitter::att()
{
    float dist =
        SoundRender->listener_position().distance_to(p_source.position);

    if (dist < p_source.min_distance)
        dist = p_source.min_distance;
    else if (dist > p_source.max_distance)
        return 0.f; //dist = p_source.max_distance;
    float att = p_source.min_distance /
        (p_source.min_distance +
         psSoundRolloff * (dist - p_source.min_distance));
    clamp(att, 0.f, 1.f);

    return att;
}

void CSoundRender_Emitter::update_environment(float dt)
{
    if (bMoved)
        e_target = *SoundRender->get_environment(p_source.position);
    e_current.lerp(e_current, e_target, dt);
}

float CSoundRender_Emitter::applyOccVolume()
{
    float vol = p_source.base_volume * p_source.volume *
        (owner_data->s_type == st_Effect ? psSoundVEffects * psSoundVFactor :
                                           psSoundVMusic);

    if (!b2D)
    {
        if (fis_zero(occluder_volume))
            return 0.f;
        if (psSoundOcclusionScale < 1.f && occluder.valid)
            vol *= psSoundOcclusionScale;
        if (psSoundOcclusionMtl > 0.f && occluder_volume < 1.f)
            vol = vol * (1.f - psSoundOcclusionMtl) +
                vol * psSoundOcclusionMtl * occluder_volume;
    }

    return vol;
}

float CSoundRender_Emitter::applyOccHfVolume()
{
    if (psSoundOcclusionHf > 0.f && occluder_volume < 1.f)
        return 1.f - psSoundOcclusionHf + occluder_volume * psSoundOcclusionHf;
    return 1.f;
}

void CSoundRender_Emitter::initStartingVolumes()
{
    fade_volume = 1.f;
    occluder_volume = (b2D || att() < psSoundCull) ?
        1.f :
        SoundRender->get_occlusion(p_source.position, .2f, &occluder, this);
    smooth_volume = applyOccVolume();
    SmoothHfVolume = applyOccHfVolume();
}

void CSoundRender_Emitter::updateOccVolume(float dt)
{
    if (target)
    {
        // Звук воспроизводится или пока ещё воспроизводится
        float occ =
            SoundRender->get_occlusion(p_source.position, .2f, &occluder, this);
        volume_lerp(occluder_volume, occ, 1.f, dt);
    }
    else
    {
        // Или звук только что запустили, или его (пока ещё?) не слышно. Нужно
        // проверить, если его не слышно потому, что он слишком далеко, тогда не
        // будем зря дёргать `get_occlusion`. `occluder_volume` нужно установить
        // сразу, а не через `volume_lerp`, что бы, если звук зазвучит на
        // следующем кадре, он сразу с правильной громкостью начал
        // воспроизводится. По этой же причине сразу установим `SmoothHfVolume`,
        // что бы высокие частоты правильную громкость имели.
        occluder_volume = (att() < psSoundCull) ?
            1.f :
            SoundRender->get_occlusion(p_source.position, .2f, &occluder, this);
        SmoothHfVolume = applyOccHfVolume();
    }
    clamp(occluder_volume, 0.f, 1.f);
}
