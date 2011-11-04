/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */
#include <string.h>
#include <engine/e_config.h>
#include <engine/e_server_interface.h>
#include <game/mapitems.hpp>

#include <game/generated/g_protocol.hpp>

#include "entities/pickup.hpp"
#include "gamecontroller.hpp"
#include "gamecontext.hpp"

#include <stdio.h>

GAMECONTROLLER::GAMECONTROLLER()
{
	gametype = "unknown";
	
	//
	do_warmup(config.sv_warmup);
	game_over_tick = -1;
	sudden_death = 0;
	round_start_tick = server_tick();
	round_count = 0;
	game_flags = 0;
	teamscore[0] = 0;
	teamscore[1] = 0;
	map_wish[0] = 0;
	
	unbalanced_tick = -1;
	force_balanced = false;
	
	num_spawn_points[0] = 0;
	num_spawn_points[1] = 0;
	num_spawn_points[2] = 0;

	slow_tick = -1;
	x2_tick = -1;
	speed_tick = -1;
	one_hit_tick = -1;
	web_tick = -1;

	is_cup = false;
	jackpot = 0;
	potcount = 0;
	do_potcount(35);
	
	/* SQL */
	// create tables
	game.sql->create_tables();
}

GAMECONTROLLER::~GAMECONTROLLER()
{
}

float GAMECONTROLLER::evaluate_spawn_pos(SPAWNEVAL *eval, vec2 pos)
{
	float score = 0.0f;
	CHARACTER *c = (CHARACTER *)game.world.find_first(NETOBJTYPE_CHARACTER);
	for(; c; c = (CHARACTER *)c->typenext())
	{
		// team mates are not as dangerous as enemies
		float scoremod = 1.0f;
		if(eval->friendly_team != -1 && c->team == eval->friendly_team)
			scoremod = 0.5f;
			
		float d = distance(pos, c->pos);
		if(d == 0)
			score += 1000000000.0f;
		else
			score += 1.0f/d;
	}
	
	return score;
}

void GAMECONTROLLER::evaluate_spawn_type(SPAWNEVAL *eval, int t)
{
	// get spawn point
	for(int i  = 0; i < num_spawn_points[t]; i++)
	{
		vec2 p = spawn_points[t][i];
		float s = evaluate_spawn_pos(eval, p);
		if(!eval->got || eval->score > s)
		{
			eval->got = true;
			eval->score = s;
			eval->pos = p;
		}
	}
}

bool GAMECONTROLLER::can_spawn(PLAYER *player, vec2 *out_pos)
{
	SPAWNEVAL eval;
	
	// spectators can't spawn
	if(player->team == -1)
		return false;
	
	if(is_teamplay())
	{
		eval.friendly_team = player->team;
		
		// try first try own team spawn, then normal spawn and then enemy
		evaluate_spawn_type(&eval, 1+(player->team&1));
		if(!eval.got)
		{
			evaluate_spawn_type(&eval, 0);
			if(!eval.got)
				evaluate_spawn_type(&eval, 1+((player->team+1)&1));
		}
	}
	else
	{
		evaluate_spawn_type(&eval, 0);
		evaluate_spawn_type(&eval, 1);
		evaluate_spawn_type(&eval, 2);
	}
	
	*out_pos = eval.pos;
	return eval.got;
}


