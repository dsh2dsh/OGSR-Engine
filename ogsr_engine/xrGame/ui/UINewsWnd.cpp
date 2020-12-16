#include "stdafx.h"

#include "UINewsWnd.h"
#include "xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "../UI.h"
#include "../HUDManager.h"
#include "../level.h"
#include "../game_news.h"
#include "../actor.h"
#include "../alife_registry_wrappers.h"
#include "UIInventoryUtilities.h"
#include "UINewsItemWnd.h"
#include "UIScrollView.h"
#include "UIEditBox.h"

#define				NEWS_XML			"news.xml"

CUINewsWnd::CUINewsWnd() {
  UIEditSearch = nullptr;
}

CUINewsWnd::~CUINewsWnd()
{}

void CUINewsWnd::Init(LPCSTR xml_name, LPCSTR start_from)
{
	string512 pth, pth2;

	bool xml_result				= uiXml.Init(CONFIG_PATH, UI_PATH, xml_name);
	R_ASSERT3					(xml_result, "xml file not found", xml_name);
	CUIXmlInit xml_init;

	strconcat					(sizeof(pth),pth,start_from,"list");
	xml_init.InitWindow			(uiXml, pth, 0, this);

	UIScrollWnd = xr_new<CUIScrollView>();
	UIScrollWnd->SetAutoDelete( true );
	AttachChild( UIScrollWnd );
	strconcat( sizeof( pth2 ), pth2, start_from, "scroll_view");
	if ( uiXml.NavigateToNode( pth2, 0 ) )
	  xml_init.InitScrollView( uiXml, pth2, 0, UIScrollWnd );
	else
	  xml_init.InitScrollView( uiXml, pth,  0, UIScrollWnd );

	strconcat( sizeof( pth ), pth, start_from, "edit_search");
	if ( uiXml.NavigateToNode( pth, 0 ) ) {
	  UIEditSearch = xr_new<CUIEditBox>();
	  UIEditSearch->SetAutoDelete( true );
	  AttachChild( UIEditSearch );
	  xml_init.InitEditBox( uiXml, pth, 0, UIEditSearch );
	}
}

void CUINewsWnd::Init()
{
	Init				(NEWS_XML,"");
}

void CUINewsWnd::LoadNews() {
  UIScrollWnd->Clear();
  if ( Actor() ) {
    GAME_NEWS_VECTOR& news_vector = Actor()->game_news_registry->registry().objects();
    // Показать только NEWS_TO_SHOW последних ньюсов
    int currentNews = 0;
    for ( GAME_NEWS_VECTOR::reverse_iterator it = news_vector.rbegin(); it != news_vector.rend() && currentNews < Actor()->NewsToShow() ; ++it ) {
      AddNewsItem( *it );
      ++currentNews;
    }
  }
  m_flags.set( eNeedAdd, FALSE );
}

void CUINewsWnd::Update()
{
	inherited::Update		();	
	if(m_flags.test(eNeedAdd))
		LoadNews			();
}


void CUINewsWnd::AddNews() {
  if ( m_flags.test( eNeedAdd ) )
    return;

  while ( UIScrollWnd->GetSize() >= Actor()->NewsToShow() )
    UIScrollWnd->RemoveWindow( UIScrollWnd->GetItem( UIScrollWnd->GetSize() - 1 ) );

  if ( Actor() ) {
    GAME_NEWS_VECTOR& news_vector = Actor()->game_news_registry->registry().objects();
    AddNewsItem( news_vector.back(), true );
  }

  if ( m_search_str.size() )
    Search();
}


void CUINewsWnd::AddNewsItem( GAME_NEWS_DATA& news_data, bool top )
{
	CUIWindow*				itm = NULL;
	switch(news_data.m_type){
		case GAME_NEWS_DATA::eNews:{
			CUINewsItemWnd* _itm		= xr_new<CUINewsItemWnd>();
			_itm->Init( uiXml, "news_item" );
			_itm->Setup					(news_data);
			itm							= _itm;					   
		}break;
		case GAME_NEWS_DATA::eTalk:{
			CUINewsItemWnd* _itm		= xr_new<CUINewsItemWnd>();
			_itm->Init( uiXml, "talk_item" );
			_itm->Setup					(news_data);
			itm							= _itm;					   
		}break;
	};
	UIScrollWnd->AddWindow( itm, true, top );
}


void CUINewsWnd::Show( bool status ) {
  if ( status ) {
    if ( m_flags.test( eNeedAdd ) )
      LoadNews();
  }
  else {
    if ( m_search_str.size() ) {
      SetSearchStr( "" );
    }
    InventoryUtilities::SendInfoToActor( "ui_pda_news_hide" );
  }
  inherited::Show( status );
}


void CUINewsWnd::Reset() {
  inherited::Reset();
  m_flags.set( eNeedAdd, TRUE );
}


void CUINewsWnd::SendMessage( CUIWindow *pWnd, s16 msg, void *pData ) {
  if ( UIEditSearch && pWnd == UIEditSearch && msg == EDIT_TEXT_COMMIT ) {
    SetSearchStr( UIEditSearch->GetText() );
  }
  CUIWindow::SendMessage( pWnd, msg, pData );
}


void CUINewsWnd::SetSearchStr( LPCSTR s ) {
  m_search_str = s;
  std::transform( m_search_str.begin(), m_search_str.end(), m_search_str.begin(), ::tolower );
  Search();
}


void CUINewsWnd::Search() {
  for ( int i = 0; i < UIScrollWnd->GetSize(); i++ ) {
    CUINewsItemWnd* itm = (CUINewsItemWnd*)UIScrollWnd->GetItem( i );
    if ( m_search_str.size() == 0 ) {
      itm->SetVisible( true );
      continue;
    }
    std::string s1( itm->m_UIText->GetText() );
    std::string s2( itm->m_UITextDate->GetText() );
    std::transform( s1.begin(), s1.end(), s1.begin(), ::tolower );
    itm->SetVisible( s1.find( m_search_str ) == std::string::npos && s2.find( m_search_str ) == std::string::npos ? false : true );
  }
  UIScrollWnd->ForceUpdate();
  UIScrollWnd->ScrollToBegin();
}
