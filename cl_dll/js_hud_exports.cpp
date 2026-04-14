#include "hud.h"
#include "ammohistory.h"
#include "com_weapons.h"
#include "js_hud_exports.h"

#include <math.h>
#include <string.h>

static_assert(sizeof(JS_HUD_SnapshotV1) == 60, "JS_HUD_SnapshotV1 layout changed");

namespace
{
	inline int Clamp( int value, int minValue, int maxValue )
	{
		if( value < minValue )
			return minValue;
		if( value > maxValue )
			return maxValue;
		return value;
	}

	inline int NormalizeTeam( int team )
	{
		if( team == TEAM_TERRORIST || team == TEAM_CT )
			return team;
		return 0;
	}

	inline int GetAlive()
	{
		return gHUD.m_fPlayerDead ? 0 : 1;
	}

	inline int GetBuyzoneHint()
	{
		// Stock client state does not expose a dedicated "in buyzone" field.
		// Freeze-time is the most stable buy-eligibility hint available in this build.
		return g_iFreezeTimeOver == 0 ? 1 : 0;
	}

	inline int GetBombState()
	{
		// Slot 33 is used by radar code to track C4 dropped/planted state.
		const extra_player_info_t &bombInfo = g_PlayerExtraInfo[33];
		if( bombInfo.dead )
			return 0;
		return bombInfo.playerclass ? 2 : 1;
	}

	inline int GetRoundTimerRemainingSec()
	{
		// HUD redraw can be suppressed in web overlays, which may stall gHUD.m_flTime.
		// Prefer HUD clock when aligned with engine clock, otherwise use engine client time.
		const float hudNow = gHUD.m_flTime;
		const float engineNow = gEngfuncs.GetClientTime();
		float now = engineNow;
		if( hudNow > 0.0f && fabsf( hudNow - engineNow ) < 2.0f )
		{
			now = hudNow;
		}

		const float remaining = (float)gHUD.m_Timer.GetBaseRoundTimeSec() +
			gHUD.m_Timer.GetRoundStartTimeSec() - now;
		if( remaining <= 0.0f )
			return 0;
		return (int)remaining;
	}

	bool ReadWeaponState( int &weaponId, int &clip, int &reserve )
	{
		weaponId = 0;
		clip = 0;
		reserve = 0;

		// Prefer active predicted weapon from local state; fallback to HUD cache.
		if( g_finalstate )
		{
			const int predictedWeaponId = g_finalstate->client.m_iId;
			if( predictedWeaponId > 0 && predictedWeaponId < MAX_WEAPONS )
				weaponId = predictedWeaponId;
		}

		if( weaponId <= 0 || weaponId >= MAX_WEAPONS )
		{
			const int hudWeaponId = HUD_GetWeapon();
			if( hudWeaponId > 0 && hudWeaponId < MAX_WEAPONS )
				weaponId = hudWeaponId;
		}

		if( weaponId <= 0 || weaponId >= MAX_WEAPONS )
		{
			weaponId = 0;
			return false;
		}

		bool hasClip = false;
		if( g_finalstate && weaponId < 32 )
		{
			const weapon_data_t &wd = g_finalstate->weapondata[weaponId];
			if( wd.m_iId == weaponId )
			{
				clip = wd.m_iClip < 0 ? 0 : wd.m_iClip;
				hasClip = true;
			}
		}

		WEAPON *pWeapon = gWR.GetWeapon( weaponId );
		const bool hasWeaponDef = pWeapon && pWeapon->iId == weaponId;

		if( !hasClip && hasWeaponDef )
		{
			clip = pWeapon->iClip < 0 ? 0 : pWeapon->iClip;
			hasClip = true;
		}

		if( hasWeaponDef && pWeapon->iAmmoType >= 0 && pWeapon->iAmmoType < MAX_AMMO_TYPES )
		{
			reserve = gWR.CountAmmo( pWeapon->iAmmoType );
			if( reserve < 0 )
				reserve = 0;
		}

		return hasWeaponDef || hasClip;
	}