bool GAMECONTROLLER::on_entity(int index, vec2 pos)
{
	int type = -1;
	int subtype = 0;
	
	if(index == ENTITY_SPAWN)
		spawn_points[0][num_spawn_points[0]++] = pos;
	else if(index == ENTITY_SPAWN_RED)
		spawn_points[1][num_spawn_points[1]++] = pos;
	else if(index == ENTITY_SPAWN_BLUE)
		spawn_points[2][num_spawn_points[2]++] = pos;
	else if(index == ENTITY_ARMOR_1)
		type = POWERUP_ARMOR;
	else if(index == ENTITY_HEALTH_1)
		type = POWERUP_HEALTH;
	else if(index == ENTITY_WEAPON_SHOTGUN)
	{
		type = POWERUP_WEAPON;
		subtype = WEAPON_SHOTGUN;
	}
	else if(index == ENTITY_WEAPON_GRENADE)
	{
		type = POWERUP_WEAPON;
		subtype = WEAPON_GRENADE;
	}
	else if(index == ENTITY_WEAPON_RIFLE)
	{
		type = POWERUP_WEAPON;
		subtype = WEAPON_RIFLE;
	}
	else if(index == ENTITY_POWERUP_NINJA && config.sv_powerups)
	{
		type = POWERUP_NINJA;
		subtype = WEAPON_NINJA;
	}
	
	if(type != -1)
	{
		PICKUP *pickup = new PICKUP(type, subtype);
		pickup->pos = pos;
		return true;
	}

	return false;
}

void GAMECONTROLLER::endround()
{
	if(warmup) // game can't end when we are running warmup
		return;
		
	game.world.paused = true;

	// save all accounts here to if there is no map change
	if(!strcmp(config.sv_map, map_wish))
		game.sql->update_all();

	game_over_tick = server_tick();
	sudden_death = 0;
	give_jackpot();
}

void GAMECONTROLLER::resetgame()
{
	game.world.reset_requested = true;
}

const char *GAMECONTROLLER::get_team_name(int team)
{
	if(is_teamplay())
	{
		if(team == 0)
			return "red team";
		else if(team == 1)
			return "blue team";
	}
	else
	{
		if(team == 0)
			return "game";
	}
	
	return "spectators";
}

static bool is_separator(char c) { return c == ';' || c == ' ' || c == ',' || c == '\t'; }

void GAMECONTROLLER::startround()
{
	resetgame();
	
	round_start_tick = server_tick();
	sudden_death = 0;
	game_over_tick = -1;
	game.world.paused = false;
	teamscore[0] = 0;
	teamscore[1] = 0;
	unbalanced_tick = -1;
	force_balanced = false;
	dbg_msg("game","start round type='%s' teamplay='%d'", gametype, game_flags&GAMEFLAG_TEAMS);
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(game.players[i])
			if(game.players[i]->enter_game)
			{
				game.players[i]->team = -1;
				game.players[i]->enter_game = false;
			}
	do_potcount(31);
}

void GAMECONTROLLER::do_potcount(int sec)
{
	potcount = sec*50;
}
void GAMECONTROLLER::change_map(const char *to_map)
{
	str_copy(map_wish, to_map, sizeof(map_wish));
	endround();
}

void GAMECONTROLLER::cyclemap()
{
	if(map_wish[0] != 0)
	{
		dbg_msg("game", "rotating map to %s", map_wish);
		str_copy(config.sv_map, map_wish, sizeof(config.sv_map));
		map_wish[0] = 0;
		round_count = 0;
		return;
	}
	if(!strlen(config.sv_maprotation))
		return;

	if(round_count < config.sv_rounds_per_map-1)
		return;
		
	// handle maprotation
	const char *map_rotation = config.sv_maprotation;
	const char *current_map = config.sv_map;
	
	int current_map_len = strlen(current_map);
	const char *next_map = map_rotation;
	while(*next_map)
	{
		int wordlen = 0;
		while(next_map[wordlen] && !is_separator(next_map[wordlen]))
			wordlen++;
		
		if(wordlen == current_map_len && strncmp(next_map, current_map, current_map_len) == 0)
		{
			// map found
			next_map += current_map_len;
			while(*next_map && is_separator(*next_map))
				next_map++;
				
			break;
		}
		
		next_map++;
	}
	
	// restart rotation
	if(next_map[0] == 0)
		next_map = map_rotation;

	// cut out the next map	
	char buf[512];
	for(int i = 0; i < 512; i++)
	{
		buf[i] = next_map[i];
		if(is_separator(next_map[i]) || next_map[i] == 0)
		{
			buf[i] = 0;
			break;
		}
	}
	
	// skip spaces
	int i = 0;
	while(is_separator(buf[i]))
		i++;
	
	round_count = 0;
	
	dbg_msg("game", "rotating map to %s", &buf[i]);
	str_copy(config.sv_map, &buf[i], sizeof(config.sv_map));
}

