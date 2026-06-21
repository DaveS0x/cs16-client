#pragma once

#include <stdint.h>

#define JS_HUD_ABI_VERSION_1 0x00010009u
#define JS_HUD_MAX_PLAYERS 32
#define JS_HUD_PLAYER_NAME_BYTES 32
#define JS_HUD_MAX_EVENTS 32
#define JS_HUD_MAX_DEATH_STATS_ROWS JS_HUD_MAX_PLAYERS
#define JS_HUD_EVENT_WEAPON_BYTES 24
#define JS_HUD_EVENT_TEXT_BYTES 192

enum JS_HUD_SnapshotFlags
{
	JS_HUD_FLAG_VALID_HEALTH      = 1u << 0,
	JS_HUD_FLAG_VALID_ARMOR       = 1u << 1,
	JS_HUD_FLAG_VALID_MONEY       = 1u << 2,
	JS_HUD_FLAG_VALID_WEAPON      = 1u << 3,
	JS_HUD_FLAG_VALID_CLIP        = 1u << 4,
	JS_HUD_FLAG_VALID_RESERVE     = 1u << 5,
	JS_HUD_FLAG_VALID_ROUND_TIMER = 1u << 6,
	JS_HUD_FLAG_ALIVE             = 1u << 7,
	JS_HUD_FLAG_IN_BUYZONE        = 1u << 8,
	JS_HUD_FLAG_BOMB_DROPPED      = 1u << 9,
	JS_HUD_FLAG_BOMB_PLANTED      = 1u << 10,
	JS_HUD_FLAG_FREEZE_TIME       = 1u << 11,
	JS_HUD_FLAG_IN_BOMB_ZONE      = 1u << 12,
	JS_HUD_FLAG_ROUND_TIMER_ACTIVE = 1u << 13,
	JS_HUD_FLAG_BOMB_TIMER_ACTIVE  = 1u << 14,
};

#define JS_HUD_TEAM_SHIFT 16u
#define JS_HUD_TEAM_MASK (0x3u << JS_HUD_TEAM_SHIFT)

typedef struct JS_HUD_SnapshotV1
{
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t tick;
	int32_t health;
	int32_t armor;
	int32_t money;
	int32_t weapon_id;
	int32_t clip;
	int32_t reserve;
	int32_t round_time_sec;
	int32_t is_buyzone;
	int32_t team;
	int32_t alive;
	int32_t bomb_state; // 0 = none, 1 = dropped, 2 = planted
	uint32_t flags;
	int32_t round_timer_active;
	int32_t bomb_time_sec;
	int32_t bomb_timer_active;
	uint32_t weapon_bits;
} JS_HUD_SnapshotV1;

enum JS_HUD_CrosshairFlags
{
	JS_HUD_CROSSHAIR_FLAG_VALID        = 1u << 0,
	JS_HUD_CROSSHAIR_FLAG_VISIBLE      = 1u << 1,
	JS_HUD_CROSSHAIR_FLAG_SCOPED       = 1u << 2,
	JS_HUD_CROSSHAIR_FLAG_SNIPER       = 1u << 3,
	JS_HUD_CROSSHAIR_FLAG_SHIELD_DRAWN = 1u << 4,
	JS_HUD_CROSSHAIR_FLAG_ALIVE        = 1u << 5,
	JS_HUD_CROSSHAIR_FLAG_SPECTATING_TARGET = 1u << 6,
};

typedef struct JS_HUD_CrosshairStateV1
{
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t tick;
	uint32_t flags;
	int32_t weapon_id;
	int32_t shots_fired;
	int32_t player_flags;
	int32_t weapon_flags;
	int32_t fov;
	float base_gap;
	float movement_gap;
	float recoil_gap;
	float spread_delta;
	float player_speed;
	int32_t observer_mode;
	int32_t observer_target_id;
} JS_HUD_CrosshairStateV1;

// Field indices for the heap-free scalar crosshair getters below. These MUST
// stay in sync with the bridge (hud-overlay/src/lib/wasmBridge.ts).
enum JS_HUD_CrosshairIntField
{
	JS_HUD_CROSSHAIR_INT_FLAGS           = 0,
	JS_HUD_CROSSHAIR_INT_WEAPON_ID       = 1,
	JS_HUD_CROSSHAIR_INT_FOV             = 2,
	JS_HUD_CROSSHAIR_INT_SHOTS_FIRED     = 3,
	JS_HUD_CROSSHAIR_INT_PLAYER_FLAGS    = 4,
	JS_HUD_CROSSHAIR_INT_WEAPON_FLAGS    = 5,
	JS_HUD_CROSSHAIR_INT_OBSERVER_MODE   = 6,
	JS_HUD_CROSSHAIR_INT_OBSERVER_TARGET = 7,
	JS_HUD_CROSSHAIR_INT_TICK            = 8,
	JS_HUD_CROSSHAIR_INT_ABI_VERSION     = 9,
};

