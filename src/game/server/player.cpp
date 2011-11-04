#include <new>

#include <engine/e_server_interface.h>

#include "player.hpp"
#include "gamecontext.hpp"

MACRO_ALLOC_POOL_ID_IMPL(PLAYER, MAX_CLIENTS)

PLAYER::PLAYER(int client_id)
{
	respawn_tick = server_tick();
	character = 0;
	this->client_id = client_id;
	this->motd_sent = false;
	this->motd_tick = server_tick() + server_tickspeed() * 3;
	spamcount = 0;
	buy_time = -1;
	this->last_input = server_tick();
	is_teamchanging = false;
	enter_game = false;
	broadcast_count = 0;
	
	/* SQL */
	// set account data
	logged_in = false;
	lvl = 1;
	
	acc_reset();
}

PLAYER::~PLAYER()
{
	delete character;
	character = 0;
}

void PLAYER::tick()
{
	if(motd_tick <= server_tick() && !motd_sent)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "Buy Mod - Please type /info!");
		game.send_broadcast(buf, client_id);
		motd_sent = true;
	}

	if(buy_time > 0)
		buy_time--;
	if(buy_time == 0)
	{
		game.send_chat_target(client_id, "Trade aborted!");
		buy_time = -1;
		buy_id = -1;
		buy_price = -1;
	}

	if(broadcast_count > 0)
		broadcast_count--;

	server_setclientscore(client_id, score);

	/* SQL */
	// login the player
	if(game.account_data->logged_in[client_id] && !logged_in)
	{
		acc_data.kills = game.account_data->kills[client_id];
		acc_data.deaths = game.account_data->deaths[client_id];
		acc_data.betrayals = game.account_data->betrayals[client_id];
		acc_data.id = game.account_data->id[client_id];
		acc_data.money = game.account_data->money[client_id];
		acc_data.exp = game.account_data->exp[client_id];
		acc_data.cup = game.account_data->cup[client_id];
		for(int i = 0; i < MAX_CLIENTS; i++)
			acc_data.upgrade[i] = game.account_data->upgrade[i][client_id];
		acc_data.rang = game.account_data->rang[client_id];
		
		// join game
		if(!game.controller->is_cup)
		{
			set_lvl(acc_data.exp, true);
			team = game.controller->get_auto_team(client_id);
		}
		
		logged_in = true;
	}
	
	// do latency stuff
	{
		CLIENT_INFO info;
		if(server_getclientinfo(client_id, &info))
		{
			latency.accum += info.latency;
			latency.accum_max = max(latency.accum_max, info.latency);
			latency.accum_min = min(latency.accum_min, info.latency);
		}

		if(server_tick()%server_tickspeed() == 0)
		{
			latency.avg = latency.accum/server_tickspeed();
			latency.max = latency.accum_max;
			latency.min = latency.accum_min;
			latency.accum = 0;
			latency.accum_min = 1000;
			latency.accum_max = 0;
		}
	}
	
	if(!character && die_tick+server_tickspeed()*3 <= server_tick())
		spawning = true;

	if(character)
	{
		if(character->alive)
		{
			view_pos = character->pos;
		}
		else
		{
			delete character;
			character = 0;
		}
	}
	else if(spawning && respawn_tick <= server_tick())
		try_respawn();
}

void PLAYER::snap(int snapping_client)
{
	NETOBJ_CLIENT_INFO *client_info = (NETOBJ_CLIENT_INFO *)snap_new_item(NETOBJTYPE_CLIENT_INFO, client_id, sizeof(NETOBJ_CLIENT_INFO));
	if(logged_in)
	{
		char buf[512];
		if(acc_data.rang)
		{
			char rang[][32]={"","*","**","***","****","*PRO*"};
			str_format(buf, sizeof(buf),  "[Lvl:%d %s]%s", lvl, rang[acc_data.rang], server_clientname(client_id));
		}
		else
			str_format(buf, sizeof(buf),  "[Lvl:%d]%s", lvl, server_clientname(client_id));
		str_to_ints(&client_info->name0, 6, buf);
	}
	else
		str_to_ints(&client_info->name0, 6, "[NOT LOGGED IN]");
	str_to_ints(&client_info->skin0, 6, skin_name);
	client_info->use_custom_color = use_custom_color;
	client_info->color_body = color_body;
	client_info->color_feet = color_feet;

	//rainbow
	static int rainbow_color = 0;
	if(acc_data.upgrade[14] == 1)
	{
		client_info->use_custom_color = 1;
		rainbow_color = (rainbow_color + 1) % 256;
		client_info->color_body = rainbow_color * 0x010000 + 0xff00;
		client_info->color_feet = rainbow_color * 0x010000 + 0xff00;
	}

	if(freeze || balloon)
	{
		client_info->use_custom_color = 1;
		client_info->color_body = 0;
		client_info->color_feet = 16777215;
		str_to_ints(&client_info->skin0, 6, "pinky");
	}

	NETOBJ_PLAYER_INFO *info = (NETOBJ_PLAYER_INFO *)snap_new_item(NETOBJTYPE_PLAYER_INFO, client_id, sizeof(NETOBJ_PLAYER_INFO));

	info->latency = latency.min;
	info->latency_flux = latency.max-latency.min;
	info->local = 0;
	info->cid = client_id;
	info->score = score;
	info->team = team;

	if(client_id == snapping_client)
		info->local = 1;	
}