void GAMECONTROLLER::post_reset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(game.players[i])
		{
			game.players[i]->respawn();
			game.players[i]->score = 0;
		}
	}
}
	
void GAMECONTROLLER::on_player_info_change(class PLAYER *p)
{
	const int team_colors[2] = {65387, 10223467};
	if(is_teamplay())
	{
		if(p->team >= 0 || p->team <= 1)
		{
			p->use_custom_color = 1;
			p->color_body = team_colors[p->team];
			p->color_feet = team_colors[p->team];
		}
	}
}


int GAMECONTROLLER::on_character_death(class CHARACTER *victim, class PLAYER *killer, int weapon)
{
	// do scoreing
	if(!killer)
		return 0;
	if(killer == victim->player)
	{
		if(victim->player->logged_in && !victim->player->is_teamchanging)
		{
			victim->player->acc_data.deaths++;
			if(victim->player->acc_data.exp > 0)
				victim->player->acc_data.exp -= 1;
		}
			
		victim->player->score--; // suicide
	}
	else
	{
		if(is_teamplay() && victim->team == killer->team)
		{
			if(killer->logged_in)
				killer->acc_data.betrayals++;
				
			killer->score--; // teamkill
			
			if(victim->player->logged_in)
				victim->player->acc_data.deaths++;
		}
		else
		{
			if(killer->logged_in)
				killer->acc_data.kills++;
				
			killer->score++; // normal kill
			
			if(victim->player->logged_in)
				victim->player->acc_data.deaths++;
		}
	}
	return 0;
}

void GAMECONTROLLER::on_character_spawn(class CHARACTER *chr)
{
	// give health
	if(chr->player->acc_data.upgrade[chr->player->HEALTH] != 0 && !is_cup)
		chr->health = chr->player->acc_data.upgrade[chr->player->HEALTH]+10;
	else 
		chr->health = 10;
	
	// give default weapons
	chr->weapons[WEAPON_HAMMER].got = 1;
	chr->weapons[WEAPON_HAMMER].ammo = -1;
	chr->weapons[WEAPON_GUN].got = 1;
	chr->weapons[WEAPON_GUN].ammo = 10;

	// give armor
	if(chr->player->acc_data.upgrade[chr->player->BEGIN_ARMOR] != 0 && !is_cup)
	{
		if(chr->player->acc_data.upgrade[chr->player->ARMOR] != 0 && chr->player->acc_data.upgrade[chr->player->BEGIN_ARMOR] == 10)
			chr->armor = chr->player->acc_data.upgrade[chr->player->ARMOR] + chr->player->acc_data.upgrade[chr->player->BEGIN_ARMOR];
		else 
			chr->armor = chr->player->acc_data.upgrade[chr->player->BEGIN_ARMOR];
	}
}

void GAMECONTROLLER::do_warmup(int seconds)
{
	warmup = seconds*server_tickspeed();
}

bool GAMECONTROLLER::is_friendly_fire(int cid1, int cid2)
{
	if(cid1 == cid2)
		return false;
	
	if(is_teamplay())
	{
		if(!game.players[cid1] || !game.players[cid2])
			return false;
			
		if(game.players[cid1]->team == game.players[cid2]->team)
			return true;
	}
	
	return false;
}

bool GAMECONTROLLER::is_force_balanced()
{
	if(force_balanced)
	{
		force_balanced = false;
		return true;
	}
	else
		return false;
}