enum JS_HUD_CrosshairFloatField
{
	JS_HUD_CROSSHAIR_FLOAT_BASE_GAP     = 0,
	JS_HUD_CROSSHAIR_FLOAT_MOVEMENT_GAP = 1,
	JS_HUD_CROSSHAIR_FLOAT_RECOIL_GAP   = 2,
	JS_HUD_CROSSHAIR_FLOAT_SPREAD_DELTA = 3,
	JS_HUD_CROSSHAIR_FLOAT_PLAYER_SPEED = 4,
};

enum JS_HUD_PlayerFlags
{
	JS_HUD_PLAYER_FLAG_LOCAL        = 1u << 0,
	JS_HUD_PLAYER_FLAG_ALIVE        = 1u << 1,
	JS_HUD_PLAYER_FLAG_VALID_ORIGIN = 1u << 2,
	JS_HUD_PLAYER_FLAG_VALID_RADAR  = 1u << 3,
	JS_HUD_PLAYER_FLAG_VALID_HEALTH = 1u << 4,
};

typedef struct JS_HUD_PlayerRowV1
{
	int32_t id;
	int32_t team;
	int32_t kills;
	int32_t deaths;
	int32_t ping;
	int32_t money;
	int32_t health;
	uint32_t flags;
	float origin_x;
	float origin_y;
	float origin_z;
	float radar_x;
	float radar_y;
	char name[JS_HUD_PLAYER_NAME_BYTES];
	int32_t assists;
} JS_HUD_PlayerRowV1;

typedef struct JS_HUD_RosterSnapshotV1
{
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t player_size;
	uint32_t tick;
	uint32_t player_count;
	int32_t local_player_id;
	int32_t ct_score;
	int32_t t_score;
	int32_t ct_alive;
	int32_t ct_total;
	int32_t t_alive;
	int32_t t_total;
	JS_HUD_PlayerRowV1 players[JS_HUD_MAX_PLAYERS];
} JS_HUD_RosterSnapshotV1;

enum JS_HUD_EventKind
{
	JS_HUD_EVENT_NONE  = 0,
	JS_HUD_EVENT_KILL  = 1,
	JS_HUD_EVENT_ROUND = 2,
	JS_HUD_EVENT_CHAT  = 3,
	JS_HUD_EVENT_VOICE = 4,
	JS_HUD_EVENT_HIT   = 5,
};

enum JS_HUD_HitEventFlags
{
	JS_HUD_HIT_FLAG_KILL  = 1u << 0, // this hit was the killing blow
	JS_HUD_HIT_FLAG_ARMOR = 1u << 1, // damage was partly absorbed by armor (reserved)
};

enum JS_HUD_ChatEventFlags
{
	JS_HUD_CHAT_FLAG_TEAM   = 1u << 0,
	JS_HUD_CHAT_FLAG_RADIO  = 1u << 1,
	JS_HUD_CHAT_FLAG_SYSTEM = 1u << 2,
};

enum JS_HUD_VoiceEventState
{
	JS_HUD_VOICE_STOPPED = 0,
	JS_HUD_VOICE_TALKING = 1,
};

enum JS_HUD_RoundEventState
{
	JS_HUD_ROUND_UNKNOWN      = 0,
	JS_HUD_ROUND_FREEZE       = 1,
	JS_HUD_ROUND_LIVE         = 2,
	JS_HUD_ROUND_CT_WIN       = 3,
	JS_HUD_ROUND_T_WIN        = 4,
	JS_HUD_ROUND_DRAW         = 5,
	JS_HUD_ROUND_BOMB_PLANTED = 6,
	JS_HUD_ROUND_BOMB_DEFUSED = 7,
};

