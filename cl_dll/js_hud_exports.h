#pragma once

#include <stdint.h>

#define JS_HUD_ABI_VERSION_1 0x00010000u

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
} JS_HUD_SnapshotV1;

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t JS_HUD_GetABIVersion( void );
uint32_t JS_HUD_GetSnapshotSize( void );
int JS_HUD_GetSnapshot( JS_HUD_SnapshotV1 *out );
int JS_HUD_GetRoundTimer( void );
int JS_HUD_GetWeapon( void );
int JS_HUD_GetMoney( void );
int JS_HUD_GetHealthArmor( void ); // lower 16 bits = health, upper 16 bits = armor
int JS_HUD_GetFlags( void );

#ifdef __cplusplus
}
#endif