void GAMECONTROLLER::tick()
{
	// do info message every 10 minutes
	if(server_tick()%(50*10*60) == 0)
	{
		game.send_chat_target(-1, "-------------------------");
		game.send_chat_target(-1, "Buy Mod - 0.1");
		game.send_chat_target(-1, "by ©Bobynator and ©KaiTee");
		game.send_chat_target(-1, "/cmdlist - see all commands");
		game.send_chat_target(-1, "/info - see aim of the game");
		game.send_chat_target(-1, "-------------------------");
	}

	// inactivity check
	if(!game.world.paused)
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(game.players[i])
				if(server_tick()%server_tickspeed() == 0 && game.players[i]->last_input != -1 && server_tick() - game.players[i]->last_input >= 120 * server_tickspeed())
					if(server_tick() - game.players[i]->last_input >= 120 * server_tickspeed())
					{
						game.players[i]->set_team(-1);  // set him to spectator
						game.send_chat_target(i, "Inactivity Check: Are you still there?");  //portal ftw ;D
					}

	// event stuff
	// TODO: write event over stuff smarter!
	if(slow_tick > 0)
		slow_tick--;
	if(slow_tick == 0)
	{
		console_execute_line("tune_reset");
		slow_tick = -1;
		game.send_broadcast("EVENT OVER!", -1);
	}

	if(speed_tick > 0)
		speed_tick--;
	if(speed_tick == 0)
	{
		console_execute_line("tune_reset");
		speed_tick = -1;
		game.send_broadcast("EVENT OVER!", -1);
	}

	if(x2_tick > 0)
		x2_tick--;
	if(x2_tick == 0)
	{
		x2_tick = -1;
		game.send_broadcast("EVENT OVER!", -1);
	}

	if(one_hit_tick > 0)
		one_hit_tick--;
	if(one_hit_tick == 0)
	{
		one_hit_tick = -1;
		game.send_broadcast("EVENT OVER!", -1);
	}

	if(web_tick > 0)
		web_tick--;
	if(web_tick == 0)
	{
		web_tick = -1;
		game.send_broadcast("EVENT OVER!", -1);
	}

	if(server_tick()%(50*60*60) == 0 && !is_cup)
	{
		int Event = 1+(rand()%(5-1+1));
		if(Event == 1)
			slowmotion(5);
		else if(Event == 2)
			x2(2);
		else if(Event == 3)
			one_hit(1);
		else if(Event == 4)
			speedmotion(3);
		else if(Event == 5)
			spider_web(4);
	}
	
	// do warmup
	if(warmup)
	{
		warmup--;
		if(!warmup)
			startround();
	}

	// do potcount
	int enter_players = 0;
	if(is_cup)
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(game.players[i])
			{
				if(game.players[i]->enter_game == true)
					enter_players++;
			}
	if(potcount < 1 && is_cup && game_over_tick == -1)
	{
		if(server_tick()%(50*6) == 0 && enter_players > 1 && jackpot > 0)
		{
			char buf[512];
			str_format(buf, sizeof(buf), "Jackpot: %d $", jackpot);
			game.send_broadcast(buf, -1, true);
		}
		if(enter_players == 1)
		{
			endround();
		}
	}
	if(potcount > 0 && is_cup)
	{
		potcount--;
		char buf[512];
		str_format(buf, sizeof(buf), "Jackpot: %d $ | Join-Time: %d", jackpot, potcount/50);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(game.players[i])
			{
				if(game.players[i]->broadcast_count < 1)
					game.send_broadcast(buf, i, true);
			}
		}
	}
	if(potcount == 0 && is_cup)
	{
		if(enter_players >= 2)
		{
			game.send_broadcast("Game Start!", -1);
			resetgame();
			potcount--;
			char buf[512];
			str_format(buf, sizeof(buf), "sv_scorelimit %d", enter_players*5);
			console_execute_line(buf);

			// start game
			round_start_tick = server_tick();
			sudden_death = 0;
			game_over_tick = -1;
			game.world.paused = false;
			teamscore[0] = 0;
			teamscore[1] = 0;
			unbalanced_tick = -1;
			force_balanced = false;
		}
		else
			do_potcount(16);
	}
	
	if(game_over_tick != -1)
	{
		// game over.. wait for restart
		if(server_tick() > game_over_tick+server_tickspeed()*10)
		{
			cyclemap();
			startround();
			round_count++;
		}
	}
	
	// do team-balancing
	if (is_teamplay() && unbalanced_tick != -1 && server_tick() > unbalanced_tick+config.sv_teambalance_time*server_tickspeed()*60)
	{
		dbg_msg("game", "Balancing teams");
		
		int t[2] = {0,0};
		int tscore[2] = {0,0};
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(game.players[i] && game.players[i]->team != -1)
			{
				t[game.players[i]->team]++;
				tscore[game.players[i]->team]+=game.players[i]->score;
			}
		}
		
		// are teams unbalanced?
		if(abs(t[0]-t[1]) >= 2)
		{
			int m = (t[0] > t[1]) ? 0 : 1;
			int num_balance = abs(t[0]-t[1]) / 2;
			
			do
			{
				PLAYER *p = 0;
				int pd = tscore[m];
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!game.players[i])
						continue;
					
					// remember the player who would cause lowest score-difference
					if(game.players[i]->team == m && (!p || abs((tscore[m^1]+game.players[i]->score) - (tscore[m]-game.players[i]->score)) < pd))
					{
						p = game.players[i];
						pd = abs((tscore[m^1]+p->score) - (tscore[m]-p->score));
					}
				}
				
				// move the player to other team without losing his score
				// TODO: change in player::set_team needed: player won't lose score on team-change
				int score_before = p->score;
				p->set_team(m^1);
				p->score = score_before;
				
				p->respawn();
				p->force_balanced = true;
			} while (--num_balance);
			
			force_balanced = true;
		}
		unbalanced_tick = -1;
	}
	
	// update browse info
	int prog = -1;
	if(config.sv_timelimit > 0)
		prog = max(prog, (server_tick()-round_start_tick) * 100 / (config.sv_timelimit*server_tickspeed()*60));

	if(config.sv_scorelimit)
	{
		if(is_teamplay())
		{
			prog = max(prog, (teamscore[0]*100)/config.sv_scorelimit);
			prog = max(prog, (teamscore[1]*100)/config.sv_scorelimit);
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(game.players[i])
					prog = max(prog, (game.players[i]->score*100)/config.sv_scorelimit);
			}
		}
	}

	if(warmup)
		prog = -1;
		
	server_setbrowseinfo(gametype, prog);
}

