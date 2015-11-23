//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements a sniper rifle weapon.
//			
//			Primary attack: fires a single high-powered shot, then reloads.
//			Secondary attack: cycles sniper scope through zoom levels.
//
// TODO: Circular mask around crosshairs when zoomed in.
// TODO: Shell ejection.
// TODO: Finalize kickback.
// TODO: Animated zoom effect?
//
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"				// For g_pGameRules
#include "in_buttons.h"
#include "soundent.h"
#include "vstdlib/random.h"

#if defined ( HUMANERROR_DLL )
#include "hl2_player.h"
#include "gamestats.h"
#include "ammodef.h"

#include "Human_Error/hlss_weapon_id.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define SNIPER_CONE_PLAYER					vec3_origin	// Spread cone when fired by the player.
#define SNIPER_CONE_NPC						vec3_origin	// Spread cone when fired by NPCs.
#define SNIPER_BULLET_COUNT_PLAYER			1			// Fire n bullets per shot fired by the player.
#define SNIPER_BULLET_COUNT_NPC				1			// Fire n bullets per shot fired by NPCs.
#define SNIPER_TRACER_FREQUENCY_PLAYER		0			// Draw a tracer every nth shot fired by the player.
#define SNIPER_TRACER_FREQUENCY_NPC			0			// Draw a tracer every nth shot fired by NPCs.
#define SNIPER_KICKBACK						3			// Range for punchangle when firing.

#define SNIPER_ZOOM_RATE					0.2			// Interval between zoom levels in seconds.

#if defined ( HUMANERROR_DLL )
ConVar sk_sniper_battery_needed("sk_sniper_battery_needed", "0");

#define SNIPERRIFLE_POWER_NEEDED sk_sniper_battery_needed.GetInt()

extern ConVar hl2_normspeed;
extern ConVar hl2_walkspeed;
#endif

//-----------------------------------------------------------------------------
// Discrete zoom levels for the scope.
//-----------------------------------------------------------------------------
static int g_nZoomFOV[] =
{
	20,
	5
};

#if defined ( HUMANERROR_DLL )
class CWeaponSniperRifle : public CBaseHLCombatWeapon
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWeaponSniperRifle, CBaseHLCombatWeapon );

	CWeaponSniperRifle(void);

	virtual const int		HLSS_GetWeaponId() { return HLSS_WEAPON_ID_SNIPER; }

	DECLARE_SERVERCLASS();

	void Precache( void );

	int CapabilitiesGet( void ) const;

	//const Vector &GetBulletSpread( void );

	virtual bool		VisibleInWeaponSelection( void ) { return true; }
	virtual bool		CanBeSelected( void ) { return true; }
	virtual bool		HasAnyAmmo( void ) { return true; }
	virtual bool		HasAmmo( void ) {  return true; }

	bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	void Drop( const Vector &vecVelocity );
	void ItemPostFrame( void );
	void PrimaryAttack( void );
	bool Reload( void );
	void Zoom( bool bForceOut = false);
	virtual float GetFireRate( void ) { return 1; };

	bool DecreamentEnergy( CBasePlayer *pOwner );
	bool NeedsEnergy( CBasePlayer *pOwner );

	bool Deploy(); 

	virtual const Vector& GetBulletSpread( void )
	{
		if (m_nZoomLevel == 0)
		{
			static const Vector cone = VECTOR_CONE_6DEGREES;
			return cone;
		}

		static const Vector zoomedCone = SNIPER_CONE_PLAYER;
		return zoomedCone;
	}

	void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );


	DECLARE_ACTTABLE();

protected:
	
	bool m_bCharging;
	bool m_bShouldShowRechargeHint;

	CNetworkVar( float, m_fNextZoom );
	CNetworkVar( int, m_nZoomLevel );

	float m_flShouldZoomOut;
};

IMPLEMENT_SERVERCLASS_ST(CWeaponSniperRifle, DT_WeaponSniperRifle)
	SendPropFloat(SENDINFO(m_fNextZoom)),
	SendPropInt(SENDINFO(m_nZoomLevel), SPROP_CHANGES_OFTEN),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_sniperrifle, CWeaponSniperRifle );
PRECACHE_WEAPON_REGISTER(weapon_sniperrifle);

