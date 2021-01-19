#include "stdafx.h"
#include "UICellItemFactory.h"
#include "UICellCustomItems.h"

CUICellItem* create_cell_item( CInventoryItem* itm ) {
  if ( itm->m_cell_item ) {
    itm->m_cell_item->ReuseItem();
    return itm->m_cell_item;
  }

  CWeaponAmmo* pAmmo = smart_cast<CWeaponAmmo*>( itm );
  if ( pAmmo )
    return xr_new<CUIAmmoCellItem>( pAmmo );

  CWeapon* pWeapon = smart_cast<CWeapon*>(itm);
  if ( pWeapon )
    return xr_new<CUIWeaponCellItem>( pWeapon );

  return xr_new<CUIInventoryCellItem>( itm );
}