void GAMECONTROLLER::give_jackpot()
{
	int top_id = -1;
	int top_score = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(game.players[i])
		{
			if(game.players[i]->enter_game)
			{
				if(game.players[i]->score > top_score)
				{
					top_id = i;
					top_score = game.players[i]->score;
				}
			}
		}
	}

	char buf[512];
	str_format(buf, sizeof(buf), "\n\n                                                             %s won:\n                                                             Jackpot (%d $)\n                                                             1 Cup-Point", server_clientname(top_id), jackpot);
	game.send_broadcast(buf, -1);
	game.players[top_id]->acc_data.money += jackpot;
	game.players[top_id]->acc_data.cup++;
	jackpot = 0;

	game.create_sound_global(SOUND_CTF_CAPTURE, -1);
}

bool GAMECONTROLLER::is_teamplay() const
{
	return game_flags&GAMEFLAG_TEAMS;
}

void GAMECONTROLLER::snap(int snapping_client)
{
	NETOBJ_GAME *gameobj = (NETOBJ_GAME *)snap_new_item(NETOBJTYPE_GAME, 0, sizeof(NETOBJ_GAME));
	gameobj->paused = game.world.paused;
	gameobj->game_over = game_over_tick==-1?0:1;
	gameobj->sudden_death = sudden_death;
	
	gameobj->score_limit = config.sv_scorelimit;
	gameobj->time_limit = config.sv_timelimit;
	gameobj->round_start_tick = round_start_tick;
	gameobj->flags = game_flags;
	
	gameobj->warmup = warmup;
	
	gameobj->round_num = (strlen(config.sv_maprotation) && config.sv_rounds_per_map) ? config.sv_rounds_per_map : 0;
	gameobj->round_current = round_count+1;
	
	
	if(snapping_client == -1)
	{
		// we are recording a demo, just set the scores
		gameobj->teamscore_red = teamscore[0];
		gameobj->teamscore_blue = teamscore[1];
	}
	else
	{
		// TODO: this little hack should be removed
		gameobj->teamscore_red = is_teamplay() ? teamscore[0] : game.players[snapping_client]->score;
		gameobj->teamscore_blue = teamscore[1];
	}
}