BEGIN_DATADESC( CWeaponSniperRifle )

	DEFINE_FIELD( m_fNextZoom,					FIELD_TIME ),
	DEFINE_FIELD( m_nZoomLevel,					FIELD_INTEGER ),
	DEFINE_FIELD( m_bCharging,					FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bShouldShowRechargeHint,	FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flShouldZoomOut,			FIELD_TIME ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Maps base activities to weapons-specific ones so our characters do the right things.
//-----------------------------------------------------------------------------
acttable_t	CWeaponSniperRifle::m_acttable[] = 
{
	{	ACT_RANGE_ATTACK1, ACT_RANGE_ATTACK_SNIPER_RIFLE, true }
};

IMPLEMENT_ACTTABLE(CWeaponSniperRifle);


//-----------------------------------------------------------------------------
// Purpose: Constructor.
//-----------------------------------------------------------------------------
CWeaponSniperRifle::CWeaponSniperRifle( void )
{
	m_fNextZoom = gpGlobals->curtime;
	m_nZoomLevel = 0;
	m_flShouldZoomOut = 0;

	SetPrimaryAmmoCount( 3 );

	m_bReloadsSingly = true;

	m_fMinRange1		= 65;
	m_fMinRange2		= 65;
	m_fMaxRange1		= 2048;
	m_fMaxRange2		= 2048;

	m_bCharging = false;

	m_bShouldShowRechargeHint = true;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CWeaponSniperRifle::CapabilitiesGet( void ) const
{
	return bits_CAP_WEAPON_RANGE_ATTACK1;
}

void CWeaponSniperRifle::Drop( const Vector &vecVelocity )
{
	if (m_nZoomLevel != 0)
	{
		Zoom( true );
	}

	BaseClass::Drop( vecVelocity );
}


//-----------------------------------------------------------------------------
// Purpose: Turns off the zoom when the rifle is holstered.
//-----------------------------------------------------------------------------
bool CWeaponSniperRifle::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	/*CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (pPlayer != NULL)
	{
		/*if ( m_nZoomLevel != 0 )
		{
			if ( pPlayer->SetFOV( this, 0 ) )
			{
				pPlayer->ShowViewModel(true);		
				m_nZoomLevel = 0;
			}
		}
		Zoom( true );
	}*/

	if (m_nZoomLevel != 0)
	{
		Zoom( true );
	}

	return BaseClass::Holster(pSwitchingTo);
}


//-----------------------------------------------------------------------------
// Purpose: Overloaded to handle the zoom functionality.
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::ItemPostFrame( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (pPlayer == NULL)
	{
		return;
	}

	if (m_flShouldZoomOut != 0 && m_flShouldZoomOut < gpGlobals->curtime )
	{
		if (m_nZoomLevel != 0)
		{
			Zoom( true );
		}

		m_flShouldZoomOut = 0;
	}

	if ((m_bInReload) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
	{
		FinishReload();
		m_bInReload = false;

		WeaponSound(RELOAD);

		SendWeaponAnim( ACT_VM_THROW );

		m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration();
	}
	else if (m_bCharging)
	{
		if (m_flNextPrimaryAttack <= gpGlobals->curtime)
		{
			m_bCharging = DecreamentEnergy( pPlayer );

			if (m_bCharging && NeedsEnergy( pPlayer ))
			{
				m_flNextPrimaryAttack = gpGlobals->curtime + 0.8f;
			}
			else
			{
				m_bCharging = false;
			}
		}

		if ( pPlayer->m_nButtons & IN_RELOAD && m_bCharging )
		{
			
		}
		else
		{
			m_bCharging = false;

			SendWeaponAnim( ACT_VM_THROW );

			m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration();
		}
	}
	else if (!m_bInReload && pPlayer->m_nButtons & IN_ATTACK2)
	{
		if (m_fNextZoom <= gpGlobals->curtime && m_flShouldZoomOut == 0)
		{
			Zoom();
			pPlayer->m_nButtons &= ~IN_ATTACK2;
		}
	}
	else if ((pPlayer->m_nButtons & IN_ATTACK) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
	{
		if ( (m_iClip1 == 0 && UsesClipsForAmmo1()) || ( !UsesClipsForAmmo1() && !pPlayer->GetAmmoCount(m_iPrimaryAmmoType) ) )
		{
			m_bFireOnEmpty = true;
		}

		// Fire underwater?
		if (pPlayer->GetWaterLevel() == 3 && m_bFiresUnderwater == false)
		{
			WeaponSound(EMPTY);
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.2;
			return;
		}
		else
		{
			// If the firing button was just pressed, reset the firing time
			if ( pPlayer && pPlayer->m_afButtonPressed & IN_ATTACK )
			{
				 m_flNextPrimaryAttack = gpGlobals->curtime;
			}

			PrimaryAttack();

			/*if (m_nZoomLevel != 0)
			{
				Zoom( true );
			}*/
		}
	}

	// -----------------------
	//  Reload pressed / Clip Empty
	// -----------------------
	if ( pPlayer->m_nButtons & IN_RELOAD && UsesClipsForAmmo1() && !m_bInReload && !m_bCharging) 
	{
		// reload when reload is pressed, or if no buttons are down and weapon is empty.
		Reload();

		if (!m_bInReload && m_flNextPrimaryAttack <= gpGlobals->curtime && NeedsEnergy( pPlayer ))
		{
			if (m_nZoomLevel != 0)
			{
				Zoom( true );
			}

			
			SendWeaponAnim( ACT_VM_PULLBACK );

			//m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration() - 0.1f;

			m_bCharging = true;
		}
	}

	// -----------------------
	//  No buttons down
	// -----------------------
	if (!((pPlayer->m_nButtons & IN_ATTACK) || (pPlayer->m_nButtons & IN_ATTACK2) || (pPlayer->m_nButtons & IN_RELOAD)))
	{
		// no fire buttons down
		m_bFireOnEmpty = false;

		if ( !HasAnyAmmo() && m_flNextPrimaryAttack < gpGlobals->curtime ) 
		{
			// weapon isn't useable, switch.
			if ( !(GetWeaponFlags() & ITEM_FLAG_NOAUTOSWITCHEMPTY) && pPlayer->SwitchToNextBestWeapon( this ) )
			{
				m_flNextPrimaryAttack = gpGlobals->curtime + 0.3;
				return;
			}
		}
		else
		{
			// weapon is useable. Reload if empty and weapon has waited as long as it has to after firing
			if ( m_iClip1 == 0 && !(GetWeaponFlags() & ITEM_FLAG_NOAUTORELOAD) && m_flNextPrimaryAttack < gpGlobals->curtime )
			{
				Reload();
				return;
			}
		}

		WeaponIdle( );
		return;
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::Precache( void )
{
	BaseClass::Precache();

	PrecacheScriptSound("SuitRecharge.Deny");

	PrecacheModel("effects/bluelaser1.vmt");	
	PrecacheModel("sprites/light_glow03.vmt");
}


//-----------------------------------------------------------------------------
// Purpose: Same as base reload but doesn't change the owner's next attack time. This
//			lets us zoom out while reloading. This hack is necessary because our
//			ItemPostFrame is only called when the owner's next attack time has
//			expired.
// Output : Returns true if the weapon was reloaded, false if no more ammo.
//-----------------------------------------------------------------------------
bool CWeaponSniperRifle::Reload( void )
{
	CBaseCombatCharacter *pOwner = GetOwner();
	if (!pOwner)
	{
		return false;
	}
		
	if (pOwner->GetAmmoCount(m_iPrimaryAmmoType) > 0)
	{
		int primary		= min(GetMaxClip1() - m_iClip1, pOwner->GetAmmoCount(m_iPrimaryAmmoType));
		//int secondary	= min(GetMaxClip2() - m_iClip2, pOwner->GetAmmoCount(m_iSecondaryAmmoType));

		if (primary) // > 0 || secondary > 0)
		{
			// Play reload on different channel as it happens after every fire
			// and otherwise steals channel away from fire sound
		
			SendWeaponAnim( ACT_VM_PULLBACK );

			m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration();

			m_bInReload = true;

			if (m_nZoomLevel != 0)
			{
				Zoom( true );
			}
		}

		return true;
	}

	if (m_bShouldShowRechargeHint)
	{
		UTIL_HudHintText( pOwner, "#HLSS_Recharge_SniperRifle" );
	}

	return false;
}

bool CWeaponSniperRifle::Deploy()
{
	if (m_bShouldShowRechargeHint)
	{
		CBaseCombatCharacter *pOwner = GetOwner();

		if (pOwner)
		{
			UTIL_HudHintText( pOwner, "#HLSS_Recharge_SniperRifle" );
		}
	}

	Zoom( true );

	return BaseClass::Deploy();
}

bool CWeaponSniperRifle::DecreamentEnergy( CBasePlayer *pOwner )
{
	int iCount = pOwner->GetAmmoCount(m_iPrimaryAmmoType);

	if ( iCount < GetAmmoDef()->MaxCarry( m_iPrimaryAmmoType ))
	{
		if ( pOwner->ArmorValue() >= SNIPERRIFLE_POWER_NEEDED)
		{
			pOwner->DecrementArmorValue( SNIPERRIFLE_POWER_NEEDED );

			WeaponSound(RELOAD);

			pOwner->SetAmmoCount( iCount + 1, m_iPrimaryAmmoType );

			m_bShouldShowRechargeHint = false;

			return true;
		}
		else
		{
			EmitSound("SuitRecharge.Deny");

			m_flNextPrimaryAttack = gpGlobals->curtime + 0.1f;
		}
	}

	return false;
}

bool CWeaponSniperRifle::NeedsEnergy( CBasePlayer *pOwner )
{
	int iCount = pOwner->GetAmmoCount(m_iPrimaryAmmoType);

	if ( iCount < GetAmmoDef()->MaxCarry( m_iPrimaryAmmoType ) )
	{
		if (pOwner->ArmorValue() >= SNIPERRIFLE_POWER_NEEDED)
		{
			return true;
		}
		else
		{
			if (m_flNextPrimaryAttack < gpGlobals->curtime)
			{
				m_flNextPrimaryAttack = gpGlobals->curtime + 0.3f;
			}
			EmitSound("SuitRecharge.Deny");
		}
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::PrimaryAttack( void )
{
	// Only the player fires this way so we can cast safely.
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (!pPlayer)
	{
		return;
	}

	if ( gpGlobals->curtime >= m_flNextPrimaryAttack )
	{
		// If my clip is empty (and I use clips) start reload
		if ( !m_iClip1 ) 
		{
			Reload();
			return;
		}

		m_flShouldZoomOut = gpGlobals->curtime + 0.5f;

		// MUST call sound before removing a round from the clip of a CMachineGun dvs: does this apply to the sniper rifle? I don't know.
		WeaponSound(SINGLE);

		pPlayer->DoMuzzleFlash();

		SendWeaponAnim( ACT_VM_PRIMARYATTACK );

		// player "shoot" animation
		pPlayer->SetAnimation( PLAYER_ATTACK1 );

		// Don't fire again until fire animation has completed
		m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();
		m_iClip1 = m_iClip1 - 1;

		Vector vecSrc	 = pPlayer->Weapon_ShootPosition();
		Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );	

		// Fire the bullets
		pPlayer->FireBullets( SNIPER_BULLET_COUNT_PLAYER, vecSrc, vecAiming, GetBulletSpread(), MAX_TRACE_LENGTH, m_iPrimaryAmmoType, SNIPER_TRACER_FREQUENCY_PLAYER );

		CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), 600, 0.2 );

		QAngle vecPunch(random->RandomFloat( -SNIPER_KICKBACK, SNIPER_KICKBACK ), 0, 0);
		pPlayer->ViewPunch(vecPunch);
		
		Vector forward;
		GetVectors( &forward, NULL, NULL );
		forward *= -200.0f;
		pPlayer->VelocityPunch( forward );

		// Indicate out of ammo condition if we run out of ammo.
		if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
		{
			pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0); 
		}
	}

	// Register a muzzleflash for the AI.
	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );
}


//-----------------------------------------------------------------------------
// Purpose: Zooms in using the sniper rifle scope.
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::Zoom( bool bForceOut )
{
	CHL2_Player *pPlayer = (CHL2_Player *)ToBasePlayer( GetOwner() );
	if (!pPlayer)
	{
		return;
	}

	if ( !bForceOut && m_flShouldZoomOut != 0 && m_flShouldZoomOut > gpGlobals->curtime )
		return;

	if ( bForceOut || (m_nZoomLevel >= sizeof(g_nZoomFOV) / sizeof(g_nZoomFOV[0])))
	{
		if ( pPlayer->SetFOV( this, 0 ) )
		{
			pPlayer->ShowViewModel(true);
			
			// Zoom out to the default zoom level
			if (!bForceOut)
			{
				WeaponSound(SPECIAL2);	
			}
			m_nZoomLevel = 0;

			m_flShouldZoomOut = 0;

			pPlayer->EnableSprint( true );
			pPlayer->SetMaxSpeed( hl2_normspeed.GetFloat() );
		}
	}
	else
	{
		if ( pPlayer->SetFOV( this, g_nZoomFOV[m_nZoomLevel] ) )
		{
			if (m_nZoomLevel == 0)
			{
				pPlayer->ShowViewModel(false);

				pPlayer->EnableSprint( false );
				pPlayer->SetMaxSpeed( hl2_walkspeed.GetFloat() * 0.6f );
			}

			WeaponSound(SPECIAL1);
			
			m_nZoomLevel++;
		}
	}

	m_fNextZoom = gpGlobals->curtime + SNIPER_ZOOM_RATE;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : virtual const Vector&
//-----------------------------------------------------------------------------
/*const Vector &CWeaponSniperRifle::GetBulletSpread( void )
{
	static Vector cone = SNIPER_CONE_PLAYER;
	return cone;
}*/


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEvent - 
//			*pOperator - 
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	switch ( pEvent->event )
	{
		case EVENT_WEAPON_SNIPER_RIFLE_FIRE:
		{
			Vector vecShootOrigin, vecShootDir;
			vecShootOrigin = pOperator->Weapon_ShootPosition();

			CAI_BaseNPC *npc = pOperator->MyNPCPointer();
			Vector vecSpread;
			if (npc)
			{
				vecShootDir = npc->GetActualShootTrajectory( vecShootOrigin );
				vecSpread = VECTOR_CONE_PRECALCULATED;
			}
			else
			{
				AngleVectors( pOperator->GetLocalAngles(), &vecShootDir );
				vecSpread = GetBulletSpread();
			}
			WeaponSound( SINGLE_NPC );
			pOperator->FireBullets( SNIPER_BULLET_COUNT_NPC, vecShootOrigin, vecShootDir, vecSpread, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, SNIPER_TRACER_FREQUENCY_NPC );
			pOperator->DoMuzzleFlash();
			break;
		}

		default:
		{
			BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
			break;
		}
	}
}
#else
class CWeaponSniperRifle : public CBaseHLCombatWeapon
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWeaponSniperRifle, CBaseHLCombatWeapon );

	CWeaponSniperRifle(void);

	DECLARE_SERVERCLASS();

	void Precache( void );

	int CapabilitiesGet( void ) const;

	const Vector &GetBulletSpread( void );

	bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	void ItemPostFrame( void );
	void PrimaryAttack( void );
	bool Reload( void );
	void Zoom( void );
	virtual float GetFireRate( void ) { return 1; };

	void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );

	DECLARE_ACTTABLE();

