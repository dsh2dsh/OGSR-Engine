// Engine.cpp: implementation of the CEngine class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Engine.h"

CEngine Engine;

extern void msCreate(LPCSTR name);

void CEngine::Initialize()
{
    Engine.Sheduler.Initialize();

#ifdef DEBUG
    msCreate("game");
#endif
}

void CEngine::Destroy()
{
    Engine.Sheduler.Destroy();
    Engine.External.Destroy();
}