int GAMECONTROLLER::get_auto_team(int notthisid)
{
	// this will force the auto balancer to work overtime aswell
	if(config.dbg_stress)
		return 0;
	
	int numplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(game.players[i] && i != notthisid)
		{
			if(game.players[i]->team == 0 || game.players[i]->team == 1)
				numplayers[game.players[i]->team]++;
		}
	}

	int team = 0;
	if(is_teamplay())
		team = numplayers[0] > numplayers[1] ? 1 : 0;
		
	if(can_join_team(team, notthisid))
		return team;
	return -1;
}

bool GAMECONTROLLER::can_join_team(int team, int notthisid)
{
	(void)team;
	int numplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(game.players[i] && i != notthisid)
		{
			if(game.players[i]->team >= 0 || game.players[i]->team == 1)
				numplayers[game.players[i]->team]++;
		}
	}
	
	return (numplayers[0] + numplayers[1]) < config.sv_max_clients-config.sv_spectator_slots;
}

bool GAMECONTROLLER::check_team_balance()
{
	if(!is_teamplay() || !config.sv_teambalance_time)
		return true;
	
	int t[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		PLAYER *p = game.players[i];
		if(p && p->team != -1)
			t[p->team]++;
	}
	
	if(abs(t[0]-t[1]) >= 2)
	{
		dbg_msg("game", "Team is NOT balanced (red=%d blue=%d)", t[0], t[1]);
		if (game.controller->unbalanced_tick == -1)
			game.controller->unbalanced_tick = server_tick();
		return false;
	}
	else
	{
		dbg_msg("game", "Team is balanced (red=%d blue=%d)", t[0], t[1]);
		game.controller->unbalanced_tick = -1;
		return true;
	}
}

bool GAMECONTROLLER::can_change_team(PLAYER *pplayer, int jointeam)
{
	int t[2] = {0, 0};
	
	if (!is_teamplay() || jointeam == -1 || !config.sv_teambalance_time)
		return true;
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		PLAYER *p = game.players[i];
		if(p && p->team != -1)
			t[p->team]++;
	}
	
	// simulate what would happen if changed team
	t[jointeam]++;
	if (pplayer->team != -1)
		t[jointeam^1]--;
	
	// there is a player-difference of at least 2
	if(abs(t[0]-t[1]) >= 2)
	{
		// player wants to join team with less players
		if ((t[0] < t[1] && jointeam == 0) || (t[0] > t[1] && jointeam == 1))
			return true;
		else
			return false;
	}
	else
		return true;
}

