//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "items.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CItemKey : public CItem
{
	DECLARE_CLASS(CItemKey, CItem);
public:
	void Spawn(void)
	{
		Precache();
		SetModel("models/1187_keycard/1187_keycard.mdl");
		BaseClass::Spawn();
	}

	void Precache(void)
	{
		PrecacheModel("models/1187_keycard/1187_keycard.mdl");
	}

	bool MyTouch(CBasePlayer *pPlayer)
	{
		UTIL_Remove(this);
		return true;
	}
};

LINK_ENTITY_TO_CLASS(item_key, CItemKey);
