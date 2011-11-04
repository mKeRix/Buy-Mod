#include <engine/e_server_interface.h>
#include <game/generated/g_protocol.hpp>
#include <game/server/gamecontext.hpp>
#include "projectile.hpp"


//////////////////////////////////////////////////
// projectile
//////////////////////////////////////////////////
PROJECTILE::PROJECTILE(int type, int owner, vec2 pos, vec2 dir, int span,
	int damage, int flags, float force, int sound_impact, int weapon)
: ENTITY(NETOBJTYPE_PROJECTILE)
{
	this->type = type;
	this->pos = pos;
	this->direction = dir;
	this->lifespan = span;
	this->owner = owner;
	this->flags = flags;
	this->force = force;
	this->damage = damage;
	this->sound_impact = sound_impact;
	this->weapon = weapon;
	this->bounce = 0;
	this->start_tick = server_tick();
	this->bounces = 3;
	game.world.insert_entity(this);
}

void PROJECTILE::reset()
{
	game.world.destroy_entity(this);
}

vec2 PROJECTILE::get_pos(float time)
{
	float curvature = 0;
	float speed = 0;
	if(type == WEAPON_GRENADE)
	{
		curvature = tuning.grenade_curvature;
		speed = tuning.grenade_speed;
	}
	else if(type == WEAPON_SHOTGUN)
	{
		curvature = tuning.shotgun_curvature;
		speed = tuning.shotgun_speed;
	}
	else if(type == WEAPON_GUN)
	{
		curvature = tuning.gun_curvature;
		speed = tuning.gun_speed;
	}
	
	return calc_pos(pos, direction, curvature, speed, time);
}

void move_point1(vec2 *inout_pos, vec2 *inout_vel)
{
	vec2 pos = *inout_pos;
	vec2 vel = *inout_vel;

	if(col_check_point(pos.x + vel.x, pos.y))
	{
		inout_vel->x *= -1;
	}

	if(col_check_point(pos.x, pos.y + vel.y))
	{
		inout_vel->y *= -1;
	}
}

void PROJECTILE::tick()
{
	
	float pt = (server_tick()-start_tick-1)/(float)server_tickspeed();
	float ct = (server_tick()-start_tick)/(float)server_tickspeed();
	float ft = (server_tick()-start_tick+1)/(float)server_tickspeed();
	vec2 prevpos = get_pos(pt);
	vec2 curpos = get_pos(ct);
	vec2 nextpos = get_pos(ft);

	lifespan--;

	PLAYER *p = game.players[owner];

	if(p)
	if(lifespan <= server_tickspeed()*tuning.grenade_lifetime-15 && weapon == WEAPON_GRENADE && p->acc_data.upgrade[10] == 1 && !game.controller->is_cup)
	{
		game.create_dragon_explosion(curpos, owner, weapon, false);
		game.create_sound(curpos, sound_impact);
	}
	
	int collide = col_intersect_line(prevpos, curpos, &curpos, 0);
	int futurecollide = col_intersect_line(curpos, nextpos, &curpos, 0);
	//int collide = col_check_point((int)curpos.x, (int)curpos.y);
	CHARACTER *ownerchar = game.get_player_char(owner);
	CHARACTER *targetchr = game.world.intersect_character(prevpos, curpos, 6.0f, curpos, ownerchar);
	if(targetchr || collide || lifespan < 0)
	{
		if(lifespan >= 0 || weapon == WEAPON_GRENADE)
			game.create_sound(curpos, sound_impact);

		if(flags & PROJECTILE_FLAGS_EXPLODE)
			game.create_explosion(curpos, owner, weapon, false);
		else if(targetchr)
			targetchr->take_damage(direction * max(0.001f, force), damage, owner, weapon);

		//destroy projectile
		if(game.controller->is_cup)
		{
			game.world.destroy_entity(this);
			return;
		}
		
		if(targetchr)
			game.world.destroy_entity(this);

		if(p)
		if((weapon == WEAPON_GRENADE && game.players[owner]->acc_data.upgrade[4] == 0) ||
			(weapon == WEAPON_SHOTGUN && game.players[owner]->acc_data.upgrade[5] == 0) || 
			(weapon == WEAPON_GUN && game.players[owner]->acc_data.upgrade[6] == 0)|| lifespan < 0)
			game.world.destroy_entity(this);

		//bouncing
		if(p)
		if((futurecollide || collide) && ((weapon == WEAPON_GRENADE && game.players[owner]->acc_data.upgrade[4] == 1) ||
			(weapon == WEAPON_SHOTGUN && game.players[owner]->acc_data.upgrade[5] == 1) || (weapon == WEAPON_GUN && game.players[owner]->acc_data.upgrade[6] == 1)))
		{

			vec2 temp_pos = prevpos;
			vec2 temp_dir = normalize(nextpos - curpos)*50.0f;
			
			move_point1(&temp_pos, &temp_dir);
			pos = prevpos;
			start_tick = server_tick();
			direction = normalize(temp_dir)*(distance(vec2(0,0), direction)*(75/100.0f));

			NETOBJ_PROJECTILE p;
			fill_info(&p);
				
			for(unsigned i = 0; i < sizeof(NETOBJ_PROJECTILE)/sizeof(int); i++)
				msg_pack_int(((int *)&p)[i]);

			bounces--;

			if(bounces <= 0)
				game.world.destroy_entity(this);

		}
	}
}

void PROJECTILE::fill_info(NETOBJ_PROJECTILE *proj)
{
	proj->x = (int)pos.x;
	proj->y = (int)pos.y;
	proj->vx = (int)(direction.x*100.0f);
	proj->vy = (int)(direction.y*100.0f);
	proj->start_tick = start_tick;
	proj->type = type;
}

void PROJECTILE::snap(int snapping_client)
{
	float ct = (server_tick()-start_tick)/(float)server_tickspeed();
	
	if(networkclipped(snapping_client, get_pos(ct)))
		return;

	NETOBJ_PROJECTILE *proj = (NETOBJ_PROJECTILE *)snap_new_item(NETOBJTYPE_PROJECTILE, id, sizeof(NETOBJ_PROJECTILE));
	fill_info(proj);
}
