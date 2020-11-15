#pragma once

#include "UIWindow.h"
#include "xrUIXmlParser.h"
class CUIEditBox;
class CUIScrollView;
struct GAME_NEWS_DATA;

class CUINewsWnd: public CUIWindow
{
	typedef CUIWindow inherited;
	enum eFlag{eNeedAdd=(1<<0),};
	Flags16			m_flags;
	CUIXml uiXml;
	std::string m_search_str;

public:
					CUINewsWnd	();
	virtual			~CUINewsWnd	();

			void	Init		();
			void	Init		(LPCSTR xml_name, LPCSTR start_from);
	void			AddNews		();
	void			LoadNews		();
	virtual void	Show		(bool status);
	virtual void	Update		();
	virtual void		Reset						();
	virtual void SendMessage( CUIWindow *pWnd, s16 msg, void *pData );

	CUIScrollView*	UIScrollWnd;
	CUIEditBox* UIEditSearch;

	void SetSearchStr( LPCSTR s );

private:
  void AddNewsItem( GAME_NEWS_DATA& news_data, bool top = false );
  void Search();
};
