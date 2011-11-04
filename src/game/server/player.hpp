#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.hpp"

// player object
class PLAYER
{
	MACRO_ALLOC_POOL_ID()
private:
	CHARACTER *character;
public:
	PLAYER(int client_id);
	~PLAYER();

	// TODO: clean this up
	char skin_name[64];
	int use_custom_color;
	int color_body;
	int color_feet;
	
	int respawn_tick;
	int die_tick;
	//
	bool spawning;
	int client_id;
	int team;
	bool is_teamchanging;
	int score;
	bool force_balanced;
	
	//
	int vote;
	int64 last_votecall;

	//
	int64 last_chat;
	int64 last_setteam;
	int64 last_changeinfo;
	int64 last_emote;
	int64 last_kill;

	// network latency calculations	
	struct
	{
		int accum;
		int accum_min;
		int accum_max;
		int avg;
		int min;
		int max;	
	} latency;
	
	// this is used for snapping so we know how we can clip the view for the player
	vec2 view_pos;

	void init(int client_id);
	
	CHARACTER *get_character();
	
	void kill_character(int weapon);

	void try_respawn();
	void respawn();
	void set_team(int team);
	
	void tick();
	void snap(int snapping_client);

	void on_direct_input(NETOBJ_PLAYER_INPUT *new_input);
	void on_predicted_input(NETOBJ_PLAYER_INPUT *new_input);
	void on_disconnect();
	
	/* Account data */
	bool logged_in;
	
	// reset local acc data
	void acc_reset();
	
	enum
	{
		SHOTGUN=0,
		LASER,
		GRENADE,  
		NINJA,
		BOUNCE_GRENADE,
		BOUNCE_SHOTGUN,
		BOUNCE_GUN,
		NINJA_FLIGHT,
		BALLOON_GUN,
		FREEZE_LASER,
		DRAGON_GRENADE,
		PROJECTILE_SHOTGUN,
		DMG_HAMMER,
		DMG_GUN,
		RAINBOW_SKIN,
		EMOTION_BOT,
		KICK_PROTECT,
		INFINITE_AMMO,
		INFINITE_JUMP,
		NO_SELFDAMAGE,
		BEGIN_ARMOR,
		HEALTH,
		ARMOR,
		SPEED_GUN,
		HOOK_DMG,
		LASER_SWAP,
		UNFREEZE_HAMMER ,
		PORTAL_HAMMER,
	};
	// data for database
	struct ACC_DATA
	{
		int kills;
		int deaths;
		int betrayals;
		int id;
		float exp;
		int money;
		int cup;
		int upgrade[30];
		int rang;
	};
	ACC_DATA acc_data;

	// balloon and freeze stuff
	bool balloon;
	int balloon_tick;
	int balloon_id;
	bool freeze;
	int freeze_tick;
	int freeze_id;

	// sell upgrade
	int buy_time;
	int buy_id;
	int buy_price;

	int lvl;
	char password[32];
	char username[32];
	int broadcast_count;
	bool enter_game;

	void set_lvl(int exp, bool is_joined);
	void buy_upgrade(int upgrade_id);

	// spam protection
	int spamcount;
	char old_message[256];

	// beginn msg
	bool motd_sent;
	int motd_tick;

	// inactivity check
	int last_input;

	// portal
	bool port_1;
	bool port_2;
	vec2 port_1_pos;
	vec2 port_2_pos;
	int port_tick;
};

#endif
