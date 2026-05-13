#include "hud.h"
#include "ammohistory.h"
#include "com_weapons.h"
#include "hud/radar.h"
#include "js_hud_exports.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(JS_HUD_SnapshotV1) == 76, "JS_HUD_SnapshotV1 layout changed");
static_assert(sizeof(JS_HUD_CrosshairStateV1) == 64, "JS_HUD_CrosshairStateV1 layout changed");
static_assert(sizeof(JS_HUD_PlayerRowV1) == 88, "JS_HUD_PlayerRowV1 layout changed");
static_assert(sizeof(JS_HUD_RosterSnapshotV1) == 2864, "JS_HUD_RosterSnapshotV1 layout changed");
static_assert(sizeof(JS_HUD_EventV1) == 232, "JS_HUD_EventV1 layout changed");
static_assert(sizeof(JS_HUD_DebugCountersV1) == 40, "JS_HUD_DebugCountersV1 layout changed");
static_assert(sizeof(JS_HUD_DeathStatsRowV1) == 48, "JS_HUD_DeathStatsRowV1 layout changed");
static_assert(sizeof(JS_HUD_DeathStatsSnapshotV1) == 3204, "JS_HUD_DeathStatsSnapshotV1 layout changed");

namespace
{
	struct HudRosterMirrorPlayer
	{
		bool seen_score;
		bool seen_assist;
		bool seen_team;
		bool seen_radar;
		int frags;
		int deaths;
		int assists;
		int playerclass;
		int teamnumber;
		char teamname[MAX_TEAM_NAME];
		Vector origin;
	};

	JS_HUD_EventV1 g_EventRing[JS_HUD_MAX_EVENTS];
	JS_HUD_SnapshotV1 g_StaticSnapshot;
	JS_HUD_CrosshairStateV1 g_StaticCrosshairState;
	JS_HUD_RosterSnapshotV1 g_StaticRosterSnapshot;
	JS_HUD_EventBufferV1 g_StaticEventBuffer;
	JS_HUD_DebugCountersV1 g_StaticDebugCounters;
	JS_HUD_DeathStatsSnapshotV1 g_StaticDeathStatsSnapshot;
	CrosshairDynamicsCache g_CrosshairExportCache = { 0.0f, 0, 0.0f };
	constexpr int OBS_IN_EYE_MODE = 4;
	uint32_t g_NextEventSeq = 1;
	HudRosterMirrorPlayer g_RosterMirror[MAX_PLAYERS + 1];
	uint32_t g_RosterSnapshotCount = 0;
	uint32_t g_LastRosterPlayerCount = 0;
	uint32_t g_ScoreInfoCount = 0;
	uint32_t g_TeamInfoCount = 0;
	uint32_t g_RadarCount = 0;
	uint32_t g_DeathMsgCount = 0;
	uint32_t g_RoundMsgCount = 0;
	int g_VoiceStatusState[JS_HUD_MAX_PLAYERS + 1];

	inline int Clamp( int value, int minValue, int maxValue )
	{
		if( value < minValue )
			return minValue;
		if( value > maxValue )
			return maxValue;
		return value;
	}

	inline float ClampFloat( float value, float minValue, float maxValue )
	{
		if( value < minValue )
			return minValue;
		if( value > maxValue )
			return maxValue;
		return value;
	}

	inline int NormalizeTeam( int team )
	{
		if( team == TEAM_CT )
			return 1;
		if( team == TEAM_TERRORIST )
			return 2;
		return 0;
	}

	inline int TeamFromName( const char *name )
	{
		if( !name || !name[0] )
			return 0;

		if( !stricmp( name, "CT" ) || !stricmp( name, "COUNTER-TERRORIST" ) ||
			!stricmp( name, "COUNTER_TERRORIST" ) || !stricmp( name, "COUNTERTERRORIST" ) )
			return 1;

		if( !stricmp( name, "TERRORIST" ) || !stricmp( name, "TERRORISTS" ) || !stricmp( name, "T" ) )
			return 2;

		return 0;
	}

	inline int NormalizePlayerTeam( int playerIndex )
	{
		if( playerIndex <= 0 || playerIndex > MAX_PLAYERS )
			return 0;

		const extra_player_info_t &extra = g_PlayerExtraInfo[playerIndex];
		const int native = NormalizeTeam( extra.teamnumber );
		if( native )
			return native;
		return TeamFromName( extra.teamname );
	}