typedef struct JS_HUD_EventV1
{
	uint32_t seq;
	uint32_t kind;
	uint32_t time_ms;
	int32_t state;
	int32_t killer_id;
	int32_t victim_id;
	int32_t killer_team;
	int32_t victim_team;
	int32_t assister_id;
	int32_t assister_team;
	int32_t headshot;
	char weapon[JS_HUD_EVENT_WEAPON_BYTES];
	char killer_name[JS_HUD_PLAYER_NAME_BYTES];
	char assister_name[JS_HUD_PLAYER_NAME_BYTES];
	char victim_name[JS_HUD_PLAYER_NAME_BYTES];
	char text[JS_HUD_EVENT_TEXT_BYTES];
	int32_t rarity_flags;
} JS_HUD_EventV1;

typedef struct JS_HUD_EventBufferV1
{
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t event_size;
	uint32_t latest_seq;
	uint32_t event_count;
	JS_HUD_EventV1 events[JS_HUD_MAX_EVENTS];
} JS_HUD_EventBufferV1;

typedef struct JS_HUD_DebugCountersV1
{
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t roster_snapshot_count;
	uint32_t roster_player_count;
	uint32_t score_info_count;
	uint32_t team_info_count;
	uint32_t radar_count;
	uint32_t death_msg_count;
	uint32_t round_msg_count;
	uint32_t latest_event_seq;
} JS_HUD_DebugCountersV1;

typedef struct JS_HUD_DeathStatsRowV1
{
	int32_t player_id;
	int32_t team;
	int32_t damage;
	int32_t hits;
	char player_name[JS_HUD_PLAYER_NAME_BYTES];
} JS_HUD_DeathStatsRowV1;

typedef struct JS_HUD_DeathStatsSnapshotV1
{
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t row_size;
	uint32_t seq;
	uint32_t time_ms;
	int32_t victim_id;
	int32_t killer_id;
	int32_t victim_team;
	int32_t killer_team;
	int32_t attacker_count;
	int32_t victim_count;
	char weapon[JS_HUD_EVENT_WEAPON_BYTES];
	char killer_name[JS_HUD_PLAYER_NAME_BYTES];
	char victim_name[JS_HUD_PLAYER_NAME_BYTES];
	JS_HUD_DeathStatsRowV1 attackers[JS_HUD_MAX_DEATH_STATS_ROWS];
	JS_HUD_DeathStatsRowV1 victims[JS_HUD_MAX_DEATH_STATS_ROWS];
} JS_HUD_DeathStatsSnapshotV1;

enum JS_HUD_DeathStatsMetaField
{
	JS_HUD_DEATH_STATS_META_SEQ = 0,
	JS_HUD_DEATH_STATS_META_TIME_MS = 1,
	JS_HUD_DEATH_STATS_META_VICTIM_ID = 2,
	JS_HUD_DEATH_STATS_META_KILLER_ID = 3,
	JS_HUD_DEATH_STATS_META_VICTIM_TEAM = 4,
	JS_HUD_DEATH_STATS_META_KILLER_TEAM = 5,
	JS_HUD_DEATH_STATS_META_ATTACKER_COUNT = 6,
	JS_HUD_DEATH_STATS_META_VICTIM_COUNT = 7,
};

enum JS_HUD_DeathStatsRowGroup
{
	JS_HUD_DEATH_STATS_GROUP_ATTACKERS = 0,
	JS_HUD_DEATH_STATS_GROUP_VICTIMS = 1,
};

enum JS_HUD_DeathStatsRowField
{
	JS_HUD_DEATH_STATS_ROW_PLAYER_ID = 0,
	JS_HUD_DEATH_STATS_ROW_TEAM = 1,
	JS_HUD_DEATH_STATS_ROW_DAMAGE = 2,
	JS_HUD_DEATH_STATS_ROW_HITS = 3,
};

enum JS_HUD_DeathStatsTextField
{
	JS_HUD_DEATH_STATS_TEXT_WEAPON = 0,
	JS_HUD_DEATH_STATS_TEXT_KILLER_NAME = 1,
	JS_HUD_DEATH_STATS_TEXT_VICTIM_NAME = 2,
};

enum JS_HUD_RosterMetaField
{
	JS_HUD_ROSTER_META_PLAYER_COUNT = 0,
	JS_HUD_ROSTER_META_LOCAL_PLAYER_ID,
	JS_HUD_ROSTER_META_CT_SCORE,
	JS_HUD_ROSTER_META_T_SCORE,
	JS_HUD_ROSTER_META_CT_ALIVE,
	JS_HUD_ROSTER_META_CT_TOTAL,
	JS_HUD_ROSTER_META_T_ALIVE,
	JS_HUD_ROSTER_META_T_TOTAL,
	JS_HUD_ROSTER_META_TICK,
};