protected:

	float m_fNextZoom;
	int m_nZoomLevel;
};

IMPLEMENT_SERVERCLASS_ST(CWeaponSniperRifle, DT_WeaponSniperRifle)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_sniperrifle, CWeaponSniperRifle );
PRECACHE_WEAPON_REGISTER(weapon_sniperrifle);

BEGIN_DATADESC( CWeaponSniperRifle )

	DEFINE_FIELD( m_fNextZoom, FIELD_FLOAT ),
	DEFINE_FIELD( m_nZoomLevel, FIELD_INTEGER ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Maps base activities to weapons-specific ones so our characters do the right things.
//-----------------------------------------------------------------------------
acttable_t	CWeaponSniperRifle::m_acttable[] = 
{
	{	ACT_RANGE_ATTACK1, ACT_RANGE_ATTACK_SNIPER_RIFLE, true }
};

IMPLEMENT_ACTTABLE(CWeaponSniperRifle);


//-----------------------------------------------------------------------------
// Purpose: Constructor.
//-----------------------------------------------------------------------------
CWeaponSniperRifle::CWeaponSniperRifle( void )
{
	m_fNextZoom = gpGlobals->curtime;
	m_nZoomLevel = 0;

	m_bReloadsSingly = true;

	m_fMinRange1		= 65;
	m_fMinRange2		= 65;
	m_fMaxRange1		= 2048;
	m_fMaxRange2		= 2048;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CWeaponSniperRifle::CapabilitiesGet( void ) const
{
	return bits_CAP_WEAPON_RANGE_ATTACK1;
}



//-----------------------------------------------------------------------------
// Purpose: Turns off the zoom when the rifle is holstered.
//-----------------------------------------------------------------------------
bool CWeaponSniperRifle::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (pPlayer != NULL)
	{
		if ( m_nZoomLevel != 0 )
		{
			if ( pPlayer->SetFOV( this, 0 ) )
			{
				pPlayer->ShowViewModel(true);		
				m_nZoomLevel = 0;
			}
		}
	}

	return BaseClass::Holster(pSwitchingTo);
}


//-----------------------------------------------------------------------------
// Purpose: Overloaded to handle the zoom functionality.
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::ItemPostFrame( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (pPlayer == NULL)
	{
		return;
	}

	if ((m_bInReload) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
	{
		FinishReload();
		m_bInReload = false;
	}

	if (pPlayer->m_nButtons & IN_ATTACK2)
	{
		if (m_fNextZoom <= gpGlobals->curtime)
		{
			Zoom();
			pPlayer->m_nButtons &= ~IN_ATTACK2;
		}
	}
	else if ((pPlayer->m_nButtons & IN_ATTACK) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
	{
		if ( (m_iClip1 == 0 && UsesClipsForAmmo1()) || ( !UsesClipsForAmmo1() && !pPlayer->GetAmmoCount(m_iPrimaryAmmoType) ) )
		{
			m_bFireOnEmpty = true;
		}

		// Fire underwater?
		if (pPlayer->GetWaterLevel() == 3 && m_bFiresUnderwater == false)
		{
			WeaponSound(EMPTY);
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.2;
			return;
		}
		else
		{
			// If the firing button was just pressed, reset the firing time
			if ( pPlayer && pPlayer->m_afButtonPressed & IN_ATTACK )
			{
				 m_flNextPrimaryAttack = gpGlobals->curtime;
			}

			PrimaryAttack();
		}
	}

	// -----------------------
	//  Reload pressed / Clip Empty
	// -----------------------
	if ( pPlayer->m_nButtons & IN_RELOAD && UsesClipsForAmmo1() && !m_bInReload ) 
	{
		// reload when reload is pressed, or if no buttons are down and weapon is empty.
		Reload();
	}

	// -----------------------
	//  No buttons down
	// -----------------------
	if (!((pPlayer->m_nButtons & IN_ATTACK) || (pPlayer->m_nButtons & IN_ATTACK2) || (pPlayer->m_nButtons & IN_RELOAD)))
	{
		// no fire buttons down
		m_bFireOnEmpty = false;

		if ( !HasAnyAmmo() && m_flNextPrimaryAttack < gpGlobals->curtime ) 
		{
			// weapon isn't useable, switch.
			if ( !(GetWeaponFlags() & ITEM_FLAG_NOAUTOSWITCHEMPTY) && pPlayer->SwitchToNextBestWeapon( this ) )
			{
				m_flNextPrimaryAttack = gpGlobals->curtime + 0.3;
				return;
			}
		}
		else
		{
			// weapon is useable. Reload if empty and weapon has waited as long as it has to after firing
			if ( m_iClip1 == 0 && !(GetWeaponFlags() & ITEM_FLAG_NOAUTORELOAD) && m_flNextPrimaryAttack < gpGlobals->curtime )
			{
				Reload();
				return;
			}
		}

		WeaponIdle( );
		return;
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::Precache( void )
{
	BaseClass::Precache();
}


//-----------------------------------------------------------------------------
// Purpose: Same as base reload but doesn't change the owner's next attack time. This
//			lets us zoom out while reloading. This hack is necessary because our
//			ItemPostFrame is only called when the owner's next attack time has
//			expired.
// Output : Returns true if the weapon was reloaded, false if no more ammo.
//-----------------------------------------------------------------------------
bool CWeaponSniperRifle::Reload( void )
{
	CBaseCombatCharacter *pOwner = GetOwner();
	if (!pOwner)
	{
		return false;
	}
		
	if (pOwner->GetAmmoCount(m_iPrimaryAmmoType) > 0)
	{
		int primary		= MIN(GetMaxClip1() - m_iClip1, pOwner->GetAmmoCount(m_iPrimaryAmmoType));
		int secondary	= MIN(GetMaxClip2() - m_iClip2, pOwner->GetAmmoCount(m_iSecondaryAmmoType));

		if (primary > 0 || secondary > 0)
		{
			// Play reload on different channel as it happens after every fire
			// and otherwise steals channel away from fire sound
			WeaponSound(RELOAD);
			SendWeaponAnim( ACT_VM_RELOAD );

			m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration();

			m_bInReload = true;
		}

		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::PrimaryAttack( void )
{
	// Only the player fires this way so we can cast safely.
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (!pPlayer)
	{
		return;
	}

	if ( gpGlobals->curtime >= m_flNextPrimaryAttack )
	{
		// If my clip is empty (and I use clips) start reload
		if ( !m_iClip1 ) 
		{
			Reload();
			return;
		}

		// MUST call sound before removing a round from the clip of a CMachineGun dvs: does this apply to the sniper rifle? I don't know.
		WeaponSound(SINGLE);

		pPlayer->DoMuzzleFlash();

		SendWeaponAnim( ACT_VM_PRIMARYATTACK );

		// player "shoot" animation
		pPlayer->SetAnimation( PLAYER_ATTACK1 );

		// Don't fire again until fire animation has completed
		m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();
		m_iClip1 = m_iClip1 - 1;

		Vector vecSrc	 = pPlayer->Weapon_ShootPosition();
		Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );	

		// Fire the bullets
		pPlayer->FireBullets( SNIPER_BULLET_COUNT_PLAYER, vecSrc, vecAiming, GetBulletSpread(), MAX_TRACE_LENGTH, m_iPrimaryAmmoType, SNIPER_TRACER_FREQUENCY_PLAYER );

		CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), 600, 0.2 );

		QAngle vecPunch(random->RandomFloat( -SNIPER_KICKBACK, SNIPER_KICKBACK ), 0, 0);
		pPlayer->ViewPunch(vecPunch);

		// Indicate out of ammo condition if we run out of ammo.
		if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
		{
			pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0); 
		}
	}

	// Register a muzzleflash for the AI.
	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );
}


//-----------------------------------------------------------------------------
// Purpose: Zooms in using the sniper rifle scope.
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::Zoom( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (!pPlayer)
	{
		return;
	}

	if (m_nZoomLevel >= sizeof(g_nZoomFOV) / sizeof(g_nZoomFOV[0]))
	{
		if ( pPlayer->SetFOV( this, 0 ) )
		{
			pPlayer->ShowViewModel(true);
			
			// Zoom out to the default zoom level
			WeaponSound(SPECIAL2);	
			m_nZoomLevel = 0;
		}
	}
	else
	{
		if ( pPlayer->SetFOV( this, g_nZoomFOV[m_nZoomLevel] ) )
		{
			if (m_nZoomLevel == 0)
			{
				pPlayer->ShowViewModel(false);
			}

			WeaponSound(SPECIAL1);
			
			m_nZoomLevel++;
		}
	}

	m_fNextZoom = gpGlobals->curtime + SNIPER_ZOOM_RATE;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : virtual const Vector&
//-----------------------------------------------------------------------------
const Vector &CWeaponSniperRifle::GetBulletSpread( void )
{
	static Vector cone = SNIPER_CONE_PLAYER;
	return cone;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEvent - 
//			*pOperator - 
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	switch ( pEvent->event )
	{
		case EVENT_WEAPON_SNIPER_RIFLE_FIRE:
		{
			Vector vecShootOrigin, vecShootDir;
			vecShootOrigin = pOperator->Weapon_ShootPosition();

			CAI_BaseNPC *npc = pOperator->MyNPCPointer();
			Vector vecSpread;
			if (npc)
			{
				vecShootDir = npc->GetActualShootTrajectory( vecShootOrigin );
				vecSpread = VECTOR_CONE_PRECALCULATED;
			}
			else
			{
				AngleVectors( pOperator->GetLocalAngles(), &vecShootDir );
				vecSpread = GetBulletSpread();
			}
			WeaponSound( SINGLE_NPC );
			pOperator->FireBullets( SNIPER_BULLET_COUNT_NPC, vecShootOrigin, vecShootDir, vecSpread, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, SNIPER_TRACER_FREQUENCY_NPC );
			pOperator->DoMuzzleFlash();
			break;
		}

		default:
		{
			BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
			break;
		}
	}
}

#endif // defined ( HUMANERROR_DLL )