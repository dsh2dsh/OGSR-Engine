#include "stdafx.h"
#include "igame_level.h"
#include "igame_persistent.h"

#include "x_ray.h"
#include "std_classes.h"
#include "customHUD.h"
#include "render.h"
#include "gamefont.h"
#include "xrLevel.h"
#include "CameraManager.h"
#include "gamemtllib.h"

ENGINE_API	IGame_Level*	g_pGameLevel	= NULL;

IGame_Level::IGame_Level	()
{
	m_pCameras					= xr_new<CCameraManager>(true);
	g_pGameLevel				= this;
	pLevel						= NULL;
	bReady						= false;
	pCurrentEntity				= NULL;
	pCurrentViewEntity			= NULL;
	pHUD						= NULL;
}

//#include "resourcemanager.h"

IGame_Level::~IGame_Level	()
{
	/*
	if(strstr(Core.Params,"-nes_texture_storing") )
		Device.Resources->StoreNecessaryTextures();
	*/

//.	DEL_INSTANCE				( pHUD			);
	xr_delete					( pLevel		);

	// Render-level unload
	Render->level_Unload		();
	xr_delete					(m_pCameras);
	// Unregister
	Device.seqRender.Remove		(this);
	Device.seqFrame.Remove		(this);
	CCameraManager::ResetPP		();
}

void IGame_Level::net_Stop			()
{
	for (int i=0; i<6; i++)
		Objects.Update(true);
	// Destroy all objects
	Objects.Unload				( );
	IR_Release					( );

	bReady						= false;	
}

//-------------------------------------------------------------------------------------------
//extern CStatTimer				tscreate;

BOOL IGame_Level::Load			(u32 dwNum) 
{
	// Initialize level data
	pApp->Level_Set				( dwNum );
	string_path					temp;
	if (!FS.exist(temp, "$level$", "level.ltx"))
		Debug.fatal	(DEBUG_INFO,"Can't find level configuration file '%s'.",temp);
	pLevel						= xr_new<CInifile>	( temp );
	
	// Open
	g_pGamePersistent->LoadTitle	("st_opening_stream");
	IReader* LL_Stream			= FS.r_open	("$level$","level");
	IReader	&fs					= *LL_Stream;

	// Header
	hdrLEVEL					H;
	fs.r_chunk_safe				(fsL_HEADER,&H,sizeof(H));
	R_ASSERT2					(XRCL_PRODUCTION_VERSION==H.XRLC_version,"Incompatible level version.");

	// CForms
	g_pGamePersistent->LoadTitle	("st_loading_cform");
	ObjectSpace.Load			();

	// HUD + Environment
//.	pHUD						= (CCustomHUD*)NEW_INSTANCE	(CLSID_HUDMANAGER);
	if(g_hud)
		pHUD					= g_hud;
	else
		pHUD					= (CCustomHUD*)NEW_INSTANCE	(CLSID_HUDMANAGER);

	// Render-level Load
	Render->level_Load			(LL_Stream);
	//tscreate.FrameEnd			();
	// Msg						("* S-CREATE: %f ms, %d times",tscreate.result,tscreate.count);

	// Objects
	g_pGamePersistent->Environment().mods_load	();
	R_ASSERT					(Load_GameSpecific_Before());
	Objects.Load				();

	// Done
	FS.r_close					( LL_Stream );
	bReady						= true;
	IR_Capture();
	Device.seqRender.Add		(this);
	Device.seqFrame.Add			(this);

	return TRUE;
}

void	IGame_Level::OnRender		( ) 
{
//	if (_abs(Device.fTimeDelta)<EPS_S) return;

	// Level render, only when no client output required
		Render->Calculate			();
		Render->Render				();

	// Font
//	pApp->pFontSystem->SetSizeI(0.023f);
//	pApp->pFontSystem->OnRender	();
}

void	IGame_Level::OnFrame		( ) 
{
	// Log				("- level:on-frame: ",u32(Device.dwFrame));
//	if (_abs(Device.fTimeDelta)<EPS_S) return;

	// Update all objects
	VERIFY						(bReady);
	Objects.Update				( false );
	pHUD->OnFrame				( );

	// Ambience
	if (Sounds_Random.size() && (Device.dwTimeGlobal > Sounds_Random_dwNextTime))
	{
		Sounds_Random_dwNextTime		= Device.dwTimeGlobal + ::Random.randI	(10000,20000);
		Fvector	pos;
		pos.random_dir().normalize().mul(::Random.randF(30,100)).add	(Device.vCameraPosition);
		int		id						= ::Random.randI(Sounds_Random.size());
		if (Sounds_Random_Enabled)		{
			Sounds_Random[id].play_at_pos	(0,pos,0);
			Sounds_Random[id].set_volume	(1.f);
			Sounds_Random[id].set_range		(10,200);
		}
	}
}


ICF static BOOL info_trace_callback( collide::rq_result& result, LPVOID params ) {
  bool& bIndoor	= *(bool*)params;
  if ( result.O ) {
    if ( g_pGameLevel->CurrentViewEntity() == result.O )
      return TRUE;
  }
  else {
    CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + result.element;
    const auto* mtl = GMLib.GetMaterialByIdx( T->material );
    if ( mtl->Flags.test( SGameMtl::flPassable | SGameMtl::flOutdoor ) )
      return TRUE;
  }	
  bIndoor = true;
  return FALSE;
}

bool IGame_Level::IsActorIndoor() {
  static bool bIndoor     = true;
  static u32  last_frame  = 0;
  static collide::rq_results RQR;
  if ( last_frame != Device.dwFrame ) {
    collide::ray_defs RD( Device.vCameraPosition, Fvector().set( 0.f, 1.f, 0.f ), 50.f, CDB::OPT_CULL, collide::rqtBoth );
    RQR.r_clear();
    ObjectSpace.RayQuery( RQR, RD, info_trace_callback, &bIndoor, NULL, CurrentViewEntity() );
    last_frame = Device.dwFrame;
  }
  return bIndoor;
}

// ==================================================================================================

void CServerInfo::AddItem( LPCSTR name_, LPCSTR value_, u32 color_ )
{
	shared_str s_name( name_ );
	AddItem( s_name, value_, color_ );
}

void CServerInfo::AddItem( shared_str& name_, LPCSTR value_, u32 color_ )
{
	SItem_ServerInfo it;
//	shared_str s_name = CStringTable().translate( name_ );
	
//	strcpy_s( it.name, s_name.c_str() );
	strcpy_s( it.name, name_.c_str() );
	strcat_s( it.name, " = " );
	strcat_s( it.name, value_ );
	it.color = color_;

	if ( data.size() < max_item )
	{
		data.push_back( it );
	}
}