enum JS_HUD_RosterPlayerIntField
{
	JS_HUD_ROSTER_PLAYER_ID = 0,
	JS_HUD_ROSTER_PLAYER_TEAM,
	JS_HUD_ROSTER_PLAYER_KILLS,
	JS_HUD_ROSTER_PLAYER_DEATHS,
	JS_HUD_ROSTER_PLAYER_PING,
	JS_HUD_ROSTER_PLAYER_MONEY,
	JS_HUD_ROSTER_PLAYER_FLAGS,
	JS_HUD_ROSTER_PLAYER_ASSISTS,
	JS_HUD_ROSTER_PLAYER_HEALTH,
};

enum JS_HUD_ObserverPovIntField
{
	JS_HUD_OBSERVER_POV_ACTIVE = 0,
	JS_HUD_OBSERVER_POV_TARGET_ID,
	JS_HUD_OBSERVER_POV_TEAM,
	JS_HUD_OBSERVER_POV_ALIVE,
	JS_HUD_OBSERVER_POV_HEALTH,
	JS_HUD_OBSERVER_POV_ARMOR,
	JS_HUD_OBSERVER_POV_ARMOR_TYPE,
	JS_HUD_OBSERVER_POV_HAS_HELMET,
	JS_HUD_OBSERVER_POV_WEAPON_ID,
	JS_HUD_OBSERVER_POV_CLIP,
	JS_HUD_OBSERVER_POV_RESERVE,
	JS_HUD_OBSERVER_POV_WEAPON_BITS,
};

enum JS_HUD_RosterPlayerFloatField
{
	JS_HUD_ROSTER_PLAYER_ORIGIN_X = 0,
	JS_HUD_ROSTER_PLAYER_ORIGIN_Y,
	JS_HUD_ROSTER_PLAYER_ORIGIN_Z,
	JS_HUD_ROSTER_PLAYER_RADAR_X,
	JS_HUD_ROSTER_PLAYER_RADAR_Y,
};

enum JS_HUD_EventMetaField
{
	JS_HUD_EVENT_META_LATEST_SEQ = 0,
	JS_HUD_EVENT_META_EVENT_COUNT,
};

enum JS_HUD_EventIntField
{
	JS_HUD_EVENT_INT_SEQ = 0,
	JS_HUD_EVENT_INT_KIND,
	JS_HUD_EVENT_INT_TIME_MS,
	JS_HUD_EVENT_INT_STATE,
	JS_HUD_EVENT_INT_KILLER_ID,
	JS_HUD_EVENT_INT_VICTIM_ID,
	JS_HUD_EVENT_INT_KILLER_TEAM,
	JS_HUD_EVENT_INT_VICTIM_TEAM,
	JS_HUD_EVENT_INT_ASSISTER_ID,
	JS_HUD_EVENT_INT_ASSISTER_TEAM,
	JS_HUD_EVENT_INT_HEADSHOT,
	JS_HUD_EVENT_INT_RARITY_FLAGS,
};

enum JS_HUD_EventTextField
{
	JS_HUD_EVENT_TEXT_WEAPON = 0,
	JS_HUD_EVENT_TEXT_KILLER_NAME,
	JS_HUD_EVENT_TEXT_ASSISTER_NAME,
	JS_HUD_EVENT_TEXT_VICTIM_NAME,
	JS_HUD_EVENT_TEXT_TEXT,
};