void PLAYER::on_disconnect()
{
	kill_character(WEAPON_GAME);
	
	//game.controller->on_player_death(&game.players[client_id], 0, -1);
		
	char buf[512];
	str_format(buf, sizeof(buf),  "%s has left the game", server_clientname(client_id));
	game.send_chat(-1, GAMECONTEXT::CHAT_ALL, buf);

	dbg_msg("game", "leave player='%d:%s'", client_id, server_clientname(client_id));
}

void PLAYER::on_predicted_input(NETOBJ_PLAYER_INPUT *new_input)
{
	CHARACTER *chr = get_character();
	if(chr)
		chr->on_predicted_input(new_input);
}

void PLAYER::on_direct_input(NETOBJ_PLAYER_INPUT *new_input)
{
	CHARACTER *chr = get_character();
	if(chr)
		chr->on_direct_input(new_input);

	if(!chr && team >= 0 && (new_input->fire&1))
		spawning = true;
	
	if(!chr && team == -1)
		view_pos = vec2(new_input->target_x, new_input->target_y);
}

CHARACTER *PLAYER::get_character()
{
	if(character && character->alive)
		return character;
	return 0;
}

void PLAYER::kill_character(int weapon)
{
	//CHARACTER *chr = get_character();
	if(character)
	{
		character->die(client_id, weapon);
		delete character;
		character = 0;
	}
}

void PLAYER::respawn()
{
	if(team > -1)
		spawning = true;
}

void PLAYER::set_team(int new_team)
{
	if(!logged_in)
		return;

	char buf[512];
	if(game.controller->is_cup && game.controller->potcount > 0 && team == -1)
	{
		int price = acc_data.cup*10+10;
		char buf[512];
		if(acc_data.money >= price)
		{
			str_format(buf, sizeof(buf), "You PAID %d $ for entering the game!", price);
			game.send_broadcast(buf, client_id);
			acc_data.money -= price;
			game.controller->jackpot += price;
			enter_game = true;
		}
		else
		{
			str_format(buf, sizeof(buf), "You NEED %d $ for entering the game!", price);
			game.send_broadcast(buf, client_id);
			return;
		}
	}
	else if(game.controller->is_cup && game.controller->potcount < 1 && team == -1)
	{
		game.send_broadcast("Wait for next round!", client_id);
		return;
	}
	else if(game.controller->is_cup && team != -1)
	{
		game.send_broadcast("You left the Game!", client_id);
		enter_game = false;
	}


	
	// clamp the team
	new_team = game.controller->clampteam(new_team);
	if(team == new_team)
		return;

	// set the player team changing
	is_teamchanging = true;

	if(team == -1 && !game.controller->is_cup)
		set_lvl(acc_data.exp, true);
		
	str_format(buf, sizeof(buf), "%s joined the %s", server_clientname(client_id), game.controller->get_team_name(new_team));
	game.send_chat(-1, GAMECONTEXT::CHAT_ALL, buf); 
	
	kill_character(WEAPON_GAME);
	team = new_team;
	score = 0;
	dbg_msg("game", "team_join player='%d:%s' team=%d", client_id, server_clientname(client_id), team);
	
	game.controller->on_player_info_change(game.players[client_id]);

	// teamchanging done
	is_teamchanging = false;
}

void PLAYER::try_respawn()
{
	vec2 spawnpos = vec2(100.0f, -60.0f);
	
	if(!game.controller->can_spawn(this, &spawnpos))
		return;

	// check if the position is occupado
	ENTITY *ents[2] = {0};
	int num_ents = game.world.find_entities(spawnpos, 64, ents, 2, NETOBJTYPE_CHARACTER);
	
	if(num_ents == 0)
	{
		spawning = false;
		character = new(client_id) CHARACTER();
		character->spawn(this, spawnpos, team);
		game.create_playerspawn(spawnpos);
	}
}

void PLAYER::set_lvl(int userexp, bool is_joined)
{
	int level[300] = {0,0};
	for(int i = 2; i < 300; i++)
	{
		level[i] = 100*(i-1);
		//level[i] = level[i-1]+5+(i*5);
		//dbg_msg("game", "%d: %d", i, level[i]);
	}

	char buf[512];
	for(int i = 2; i < 299; i++)
	{
		if(userexp >= level[i] && userexp < level[i+1] && lvl != i)
		{
			lvl = i;
			if(!is_joined)
			{
				str_format(buf, sizeof(buf), "You reached level %d!", i);
				game.send_broadcast(buf, client_id);
			}
		}
	}
	str_format(buf, sizeof(buf), "Next Level[%d]: %d %%", lvl+1, 100-(level[lvl+1]-(int)acc_data.exp));
	game.send_chat_target(client_id, buf);

	str_format(buf, sizeof(buf), "Money: %d $", acc_data.money);
	game.send_chat_target(client_id, buf);
}