	uint32_t BuildFlags( int alive, int team, int isBuyzone, int bombState, bool hasWeapon )
	{
		uint32_t flags = 0;
		flags |= JS_HUD_FLAG_VALID_HEALTH;
		flags |= JS_HUD_FLAG_VALID_ARMOR;
		flags |= JS_HUD_FLAG_VALID_MONEY;
		flags |= JS_HUD_FLAG_VALID_ROUND_TIMER;

		if( hasWeapon )
		{
			flags |= JS_HUD_FLAG_VALID_WEAPON;
			flags |= JS_HUD_FLAG_VALID_CLIP;
			flags |= JS_HUD_FLAG_VALID_RESERVE;
		}

		if( alive )
			flags |= JS_HUD_FLAG_ALIVE;
		if( isBuyzone )
			flags |= JS_HUD_FLAG_IN_BUYZONE;
		if( bombState == 1 )
			flags |= JS_HUD_FLAG_BOMB_DROPPED;
		else if( bombState == 2 )
			flags |= JS_HUD_FLAG_BOMB_PLANTED;
		if( g_iFreezeTimeOver == 0 )
			flags |= JS_HUD_FLAG_FREEZE_TIME;
		if( g_bInBombZone )
			flags |= JS_HUD_FLAG_IN_BOMB_ZONE;

		flags |= ((uint32_t)(team & 0x3) << JS_HUD_TEAM_SHIFT);
		return flags;
	}
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetABIVersion( void )
{
	return JS_HUD_ABI_VERSION_1;
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetSnapshotSize( void )
{
	return (uint32_t)sizeof(JS_HUD_SnapshotV1);
}

extern "C" int DLLEXPORT JS_HUD_GetSnapshot( JS_HUD_SnapshotV1 *out )
{
	if( !out )
		return 0;

	memset( out, 0, sizeof(JS_HUD_SnapshotV1) );

	int weaponId = 0;
	int clip = 0;
	int reserve = 0;
	const bool hasWeapon = ReadWeaponState( weaponId, clip, reserve );
	const int health = Clamp( gHUD.m_Health.m_iHealth, 0, 100 );
	const int armor = Clamp( gHUD.m_Battery.GetArmorValue(), 0, 100 );
	const int money = Clamp( gHUD.m_Money.GetMoneyValue(), 0, 16000 );
	const int roundTime = Clamp( GetRoundTimerRemainingSec(), 0, 600 );
	const int team = NormalizeTeam( g_iTeamNumber );
	const int alive = GetAlive();
	const int isBuyzone = GetBuyzoneHint();
	const int bombState = GetBombState();
	const float hudTime = gHUD.m_flTime;

	out->abi_version = JS_HUD_ABI_VERSION_1;
	out->struct_size = (uint32_t)sizeof(JS_HUD_SnapshotV1);
	out->tick = hudTime <= 0.0f ? 0u : (uint32_t)(hudTime * 1000.0f);
	out->health = health;
	out->armor = armor;
	out->money = money;
	out->weapon_id = weaponId;
	out->clip = clip;
	out->reserve = reserve;
	out->round_time_sec = roundTime;
	out->is_buyzone = isBuyzone;
	out->team = team;
	out->alive = alive;
	out->bomb_state = bombState;
	out->flags = BuildFlags( alive, team, isBuyzone, bombState, hasWeapon );

	return 1;
}

extern "C" int DLLEXPORT JS_HUD_GetRoundTimer( void )
{
	return Clamp( GetRoundTimerRemainingSec(), 0, 600 );
}

extern "C" int DLLEXPORT JS_HUD_GetWeapon( void )
{
	int weaponId = 0;
	int clip = 0;
	int reserve = 0;
	ReadWeaponState( weaponId, clip, reserve );
	return weaponId;
}

extern "C" int DLLEXPORT JS_HUD_GetMoney( void )
{
	return Clamp( gHUD.m_Money.GetMoneyValue(), 0, 16000 );
}

extern "C" int DLLEXPORT JS_HUD_GetHealthArmor( void )
{
	const int health = Clamp( gHUD.m_Health.m_iHealth, 0, 100 );
	const int armor = Clamp( gHUD.m_Battery.GetArmorValue(), 0, 100 );
	return ((armor & 0xFFFF) << 16) | (health & 0xFFFF);
}

extern "C" int DLLEXPORT JS_HUD_GetFlags( void )
{
	int weaponId = 0;
	int clip = 0;
	int reserve = 0;
	const bool hasWeapon = ReadWeaponState( weaponId, clip, reserve );
	const int team = NormalizeTeam( g_iTeamNumber );
	const int alive = GetAlive();
	const int isBuyzone = GetBuyzoneHint();
	const int bombState = GetBombState();
	return (int)BuildFlags( alive, team, isBuyzone, bombState, hasWeapon );
}