enum JS_HUD_DebugCounterField
{
	JS_HUD_DEBUG_ROSTER_SNAPSHOT_COUNT = 0,
	JS_HUD_DEBUG_ROSTER_PLAYER_COUNT,
	JS_HUD_DEBUG_SCORE_INFO_COUNT,
	JS_HUD_DEBUG_TEAM_INFO_COUNT,
	JS_HUD_DEBUG_RADAR_COUNT,
	JS_HUD_DEBUG_DEATH_MSG_COUNT,
	JS_HUD_DEBUG_ROUND_MSG_COUNT,
	JS_HUD_DEBUG_LATEST_EVENT_SEQ,
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t JS_HUD_GetABIVersion( void );
uint32_t JS_HUD_GetSnapshotSize( void );
int JS_HUD_GetSnapshot( JS_HUD_SnapshotV1 *out );
const JS_HUD_SnapshotV1 *JS_HUD_GetSnapshotPtr( void );
int JS_HUD_GetRoundTimer( void );
int JS_HUD_GetBombTimer( void );
int JS_HUD_GetWeapon( void );
uint32_t JS_HUD_GetWeaponBits( void );
int JS_HUD_GetClip( void );
int JS_HUD_GetReserve( void );
int JS_HUD_GetMoney( void );
int JS_HUD_GetHealthArmor( void ); // lower 16 bits = health, upper 16 bits = armor
int JS_HUD_GetFlags( void );
int JS_HUD_GetFfaBonusWeapon( void );      // CounterSol: current FFA bonus WeaponIdType (0 = none)
int JS_HUD_GetFfaBonusSecondsLeft( void ); // CounterSol: seconds until the FFA bonus rotates
uint32_t JS_HUD_GetCrosshairStateSize( void );
int JS_HUD_GetCrosshairState( JS_HUD_CrosshairStateV1 *out );
const JS_HUD_CrosshairStateV1 *JS_HUD_GetCrosshairStatePtr( void );
// Heap-free scalar crosshair accessors, for runtimes where Module.HEAPU8 is not
// readable from the overlay. Call JS_HUD_BuildCrosshairState() once per frame,
// then read fields via the Int/Float getters (see JS_HUD_CrosshairIntField /
// JS_HUD_CrosshairFloatField). Mirrors the roster scalar accessor pattern.
int JS_HUD_BuildCrosshairState( void );
int JS_HUD_GetCrosshairInt( int field );
float JS_HUD_GetCrosshairFloat( int field );
uint32_t JS_HUD_GetRosterSnapshotSize( void );
int JS_HUD_GetRosterSnapshot( JS_HUD_RosterSnapshotV1 *out );
const JS_HUD_RosterSnapshotV1 *JS_HUD_GetRosterSnapshotPtr( void );
int JS_HUD_BuildRosterSnapshot( void );
int JS_HUD_GetRosterMeta( int field );
int JS_HUD_GetRosterPlayerInt( int slot, int field );
float JS_HUD_GetRosterPlayerFloat( int slot, int field );
uint32_t JS_HUD_GetRosterPlayerNamePacked( int slot, int chunk );
int JS_HUD_BuildObserverPov( void );
int JS_HUD_GetObserverPovInt( int field );
uint32_t JS_HUD_GetEventBufferSize( void );
int JS_HUD_GetEvents( uint32_t after_seq, JS_HUD_EventBufferV1 *out );
const JS_HUD_EventBufferV1 *JS_HUD_GetEventsPtr( uint32_t after_seq );
int JS_HUD_BuildEvents( uint32_t after_seq );
int JS_HUD_GetEventMeta( int field );
int JS_HUD_GetEventInt( int slot, int field );
uint32_t JS_HUD_GetEventTextPacked( int slot, int text_field, int chunk );
void JS_HUD_ResetEvents( void );
void JS_HUD_ResetRosterMirror( void );
void JS_HUD_RecordScoreInfo( int player, int frags, int deaths, int playerclass, int teamnumber );
void JS_HUD_RecordAssistInfo( int player, int assists );
void JS_HUD_RecordTeamInfo( int player, const char *team_name, int teamnumber );
void JS_HUD_RecordRadarPosition( int player, float x, float y, float z );
void JS_HUD_RecordKillEvent( int killer, int victim, int headshot, const char *weapon, int assister, int rarityFlags );
void JS_HUD_RecordHitEvent( int victim, int damage, int headshot, int victimHealth, int flags );
void JS_HUD_RecordFfaBonus( int weaponId, int secondsLeft ); // CounterSol
void JS_HUD_RecordRoundTextEvent( int msg_dest, const char *raw_text, const char *resolved_text );
void JS_HUD_RecordChatEvent( int player, int flags, const char *player_name, const char *text );
void JS_HUD_RecordVoiceStatus( int entindex, int talking );
void JS_HUD_RecordDeathStats(
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
);
int JS_HUD_GetDeathStatsMeta( int field );
int JS_HUD_GetDeathStatsRowInt( int group, int slot, int field );
uint32_t JS_HUD_GetDeathStatsTextPacked( int text_field, int chunk );
uint32_t JS_HUD_GetDeathStatsRowNamePacked( int group, int slot, int chunk );
uint32_t JS_HUD_GetDebugCountersSize( void );
int JS_HUD_GetDebugCounters( JS_HUD_DebugCountersV1 *out );
const JS_HUD_DebugCountersV1 *JS_HUD_GetDebugCountersPtr( void );
int JS_HUD_BuildDebugCounters( void );
int JS_HUD_GetDebugCounter( int field );

#ifdef __cplusplus
}
#endif