	inline int NormalizeMirrorPlayerTeam( int playerIndex )
	{
		const int nativeTeam = NormalizePlayerTeam( playerIndex );
		if( nativeTeam )
			return nativeTeam;

		if( playerIndex <= 0 || playerIndex > MAX_PLAYERS )
			return 0;

		const HudRosterMirrorPlayer &mirror = g_RosterMirror[playerIndex];
		const int mirrorNative = NormalizeTeam( mirror.teamnumber );
		if( mirrorNative )
			return mirrorNative;
		return TeamFromName( mirror.teamname );
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

	inline bool IsRoundTimerActive()
	{
		// The web overlay intentionally hides the native HUD. RoundTime is still
		// authoritative, so do not tie the exported timer to native draw flags.
		return gHUD.m_Timer.GetBaseRoundTimeSec() > 0;
	}

	inline int GetRoundTimerRemainingSec()
	{
		if( !IsRoundTimerActive() )
			return 0;

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

	inline uint32_t GetHudTimeMs()
	{
		const float hudTime = gHUD.m_flTime > 0.0f ? gHUD.m_flTime : gEngfuncs.GetClientTime();
		return hudTime <= 0.0f ? 0u : (uint32_t)(hudTime * 1000.0f);
	}

	inline void CopyFixedString( char *dst, size_t dstSize, const char *src )
	{
		if( !dst || dstSize == 0 )
			return;

		if( !src )
			src = "";

		strncpy( dst, src, dstSize );
		dst[dstSize - 1] = 0;
	}

	inline void CopyCleanChatString( char *dst, size_t dstSize, const char *src )
	{
		if( !dst || dstSize == 0 )
			return;

		dst[0] = 0;
		if( !src )
			return;

		size_t out = 0;
		for( const unsigned char *p = (const unsigned char*)src; *p && out + 1 < dstSize; ++p )
		{
			const unsigned char ch = *p;
			if( ch >= 1 && ch <= 4 )
				continue;

			if( ch == '\r' || ch == '\n' || ch == '\t' )
			{
				if( out > 0 && dst[out - 1] != ' ' )
					dst[out++] = ' ';
				continue;
			}

			if( ch < 32 || ch > 126 )
				continue;

			dst[out++] = (char)ch;
		}

		while( out > 0 && dst[out - 1] == ' ' )
			out--;
		dst[out] = 0;
	}

	inline uint32_t PackAsciiChunk( const char *src, size_t srcSize, int chunk )
	{
		if( !src || srcSize == 0 || chunk < 0 )
			return 0;

		const size_t offset = (size_t)chunk * 4u;
		if( offset >= srcSize )
			return 0;

		uint32_t packed = 0;
		for( int i = 0; i < 4; i++ )
		{
			const size_t idx = offset + (size_t)i;
			const unsigned char ch = idx < srcSize ? (unsigned char)src[idx] : 0;
			packed |= (uint32_t)ch << (i * 8);
			if( ch == 0 )
				break;
		}
		return packed;
	}

	inline const char *PlayerNameOrFallback( int playerIndex, const char *fallback )
	{
		gHUD.m_Scoreboard.GetAllPlayersInfo();
		if( playerIndex > 0 && playerIndex <= MAX_PLAYERS && g_PlayerInfoList[playerIndex].name &&
			g_PlayerInfoList[playerIndex].name[0] )
			return g_PlayerInfoList[playerIndex].name;
		return fallback;
	}

	inline void FillDeathStatsRow( JS_HUD_DeathStatsRowV1 &row, int playerId, int damage, int hits )
	{
		memset( &row, 0, sizeof(row) );
		row.player_id = Clamp( playerId, 0, JS_HUD_MAX_PLAYERS );
		row.team = NormalizeMirrorPlayerTeam( row.player_id );
		row.damage = Clamp( damage, 0, 32767 );
		row.hits = Clamp( hits, 0, 255 );
		CopyFixedString( row.player_name, sizeof(row.player_name), PlayerNameOrFallback( row.player_id, "Player" ) );
	}

	inline JS_HUD_DeathStatsRowV1 *DeathStatsRowsForGroup( int group, int &count )
	{
		if( group == JS_HUD_DEATH_STATS_GROUP_ATTACKERS )
		{
			count = g_StaticDeathStatsSnapshot.attacker_count;
			return g_StaticDeathStatsSnapshot.attackers;
		}

		if( group == JS_HUD_DEATH_STATS_GROUP_VICTIMS )
		{
			count = g_StaticDeathStatsSnapshot.victim_count;
			return g_StaticDeathStatsSnapshot.victims;
		}

		count = 0;
		return nullptr;
	}

	inline bool OriginHasSignal( const Vector &origin )
	{
		return fabsf( origin.x ) > 0.01f || fabsf( origin.y ) > 0.01f || fabsf( origin.z ) > 0.01f;
	}

	inline int GetLocalPlayerIndex()
	{
		if( gHUD.m_Scoreboard.m_iPlayerNum > 0 && gHUD.m_Scoreboard.m_iPlayerNum <= JS_HUD_MAX_PLAYERS )
			return gHUD.m_Scoreboard.m_iPlayerNum;

		cl_entity_t *local = gEngfuncs.GetLocalPlayer();
		if( local && local->index > 0 && local->index <= JS_HUD_MAX_PLAYERS )
			return local->index;

		return 0;
	}

	inline bool HasMirrorSignal( const HudRosterMirrorPlayer &mirror )
	{
		return mirror.seen_score || mirror.seen_assist || mirror.seen_team || mirror.seen_radar;
	}

	inline void BuildRadarPoint( const Vector &origin, bool isLocal, float &radarX, float &radarY, bool &valid )
	{
		if( isLocal )
		{
			radarX = 0.0f;
			radarY = 0.0f;
			valid = true;
			return;
		}

		if( !OriginHasSignal( origin ) || !OriginHasSignal( gHUD.m_vecOrigin ) )
		{
			radarX = 0.0f;
			radarY = 0.0f;
			valid = false;
			return;
		}

		float dx = origin.x - gHUD.m_vecOrigin.x;
		float dy = origin.y - gHUD.m_vecOrigin.y;
		if( dx == 0.0f )
			dx = 0.00001f;
		if( dy == 0.0f )
			dy = 0.00001f;

		const float radarScale = 32.0f;
		const float radarRadius = 64.0f;
		const float distance = sqrtf( dx * dx + dy * dy );
		const float radius = ClampFloat( distance / radarScale, 0.0f, radarRadius );
		const float pi = 3.14159265358979323846f;
		const float offset = (float)( ( gHUD.m_vecAngles.y - ( atan2f( dy, dx ) * 180.0f / pi ) ) * pi / 180.0f );

		radarX = ClampFloat( sinf( offset ) * radius / radarRadius, -1.0f, 1.0f );
		radarY = ClampFloat( -cosf( offset ) * radius / radarRadius, -1.0f, 1.0f );
		valid = true;
	}

	inline int GetTeamScore( int uiTeam )
	{
		for( int i = 1; i <= MAX_TEAMS; i++ )
		{
			const team_info_t &team = g_TeamInfo[i];
			int normalized = NormalizeTeam( team.teamnumber );
			if( !normalized )
				normalized = TeamFromName( team.name );
			if( normalized == uiTeam )
				return team.frags;
		}

		return 0;
	}

	inline bool ContainsNoCase( const char *haystack, const char *needle )
	{
		if( !haystack || !needle || !needle[0] )
			return false;

		for( const char *h = haystack; *h; h++ )
		{
			const char *hp = h;
			const char *np = needle;
			while( *hp && *np && tolower( (unsigned char)*hp ) == tolower( (unsigned char)*np ) )
			{
				hp++;
				np++;
			}
			if( !*np )
				return true;
		}

		return false;
	}

	inline bool ContainsEitherNoCase( const char *a, const char *b, const char *needle )
	{
		return ContainsNoCase( a, needle ) || ContainsNoCase( b, needle );
	}

	int MatchRoundState( const char *rawText, const char *resolvedText )
	{
		if( ContainsEitherNoCase( rawText, resolvedText, "ct_win" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "ctwin" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "cts_win" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "cts win" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "counter-terrorists win" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "counter terrorists win" ) )
			return JS_HUD_ROUND_CT_WIN;

		if( ContainsEitherNoCase( rawText, resolvedText, "terrorists_win" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "terwin" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "terrorists win" ) )
			return JS_HUD_ROUND_T_WIN;

		if( ContainsEitherNoCase( rawText, resolvedText, "round_draw" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "round draw" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "draw" ) )
			return JS_HUD_ROUND_DRAW;

		if( ContainsEitherNoCase( rawText, resolvedText, "bomb_planted" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "bomb planted" ) )
			return JS_HUD_ROUND_BOMB_PLANTED;

		if( ContainsEitherNoCase( rawText, resolvedText, "bomb_defused" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "bomb defused" ) )
			return JS_HUD_ROUND_BOMB_DEFUSED;

		if( ContainsEitherNoCase( rawText, resolvedText, "target_bombed" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "target bombed" ) )
			return JS_HUD_ROUND_T_WIN;

		if( ContainsEitherNoCase( rawText, resolvedText, "game_commencing" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "game commencing" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "game_will_restart_in" ) ||
			ContainsEitherNoCase( rawText, resolvedText, "will restart" ) )
			return JS_HUD_ROUND_FREEZE;

		return JS_HUD_ROUND_UNKNOWN;
	}

	inline JS_HUD_EventV1 *PushEvent( uint32_t kind )
	{
		const uint32_t seq = g_NextEventSeq++;
		JS_HUD_EventV1 *event = &g_EventRing[(seq - 1) % JS_HUD_MAX_EVENTS];
		memset( event, 0, sizeof(*event) );
		event->seq = seq;
		event->kind = kind;
		event->time_ms = GetHudTimeMs();
		return event;
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

	inline bool IsSniperCrosshairWeapon( int weaponId )
	{
		return weaponId == WEAPON_AWP ||
			weaponId == WEAPON_SCOUT ||
			weaponId == WEAPON_SG550 ||
			weaponId == WEAPON_G3SG1;
	}

	uint32_t BuildFlags( int alive, int team, int isBuyzone, int bombState, bool hasWeapon, bool roundTimerActive, bool bombTimerActive )
	{
		uint32_t flags = 0;
		flags |= JS_HUD_FLAG_VALID_HEALTH;
		flags |= JS_HUD_FLAG_VALID_ARMOR;
		flags |= JS_HUD_FLAG_VALID_MONEY;
		if( roundTimerActive )
		{
			flags |= JS_HUD_FLAG_VALID_ROUND_TIMER;
			flags |= JS_HUD_FLAG_ROUND_TIMER_ACTIVE;
		}

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
		if( bombTimerActive )
			flags |= JS_HUD_FLAG_BOMB_TIMER_ACTIVE;

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
	const bool roundTimerActive = IsRoundTimerActive();
	const int bombTime = CounterSol_GetBombTimerRemainingSec();
	const bool bombTimerActive = bombState == 2 && bombTime > 0;
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
	out->flags = BuildFlags( alive, team, isBuyzone, bombState, hasWeapon, roundTimerActive, bombTimerActive );
	out->round_timer_active = roundTimerActive ? 1 : 0;
	out->bomb_time_sec = bombTime;
	out->bomb_timer_active = bombTimerActive ? 1 : 0;
	out->weapon_bits = (uint32_t)gHUD.m_iWeaponBits;

	return 1;
}

extern "C" const JS_HUD_SnapshotV1 *DLLEXPORT JS_HUD_GetSnapshotPtr( void )
{
	return JS_HUD_GetSnapshot( &g_StaticSnapshot ) ? &g_StaticSnapshot : nullptr;
}

extern "C" int DLLEXPORT JS_HUD_GetRoundTimer( void )
{
	return Clamp( GetRoundTimerRemainingSec(), 0, 600 );
}

extern "C" int DLLEXPORT JS_HUD_GetBombTimer( void )
{
	return Clamp( CounterSol_GetBombTimerRemainingSec(), 0, 600 );
}

extern "C" int DLLEXPORT JS_HUD_GetWeapon( void )
{
	int weaponId = 0;
	int clip = 0;
	int reserve = 0;
	ReadWeaponState( weaponId, clip, reserve );
	return weaponId;
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetWeaponBits( void )
{
	return (uint32_t)gHUD.m_iWeaponBits;
}

extern "C" int DLLEXPORT JS_HUD_GetClip( void )
{
	int weaponId = 0;
	int clip = 0;
	int reserve = 0;
	return ReadWeaponState( weaponId, clip, reserve ) ? clip : 0;
}

extern "C" int DLLEXPORT JS_HUD_GetReserve( void )
{
	int weaponId = 0;
	int clip = 0;
	int reserve = 0;
	return ReadWeaponState( weaponId, clip, reserve ) ? reserve : 0;
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
	const bool roundTimerActive = IsRoundTimerActive();
	const bool bombTimerActive = bombState == 2 && CounterSol_IsBombTimerActive();
	return (int)BuildFlags( alive, team, isBuyzone, bombState, hasWeapon, roundTimerActive, bombTimerActive );
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetCrosshairStateSize( void )
{
	return (uint32_t)sizeof(JS_HUD_CrosshairStateV1);
}

extern "C" int DLLEXPORT JS_HUD_GetCrosshairState( JS_HUD_CrosshairStateV1 *out )
{
	if( !out )
		return 0;

	memset( out, 0, sizeof(JS_HUD_CrosshairStateV1) );

	int weaponId = 0;
	int clip = 0;
	int reserve = 0;
	const bool hasWeapon = ReadWeaponState( weaponId, clip, reserve );
	const bool alive = GetAlive() != 0;
	const bool scoped = gHUD.m_iFOV > 0 && gHUD.m_iFOV <= 40;
	const bool sniper = IsSniperCrosshairWeapon( weaponId );
	const bool shieldDrawn = ( g_iWeaponFlags & WPNSTATE_SHIELD_DRAWN ) != 0;
	const bool spectatingTarget = g_iUser1 == OBS_IN_EYE_MODE && g_iUser2 > 0 && g_iUser2 <= MAX_PLAYERS;
	const bool valid = hasWeapon && weaponId > 0;
	const bool visible = valid && ( alive || spectatingTarget ) && !scoped && !sniper && !shieldDrawn;

	uint32_t flags = 0;
	if( valid )
		flags |= JS_HUD_CROSSHAIR_FLAG_VALID;
	if( visible )
		flags |= JS_HUD_CROSSHAIR_FLAG_VISIBLE;
	if( scoped )
		flags |= JS_HUD_CROSSHAIR_FLAG_SCOPED;
	if( sniper )
		flags |= JS_HUD_CROSSHAIR_FLAG_SNIPER;
	if( shieldDrawn )
		flags |= JS_HUD_CROSSHAIR_FLAG_SHIELD_DRAWN;
	if( alive )
		flags |= JS_HUD_CROSSHAIR_FLAG_ALIVE;
	if( spectatingTarget )
		flags |= JS_HUD_CROSSHAIR_FLAG_SPECTATING_TARGET;

	out->abi_version = JS_HUD_ABI_VERSION_1;
	out->struct_size = (uint32_t)sizeof(JS_HUD_CrosshairStateV1);
	out->tick = GetHudTimeMs();
	out->flags = flags;
	out->weapon_id = weaponId;
	out->shots_fired = g_iShotsFired;
	out->player_flags = g_iPlayerFlags;
	out->weapon_flags = g_iWeaponFlags;
	out->fov = gHUD.m_iFOV;
	out->player_speed = g_flPlayerSpeed;
	out->observer_mode = g_iUser1;
	out->observer_target_id = spectatingTarget ? g_iUser2 : 0;

	if( valid )
	{
		CrosshairDynamicsConfig config;
		config.dynamicMove = true;
		config.useWeaponBaseGap = true;
		config.dynamicScale = 1.0f;
		config.extraGap = 0.0f;
		const CrosshairDynamicsResult dynamics =
			CHudAmmo::CalculateCrosshairDynamics( weaponId, config, g_CrosshairExportCache );
		out->base_gap = dynamics.baseGap;
		out->movement_gap = dynamics.movementGap;
		out->recoil_gap = dynamics.finalGap;
		out->spread_delta = dynamics.spreadDelta;
	}

	return 1;
}

extern "C" const JS_HUD_CrosshairStateV1 *DLLEXPORT JS_HUD_GetCrosshairStatePtr( void )
{
	return JS_HUD_GetCrosshairState( &g_StaticCrosshairState ) ? &g_StaticCrosshairState : nullptr;
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetRosterSnapshotSize( void )
{
	return (uint32_t)sizeof(JS_HUD_RosterSnapshotV1);
}

extern "C" int DLLEXPORT JS_HUD_GetRosterSnapshot( JS_HUD_RosterSnapshotV1 *out )
{
	if( !out )
		return 0;

	memset( out, 0, sizeof(JS_HUD_RosterSnapshotV1) );
	gHUD.m_Scoreboard.GetAllPlayersInfo();

	out->abi_version = JS_HUD_ABI_VERSION_1;
	out->struct_size = (uint32_t)sizeof(JS_HUD_RosterSnapshotV1);
	out->player_size = (uint32_t)sizeof(JS_HUD_PlayerRowV1);
	out->tick = GetHudTimeMs();
	const int localPlayerId = GetLocalPlayerIndex();
	out->local_player_id = localPlayerId;
	out->ct_score = GetTeamScore( 1 );
	out->t_score = GetTeamScore( 2 );

	uint32_t count = 0;
	for( int i = 1; i <= JS_HUD_MAX_PLAYERS && count < JS_HUD_MAX_PLAYERS; i++ )
	{
		hud_player_info_t &info = g_PlayerInfoList[i];
		extra_player_info_t &extra = g_PlayerExtraInfo[i];
		const HudRosterMirrorPlayer &mirror = g_RosterMirror[i];
		const char *name = info.name;
		const int team = NormalizeMirrorPlayerTeam( i );
		const bool hasName = name && name[0];

		if( !hasName && team == 0 && !HasMirrorSignal( mirror ) && i != localPlayerId )
			continue;

		JS_HUD_PlayerRowV1 &row = out->players[count++];
		const bool isLocal = info.thisplayer || localPlayerId == i;
		const bool isAlive = team != 0 && !extra.dead;
		Vector origin = isLocal ? gHUD.m_vecOrigin : extra.origin;
		if( !isLocal && !OriginHasSignal( origin ) && mirror.seen_radar )
			origin = mirror.origin;
		float radarX = 0.0f;
		float radarY = 0.0f;
		bool validRadar = false;
		BuildRadarPoint( origin, isLocal, radarX, radarY, validRadar );

		row.id = i;
		row.team = team;
		row.kills = mirror.seen_score ? mirror.frags : extra.frags;
		row.deaths = mirror.seen_score ? mirror.deaths : extra.deaths;
		row.assists = mirror.seen_assist ? mirror.assists : extra.assists;
		row.ping = info.ping < 0 ? 0 : info.ping;
		row.money = extra.sb_account >= 0 ? Clamp( extra.sb_account, 0, 16000 ) : 0;
		const int rowHealth = isLocal
			? Clamp( gHUD.m_Health.m_iHealth, 0, 100 )
			: ( extra.sb_health >= 0 ? Clamp( extra.sb_health, 0, 100 ) : ( isAlive ? -1 : 0 ) );
		row.health = rowHealth >= 0 ? rowHealth : 0;
		row.flags = 0;
		if( isLocal )
			row.flags |= JS_HUD_PLAYER_FLAG_LOCAL;
		if( isAlive )
			row.flags |= JS_HUD_PLAYER_FLAG_ALIVE;
		if( OriginHasSignal( origin ) || isLocal )
			row.flags |= JS_HUD_PLAYER_FLAG_VALID_ORIGIN;
		if( validRadar )
			row.flags |= JS_HUD_PLAYER_FLAG_VALID_RADAR;
		if( rowHealth >= 0 )
			row.flags |= JS_HUD_PLAYER_FLAG_VALID_HEALTH;

		row.origin_x = origin.x;
		row.origin_y = origin.y;
		row.origin_z = origin.z;
		row.radar_x = radarX;
		row.radar_y = radarY;
		char fallbackName[16];
		snprintf( fallbackName, sizeof(fallbackName), "P%d", i );
		CopyFixedString( row.name, sizeof(row.name), hasName ? name : fallbackName );

		if( team == 1 )
		{
			out->ct_total++;
			if( isAlive )
				out->ct_alive++;
		}
		else if( team == 2 )
		{
			out->t_total++;
			if( isAlive )
				out->t_alive++;
		}
	}

	out->player_count = count;
	g_RosterSnapshotCount++;
	g_LastRosterPlayerCount = count;
	return 1;
}

extern "C" const JS_HUD_RosterSnapshotV1 *DLLEXPORT JS_HUD_GetRosterSnapshotPtr( void )
{
	return JS_HUD_GetRosterSnapshot( &g_StaticRosterSnapshot ) ? &g_StaticRosterSnapshot : nullptr;
}

extern "C" int DLLEXPORT JS_HUD_BuildRosterSnapshot( void )
{
	return JS_HUD_GetRosterSnapshot( &g_StaticRosterSnapshot );
}

extern "C" int DLLEXPORT JS_HUD_GetRosterMeta( int field )
{
	switch( field )
	{
	case JS_HUD_ROSTER_META_PLAYER_COUNT:
		return (int)g_StaticRosterSnapshot.player_count;
	case JS_HUD_ROSTER_META_LOCAL_PLAYER_ID:
		return g_StaticRosterSnapshot.local_player_id;
	case JS_HUD_ROSTER_META_CT_SCORE:
		return g_StaticRosterSnapshot.ct_score;
	case JS_HUD_ROSTER_META_T_SCORE:
		return g_StaticRosterSnapshot.t_score;
	case JS_HUD_ROSTER_META_CT_ALIVE:
		return g_StaticRosterSnapshot.ct_alive;
	case JS_HUD_ROSTER_META_CT_TOTAL:
		return g_StaticRosterSnapshot.ct_total;
	case JS_HUD_ROSTER_META_T_ALIVE:
		return g_StaticRosterSnapshot.t_alive;
	case JS_HUD_ROSTER_META_T_TOTAL:
		return g_StaticRosterSnapshot.t_total;
	case JS_HUD_ROSTER_META_TICK:
		return (int)g_StaticRosterSnapshot.tick;
	default:
		return 0;
	}
}

extern "C" int DLLEXPORT JS_HUD_GetRosterPlayerInt( int slot, int field )
{
	if( slot < 0 || slot >= JS_HUD_MAX_PLAYERS )
		return 0;

	const JS_HUD_PlayerRowV1 &row = g_StaticRosterSnapshot.players[slot];
	switch( field )
	{
	case JS_HUD_ROSTER_PLAYER_ID:
		return row.id;
	case JS_HUD_ROSTER_PLAYER_TEAM:
		return row.team;
	case JS_HUD_ROSTER_PLAYER_KILLS:
		return row.kills;
	case JS_HUD_ROSTER_PLAYER_DEATHS:
		return row.deaths;
	case JS_HUD_ROSTER_PLAYER_PING:
		return row.ping;
	case JS_HUD_ROSTER_PLAYER_MONEY:
		return row.money;
	case JS_HUD_ROSTER_PLAYER_FLAGS:
		return (int)row.flags;
	case JS_HUD_ROSTER_PLAYER_ASSISTS:
		return row.assists;
	case JS_HUD_ROSTER_PLAYER_HEALTH:
		return row.health;
	default:
		return 0;
	}
}

extern "C" float DLLEXPORT JS_HUD_GetRosterPlayerFloat( int slot, int field )
{
	if( slot < 0 || slot >= JS_HUD_MAX_PLAYERS )
		return 0.0f;

	const JS_HUD_PlayerRowV1 &row = g_StaticRosterSnapshot.players[slot];
	switch( field )
	{
	case JS_HUD_ROSTER_PLAYER_ORIGIN_X:
		return row.origin_x;
	case JS_HUD_ROSTER_PLAYER_ORIGIN_Y:
		return row.origin_y;
	case JS_HUD_ROSTER_PLAYER_ORIGIN_Z:
		return row.origin_z;
	case JS_HUD_ROSTER_PLAYER_RADAR_X:
		return row.radar_x;
	case JS_HUD_ROSTER_PLAYER_RADAR_Y:
		return row.radar_y;
	default:
		return 0.0f;
	}
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetRosterPlayerNamePacked( int slot, int chunk )
{
	if( slot < 0 || slot >= JS_HUD_MAX_PLAYERS )
		return 0;

	return PackAsciiChunk(
		g_StaticRosterSnapshot.players[slot].name,
		sizeof(g_StaticRosterSnapshot.players[slot].name),
		chunk
	);
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetEventBufferSize( void )
{
	return (uint32_t)sizeof(JS_HUD_EventBufferV1);
}

extern "C" int DLLEXPORT JS_HUD_GetEvents( uint32_t after_seq, JS_HUD_EventBufferV1 *out )
{
	if( !out )
		return 0;

	memset( out, 0, sizeof(JS_HUD_EventBufferV1) );
	out->abi_version = JS_HUD_ABI_VERSION_1;
	out->struct_size = (uint32_t)sizeof(JS_HUD_EventBufferV1);
	out->event_size = (uint32_t)sizeof(JS_HUD_EventV1);
	out->latest_seq = g_NextEventSeq > 1 ? g_NextEventSeq - 1 : 0;

	const uint32_t firstSeq = g_NextEventSeq > JS_HUD_MAX_EVENTS ? g_NextEventSeq - JS_HUD_MAX_EVENTS : 1;
	const uint32_t startSeq = after_seq + 1 > firstSeq ? after_seq + 1 : firstSeq;

	uint32_t count = 0;
	for( uint32_t seq = startSeq; seq < g_NextEventSeq && count < JS_HUD_MAX_EVENTS; seq++ )
	{
		const JS_HUD_EventV1 &event = g_EventRing[(seq - 1) % JS_HUD_MAX_EVENTS];
		if( event.seq == seq && event.kind != JS_HUD_EVENT_NONE )
			out->events[count++] = event;
	}

	out->event_count = count;
	return 1;
}

extern "C" const JS_HUD_EventBufferV1 *DLLEXPORT JS_HUD_GetEventsPtr( uint32_t after_seq )
{
	return JS_HUD_GetEvents( after_seq, &g_StaticEventBuffer ) ? &g_StaticEventBuffer : nullptr;
}

extern "C" int DLLEXPORT JS_HUD_BuildEvents( uint32_t after_seq )
{
	return JS_HUD_GetEvents( after_seq, &g_StaticEventBuffer );
}

extern "C" int DLLEXPORT JS_HUD_GetEventMeta( int field )
{
	switch( field )
	{
	case JS_HUD_EVENT_META_LATEST_SEQ:
		return (int)g_StaticEventBuffer.latest_seq;
	case JS_HUD_EVENT_META_EVENT_COUNT:
		return (int)g_StaticEventBuffer.event_count;
	default:
		return 0;
	}
}

extern "C" int DLLEXPORT JS_HUD_GetEventInt( int slot, int field )
{
	if( slot < 0 || slot >= JS_HUD_MAX_EVENTS )
		return 0;

	const JS_HUD_EventV1 &event = g_StaticEventBuffer.events[slot];
	switch( field )
	{
	case JS_HUD_EVENT_INT_SEQ:
		return (int)event.seq;
	case JS_HUD_EVENT_INT_KIND:
		return (int)event.kind;
	case JS_HUD_EVENT_INT_TIME_MS:
		return (int)event.time_ms;
	case JS_HUD_EVENT_INT_STATE:
		return event.state;
	case JS_HUD_EVENT_INT_KILLER_ID:
		return event.killer_id;
	case JS_HUD_EVENT_INT_VICTIM_ID:
		return event.victim_id;
	case JS_HUD_EVENT_INT_KILLER_TEAM:
		return event.killer_team;
	case JS_HUD_EVENT_INT_VICTIM_TEAM:
		return event.victim_team;
	case JS_HUD_EVENT_INT_ASSISTER_ID:
		return event.assister_id;
	case JS_HUD_EVENT_INT_ASSISTER_TEAM:
		return event.assister_team;
	case JS_HUD_EVENT_INT_HEADSHOT:
		return event.headshot;
	case JS_HUD_EVENT_INT_RARITY_FLAGS:
		return event.rarity_flags;
	default:
		return 0;
	}
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetEventTextPacked( int slot, int text_field, int chunk )
{
	if( slot < 0 || slot >= JS_HUD_MAX_EVENTS )
		return 0;

	const JS_HUD_EventV1 &event = g_StaticEventBuffer.events[slot];
	switch( text_field )
	{
	case JS_HUD_EVENT_TEXT_WEAPON:
		return PackAsciiChunk( event.weapon, sizeof(event.weapon), chunk );
	case JS_HUD_EVENT_TEXT_KILLER_NAME:
		return PackAsciiChunk( event.killer_name, sizeof(event.killer_name), chunk );
	case JS_HUD_EVENT_TEXT_ASSISTER_NAME:
		return PackAsciiChunk( event.assister_name, sizeof(event.assister_name), chunk );
	case JS_HUD_EVENT_TEXT_VICTIM_NAME:
		return PackAsciiChunk( event.victim_name, sizeof(event.victim_name), chunk );
	case JS_HUD_EVENT_TEXT_TEXT:
		return PackAsciiChunk( event.text, sizeof(event.text), chunk );
	default:
		return 0;
	}
}

extern "C" void DLLEXPORT JS_HUD_ResetEvents( void )
{
	memset( g_EventRing, 0, sizeof(g_EventRing) );
	memset( g_VoiceStatusState, 0, sizeof(g_VoiceStatusState) );
	if( g_NextEventSeq == 0 )
		g_NextEventSeq = 1;
}

extern "C" void DLLEXPORT JS_HUD_ResetRosterMirror( void )
{
	memset( g_RosterMirror, 0, sizeof(g_RosterMirror) );
	g_LastRosterPlayerCount = 0;
}

extern "C" void DLLEXPORT JS_HUD_RecordScoreInfo( int player, int frags, int deaths, int playerclass, int teamnumber )
{
	if( player <= 0 || player > JS_HUD_MAX_PLAYERS )
		return;

	HudRosterMirrorPlayer &mirror = g_RosterMirror[player];
	mirror.seen_score = true;
	mirror.frags = frags;
	mirror.deaths = deaths;
	mirror.playerclass = playerclass;
	mirror.teamnumber = teamnumber;
	g_ScoreInfoCount++;
}

extern "C" void DLLEXPORT JS_HUD_RecordAssistInfo( int player, int assists )
{
	if( player <= 0 || player > JS_HUD_MAX_PLAYERS )
		return;

	HudRosterMirrorPlayer &mirror = g_RosterMirror[player];
	mirror.seen_assist = true;
	mirror.assists = assists;
}

extern "C" void DLLEXPORT JS_HUD_RecordTeamInfo( int player, const char *team_name, int teamnumber )
{
	if( player <= 0 || player > JS_HUD_MAX_PLAYERS )
		return;

	HudRosterMirrorPlayer &mirror = g_RosterMirror[player];
	mirror.seen_team = true;
	mirror.teamnumber = teamnumber;
	CopyFixedString( mirror.teamname, sizeof(mirror.teamname), team_name );
	g_TeamInfoCount++;
}

extern "C" void DLLEXPORT JS_HUD_RecordRadarPosition( int player, float x, float y, float z )
{
	if( player <= 0 || player > JS_HUD_MAX_PLAYERS )
		return;

	HudRosterMirrorPlayer &mirror = g_RosterMirror[player];
	mirror.seen_radar = true;
	mirror.origin.x = x;
	mirror.origin.y = y;
	mirror.origin.z = z;
	g_RadarCount++;
}

extern "C" void DLLEXPORT JS_HUD_RecordKillEvent( int killer, int victim, int headshot, const char *weapon, int assister, int rarityFlags )
{
	g_DeathMsgCount++;
	JS_HUD_EventV1 *event = PushEvent( JS_HUD_EVENT_KILL );
	event->killer_id = killer;
	event->victim_id = victim;
	event->killer_team = NormalizeMirrorPlayerTeam( killer );
	event->victim_team = NormalizeMirrorPlayerTeam( victim );
	event->assister_id = assister;
	event->assister_team = NormalizeMirrorPlayerTeam( assister );
	event->headshot = headshot ? 1 : 0;
	event->rarity_flags = rarityFlags;
	event->state = JS_HUD_ROUND_UNKNOWN;
	CopyFixedString( event->weapon, sizeof(event->weapon), weapon && weapon[0] ? weapon : "world" );
	CopyFixedString( event->killer_name, sizeof(event->killer_name), PlayerNameOrFallback( killer, killer > 0 ? "Player" : "World" ) );
	CopyFixedString( event->assister_name, sizeof(event->assister_name), assister > 0 ? PlayerNameOrFallback( assister, "Player" ) : "" );
	CopyFixedString( event->victim_name, sizeof(event->victim_name), PlayerNameOrFallback( victim, "Player" ) );
}

extern "C" void DLLEXPORT JS_HUD_RecordRoundTextEvent( int msg_dest, const char *raw_text, const char *resolved_text )
{
	const int state = MatchRoundState( raw_text, resolved_text );
	if( state == JS_HUD_ROUND_UNKNOWN )
		return;

	g_RoundMsgCount++;
	JS_HUD_EventV1 *event = PushEvent( JS_HUD_EVENT_ROUND );
	event->state = state;
	snprintf( event->text, sizeof(event->text), "%s", resolved_text && resolved_text[0] ? resolved_text : ( raw_text ? raw_text : "" ) );
}

extern "C" void DLLEXPORT JS_HUD_RecordChatEvent( int player, int flags, const char *player_name, const char *text )
{
	char cleanText[JS_HUD_EVENT_TEXT_BYTES];
	CopyCleanChatString( cleanText, sizeof(cleanText), text );
	if( !cleanText[0] )
		return;

	const int playerId = Clamp( player, 0, JS_HUD_MAX_PLAYERS );
	const int chatFlags = flags & ( JS_HUD_CHAT_FLAG_TEAM | JS_HUD_CHAT_FLAG_RADIO | JS_HUD_CHAT_FLAG_SYSTEM );
	const char *fallback = ( chatFlags & JS_HUD_CHAT_FLAG_SYSTEM ) ? "Server" :
		( ( chatFlags & JS_HUD_CHAT_FLAG_RADIO ) ? "Radio" : "Player" );

	char cleanName[JS_HUD_PLAYER_NAME_BYTES];
	CopyCleanChatString(
		cleanName,
		sizeof(cleanName),
		player_name && player_name[0] ? player_name : PlayerNameOrFallback( playerId, fallback )
	);
	if( !cleanName[0] )
		CopyFixedString( cleanName, sizeof(cleanName), fallback );

	JS_HUD_EventV1 *event = PushEvent( JS_HUD_EVENT_CHAT );
	event->state = chatFlags;
	event->killer_id = playerId;
	event->killer_team = NormalizeMirrorPlayerTeam( playerId );
	CopyFixedString( event->killer_name, sizeof(event->killer_name), cleanName );
	CopyFixedString( event->text, sizeof(event->text), cleanText );
}

extern "C" void DLLEXPORT JS_HUD_RecordVoiceStatus( int entindex, int talking )
{
	if( entindex == -2 )
		return;

	const bool isLocal = entindex == -1;
	const int playerId = isLocal ? GetLocalPlayerIndex() : Clamp( entindex, 0, JS_HUD_MAX_PLAYERS );
	if( !isLocal && ( playerId <= 0 || playerId > JS_HUD_MAX_PLAYERS ) )
		return;

	const int slot = isLocal ? 0 : playerId;
	const int nextState = talking ? JS_HUD_VOICE_TALKING : JS_HUD_VOICE_STOPPED;
	if( g_VoiceStatusState[slot] == nextState )
		return;

	g_VoiceStatusState[slot] = nextState;

	JS_HUD_EventV1 *event = PushEvent( JS_HUD_EVENT_VOICE );
	event->state = nextState;
	event->killer_id = playerId;
	event->killer_team = NormalizeMirrorPlayerTeam( playerId );
	event->headshot = isLocal ? 1 : 0;
	CopyFixedString( event->killer_name, sizeof(event->killer_name), PlayerNameOrFallback( playerId, isLocal ? "You" : "Player" ) );
	CopyFixedString( event->text, sizeof(event->text), nextState == JS_HUD_VOICE_TALKING ? "talking" : "stopped" );
}

extern "C" void DLLEXPORT JS_HUD_RecordDeathStats(
	int seq,
	int victim,
	int killer,
	const char *weapon,
	int attackerCount,
	const int *attackerIds,
	const int *attackerDamage,
	const int *attackerHits,
	int victimCount,
	const int *victimIds,
	const int *victimDamage,
	const int *victimHits
)
{
	memset( &g_StaticDeathStatsSnapshot, 0, sizeof(g_StaticDeathStatsSnapshot) );
	g_StaticDeathStatsSnapshot.abi_version = JS_HUD_ABI_VERSION_1;
	g_StaticDeathStatsSnapshot.struct_size = (uint32_t)sizeof(JS_HUD_DeathStatsSnapshotV1);
	g_StaticDeathStatsSnapshot.row_size = (uint32_t)sizeof(JS_HUD_DeathStatsRowV1);
	g_StaticDeathStatsSnapshot.seq = seq > 0 ? (uint32_t)seq : 0u;
	g_StaticDeathStatsSnapshot.time_ms = GetHudTimeMs();
	g_StaticDeathStatsSnapshot.victim_id = Clamp( victim, 0, JS_HUD_MAX_PLAYERS );
	g_StaticDeathStatsSnapshot.killer_id = Clamp( killer, 0, JS_HUD_MAX_PLAYERS );
	g_StaticDeathStatsSnapshot.victim_team = NormalizeMirrorPlayerTeam( victim );
	g_StaticDeathStatsSnapshot.killer_team = NormalizeMirrorPlayerTeam( killer );
	g_StaticDeathStatsSnapshot.attacker_count = Clamp( attackerCount, 0, JS_HUD_MAX_DEATH_STATS_ROWS );
	g_StaticDeathStatsSnapshot.victim_count = Clamp( victimCount, 0, JS_HUD_MAX_DEATH_STATS_ROWS );

	CopyFixedString( g_StaticDeathStatsSnapshot.weapon, sizeof(g_StaticDeathStatsSnapshot.weapon), weapon && weapon[0] ? weapon : "world" );
	CopyFixedString(
		g_StaticDeathStatsSnapshot.killer_name,
		sizeof(g_StaticDeathStatsSnapshot.killer_name),
		PlayerNameOrFallback( killer, killer > 0 ? "Player" : "World" )
	);
	CopyFixedString(
		g_StaticDeathStatsSnapshot.victim_name,
		sizeof(g_StaticDeathStatsSnapshot.victim_name),
		PlayerNameOrFallback( victim, "Player" )
	);

	for( int i = 0; i < g_StaticDeathStatsSnapshot.attacker_count; i++ )
	{
		FillDeathStatsRow(
			g_StaticDeathStatsSnapshot.attackers[i],
			attackerIds ? attackerIds[i] : 0,
			attackerDamage ? attackerDamage[i] : 0,
			attackerHits ? attackerHits[i] : 0
		);
	}

	for( int i = 0; i < g_StaticDeathStatsSnapshot.victim_count; i++ )
	{
		FillDeathStatsRow(
			g_StaticDeathStatsSnapshot.victims[i],
			victimIds ? victimIds[i] : 0,
			victimDamage ? victimDamage[i] : 0,
			victimHits ? victimHits[i] : 0
		);
	}
}

extern "C" int DLLEXPORT JS_HUD_GetDeathStatsMeta( int field )
{
	if( g_StaticDeathStatsSnapshot.abi_version != JS_HUD_ABI_VERSION_1 )
		return 0;

	switch( field )
	{
	case JS_HUD_DEATH_STATS_META_SEQ:
		return (int)g_StaticDeathStatsSnapshot.seq;
	case JS_HUD_DEATH_STATS_META_TIME_MS:
		return (int)g_StaticDeathStatsSnapshot.time_ms;
	case JS_HUD_DEATH_STATS_META_VICTIM_ID:
		return g_StaticDeathStatsSnapshot.victim_id;
	case JS_HUD_DEATH_STATS_META_KILLER_ID:
		return g_StaticDeathStatsSnapshot.killer_id;
	case JS_HUD_DEATH_STATS_META_VICTIM_TEAM:
		return g_StaticDeathStatsSnapshot.victim_team;
	case JS_HUD_DEATH_STATS_META_KILLER_TEAM:
		return g_StaticDeathStatsSnapshot.killer_team;
	case JS_HUD_DEATH_STATS_META_ATTACKER_COUNT:
		return g_StaticDeathStatsSnapshot.attacker_count;
	case JS_HUD_DEATH_STATS_META_VICTIM_COUNT:
		return g_StaticDeathStatsSnapshot.victim_count;
	default:
		return 0;
	}
}

extern "C" int DLLEXPORT JS_HUD_GetDeathStatsRowInt( int group, int slot, int field )
{
	int count = 0;
	JS_HUD_DeathStatsRowV1 *rows = DeathStatsRowsForGroup( group, count );
	if( !rows || slot < 0 || slot >= count || slot >= JS_HUD_MAX_DEATH_STATS_ROWS )
		return 0;

	const JS_HUD_DeathStatsRowV1 &row = rows[slot];
	switch( field )
	{
	case JS_HUD_DEATH_STATS_ROW_PLAYER_ID:
		return row.player_id;
	case JS_HUD_DEATH_STATS_ROW_TEAM:
		return row.team;
	case JS_HUD_DEATH_STATS_ROW_DAMAGE:
		return row.damage;
	case JS_HUD_DEATH_STATS_ROW_HITS:
		return row.hits;
	default:
		return 0;
	}
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetDeathStatsTextPacked( int text_field, int chunk )
{
	if( g_StaticDeathStatsSnapshot.abi_version != JS_HUD_ABI_VERSION_1 )
		return 0;

	switch( text_field )
	{
	case JS_HUD_DEATH_STATS_TEXT_WEAPON:
		return PackAsciiChunk( g_StaticDeathStatsSnapshot.weapon, sizeof(g_StaticDeathStatsSnapshot.weapon), chunk );
	case JS_HUD_DEATH_STATS_TEXT_KILLER_NAME:
		return PackAsciiChunk( g_StaticDeathStatsSnapshot.killer_name, sizeof(g_StaticDeathStatsSnapshot.killer_name), chunk );
	case JS_HUD_DEATH_STATS_TEXT_VICTIM_NAME:
		return PackAsciiChunk( g_StaticDeathStatsSnapshot.victim_name, sizeof(g_StaticDeathStatsSnapshot.victim_name), chunk );
	default:
		return 0;
	}
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetDeathStatsRowNamePacked( int group, int slot, int chunk )
{
	int count = 0;
	JS_HUD_DeathStatsRowV1 *rows = DeathStatsRowsForGroup( group, count );
	if( !rows || slot < 0 || slot >= count || slot >= JS_HUD_MAX_DEATH_STATS_ROWS )
		return 0;

	return PackAsciiChunk( rows[slot].player_name, sizeof(rows[slot].player_name), chunk );
}

extern "C" uint32_t DLLEXPORT JS_HUD_GetDebugCountersSize( void )
{
	return (uint32_t)sizeof(JS_HUD_DebugCountersV1);
}

extern "C" int DLLEXPORT JS_HUD_GetDebugCounters( JS_HUD_DebugCountersV1 *out )
{
	if( !out )
		return 0;

	memset( out, 0, sizeof(JS_HUD_DebugCountersV1) );
	out->abi_version = JS_HUD_ABI_VERSION_1;
	out->struct_size = (uint32_t)sizeof(JS_HUD_DebugCountersV1);
	out->roster_snapshot_count = g_RosterSnapshotCount;
	out->roster_player_count = g_LastRosterPlayerCount;
	out->score_info_count = g_ScoreInfoCount;
	out->team_info_count = g_TeamInfoCount;
	out->radar_count = g_RadarCount;
	out->death_msg_count = g_DeathMsgCount;
	out->round_msg_count = g_RoundMsgCount;
	out->latest_event_seq = g_NextEventSeq > 1 ? g_NextEventSeq - 1 : 0;
	return 1;
}

extern "C" const JS_HUD_DebugCountersV1 *DLLEXPORT JS_HUD_GetDebugCountersPtr( void )
{
	return JS_HUD_GetDebugCounters( &g_StaticDebugCounters ) ? &g_StaticDebugCounters : nullptr;
}

extern "C" int DLLEXPORT JS_HUD_BuildDebugCounters( void )
{
	return JS_HUD_GetDebugCounters( &g_StaticDebugCounters );
}

extern "C" int DLLEXPORT JS_HUD_GetDebugCounter( int field )
{
	switch( field )
	{
	case JS_HUD_DEBUG_ROSTER_SNAPSHOT_COUNT:
		return (int)g_StaticDebugCounters.roster_snapshot_count;
	case JS_HUD_DEBUG_ROSTER_PLAYER_COUNT:
		return (int)g_StaticDebugCounters.roster_player_count;
	case JS_HUD_DEBUG_SCORE_INFO_COUNT:
		return (int)g_StaticDebugCounters.score_info_count;
	case JS_HUD_DEBUG_TEAM_INFO_COUNT:
		return (int)g_StaticDebugCounters.team_info_count;
	case JS_HUD_DEBUG_RADAR_COUNT:
		return (int)g_StaticDebugCounters.radar_count;
	case JS_HUD_DEBUG_DEATH_MSG_COUNT:
		return (int)g_StaticDebugCounters.death_msg_count;
	case JS_HUD_DEBUG_ROUND_MSG_COUNT:
		return (int)g_StaticDebugCounters.round_msg_count;
	case JS_HUD_DEBUG_LATEST_EVENT_SEQ:
		return (int)g_StaticDebugCounters.latest_event_seq;
	default:
		return 0;
	}
}