void PLAYER::buy_upgrade(int upgrade_id)
{
	int price = game.controller->upgrade_price[upgrade_id];
	char buf[512];
	
	if(price <= 0)
	{
		game.send_broadcast("Thats not available!", client_id);
		game.create_sound_global(SOUND_CTF_GRAB_EN, client_id);
		return;
	}
	if(acc_data.money >= price)
	{
		if(acc_data.upgrade[upgrade_id] == 1 && !(upgrade_id == HEALTH || upgrade_id == ARMOR || upgrade_id == BEGIN_ARMOR))
		{
			game.send_broadcast("You already bought this upgrade!", client_id);
			game.create_sound_global(SOUND_CTF_GRAB_EN, client_id);
			return;
		}
		if(lvl < game.controller->upgrade_lvl[upgrade_id])
		{
			game.send_broadcast("You need a higher level!", client_id);
			game.create_sound_global(SOUND_CTF_GRAB_EN, client_id);
			return;
		}
		if(acc_data.cup < game.controller->upgrade_cup[upgrade_id])
		{
			game.send_broadcast("You need more cup-points!", client_id);
			game.create_sound_global(SOUND_CTF_GRAB_EN, client_id);
			return;
		}
		if(upgrade_id == HEALTH || upgrade_id == ARMOR || upgrade_id == BEGIN_ARMOR)
		{
			if(acc_data.upgrade[upgrade_id] < 5 && (upgrade_id == HEALTH || upgrade_id == ARMOR))
			{
				acc_data.upgrade[upgrade_id]++;
				if(upgrade_id == HEALTH)
					str_format(buf, sizeof(buf), "You bought Health++ [%d/5]!", acc_data.upgrade[upgrade_id]);
				else if(upgrade_id == ARMOR)
					str_format(buf, sizeof(buf), "You bought Armor++ [%d/5]!", acc_data.upgrade[upgrade_id]);
				game.send_broadcast(buf, client_id);
			}
			else if(upgrade_id == BEGIN_ARMOR && acc_data.upgrade[upgrade_id] < 15)
			{
				if(acc_data.upgrade[upgrade_id] >= 10 && acc_data.upgrade[upgrade_id]-(acc_data.upgrade[ARMOR]+10) == 0)
				{
					game.send_broadcast("This upgrade is no more buyable!", client_id);
					game.create_sound_global(SOUND_CTF_GRAB_EN, client_id);
					return;
				}
				else
				{
					acc_data.upgrade[upgrade_id]++;
					str_format(buf, sizeof(buf), "You bought Begin-Armor++ [%d/%d]!", acc_data.upgrade[upgrade_id], acc_data.upgrade[ARMOR]+10);
					game.send_broadcast(buf, client_id);
				}
			}
			else
			{
				game.send_broadcast("This upgrade is no more buyable!", client_id);
				game.create_sound_global(SOUND_CTF_GRAB_EN, client_id);
				return;
			}
		}
		else
		{
			acc_data.upgrade[upgrade_id] = 1;
			str_format(buf, sizeof(buf), "You bought upgrade %d!", upgrade_id);
			game.send_broadcast(buf, client_id);
		}

		acc_data.money -= price;
		str_format(buf, sizeof(buf), "You got %d money left!", acc_data.money);
		game.send_chat_target(client_id, buf);
		game.create_sound_global(SOUND_CTF_CAPTURE, client_id);
		dbg_msg("buy", "upgrade: %d price: %d user-id: %d", upgrade_id, price, acc_data.id);
	}
	else
	{
		game.send_broadcast("You got not enough money!", client_id);
		game.create_sound_global(SOUND_CTF_GRAB_EN, client_id);
	}
}

/* SQL */
void PLAYER::acc_reset()
{
	acc_data.kills = 0;
	acc_data.deaths = 0;
	acc_data.betrayals = 0;
	acc_data.id = -1;
	acc_data.exp = 0;
	acc_data.money = 0;
	acc_data.cup = 0;
	for(int i=0; i<30; i++)
		acc_data.upgrade[i] = 0;
	acc_data.rang = 0;
	
	// rest account_data struct too
	game.account_data->kills[client_id] = 0;
	game.account_data->deaths[client_id] = 0;
	game.account_data->betrayals[client_id] = 0;
	game.account_data->id[client_id] = 0;
	game.account_data->logged_in[client_id] = false;
	game.account_data->exp[client_id] = 0;
	game.account_data->money[client_id] = 0;
	game.account_data->cup[client_id] = 0;
	for(int i=0; i<30; i++)
		game.account_data->upgrade[i][client_id] = 0;
	game.account_data->rang[client_id] = 0;

	lvl = 1;
	str_copy(password, "", MAX_NAME_LENGTH);
	str_copy(username, "", MAX_NAME_LENGTH);
}