void GAMECONTROLLER::do_player_score_wincheck()
{
	if(game_over_tick == -1  && !warmup)
	{
		// gather some stats
		int topscore = 0;
		int topscore_count = 0;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(game.players[i])
			{
				if(game.players[i]->score > topscore)
				{
					topscore = game.players[i]->score;
					topscore_count = 1;
				}
				else if(game.players[i]->score == topscore)
					topscore_count++;
			}
		}
		
		// check score win condition
		if((config.sv_scorelimit > 0 && topscore >= config.sv_scorelimit) ||
			(config.sv_timelimit > 0 && (server_tick()-round_start_tick) >= config.sv_timelimit*server_tickspeed()*60))
		{
			if(topscore_count == 1)
				endround();
			else
				sudden_death = 1;
		}
	}
}

void GAMECONTROLLER::do_team_score_wincheck()
{
	if(game_over_tick == -1 && !warmup)
	{
		// check score win condition
		if((config.sv_scorelimit > 0 && (teamscore[0] >= config.sv_scorelimit || teamscore[1] >= config.sv_scorelimit)) ||
			(config.sv_timelimit > 0 && (server_tick()-round_start_tick) >= config.sv_timelimit*server_tickspeed()*60))
		{
			if(teamscore[0] != teamscore[1])
				endround();
			else
				sudden_death = 1;
		}
	}
}

int GAMECONTROLLER::clampteam(int team)
{
	if(team < 0) // spectator
		return -1;
	if(is_teamplay())
		return team&1;
	return  0;
}

bool GAMECONTROLLER::check_spam(const char *message, PLAYER *p)
{
	if(!strcmp(p->old_message, message))
		p->spamcount++;
	else
	{
		p->spamcount = 0;
		str_copy(p->old_message, message, sizeof(message));
	}
	
	if(p->spamcount >= 5)
		return true;
	else
		return false;
}

void GAMECONTROLLER::slowmotion(int min)
{
	console_execute_line("tune ground_control_speed 5");
	console_execute_line("tune air_control_speed 2");
	console_execute_line("tune air_friction 0.55");
	console_execute_line("tune hook_fire_speed 40");
	console_execute_line("tune hook_drag_speed 7.5");
	console_execute_line("tune gravity 0.6");
	console_execute_line("tune gun_lifetime 6");
	console_execute_line("tune gun_speed 500");
	console_execute_line("tune shotgun_speed 500");
	console_execute_line("tune shotgun_lifetime 2");
	console_execute_line("tune grenade_speed 200");
	console_execute_line("tune grenade_lifetime 10");

	slow_tick = min*50*60;
	game.send_broadcast("EVENT: Slowmotion!", -1);
	game.create_sound_global(SOUND_CTF_CAPTURE);
}

void GAMECONTROLLER::speedmotion(int min)
{
	console_execute_line("tune ground_control_speed 35");
	console_execute_line("tune air_control_speed 30");
	console_execute_line("tune hook_fire_speed 160");
	console_execute_line("tune hook_drag_speed 40");
	console_execute_line("tune gun_speed 4000");
	console_execute_line("tune shotgun_speed 4000");
	console_execute_line("tune grenade_speed 2000");
	console_execute_line("tune ground_jump_impulse 22");
	console_execute_line("tune air_jump_impulse 20");


	speed_tick = min*50*60;
	game.send_broadcast("EVENT: Speedmotion!", -1);
	game.create_sound_global(SOUND_CTF_CAPTURE);
}

void GAMECONTROLLER::x2(int min)
{
	x2_tick = min*50*60;
	game.send_broadcast("EVENT: Money & Exp x2!", -1);
	game.create_sound_global(SOUND_CTF_CAPTURE);
}

void GAMECONTROLLER::one_hit(int min)
{
	one_hit_tick = min*50*60;
	game.send_broadcast("EVENT: One-Hit-Kill!", -1);
	game.create_sound_global(SOUND_CTF_CAPTURE);
}

void GAMECONTROLLER::spider_web(int min)
{
	web_tick = min*50*60;
	game.send_broadcast("EVENT: Spider Web!", -1);
	game.create_sound_global(SOUND_CTF_CAPTURE);
}

GAMECONTROLLER *gamecontroller = 0;
