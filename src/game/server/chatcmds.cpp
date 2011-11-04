/* SQL by Sushi */

#include <string.h>

#include <engine/e_server_interface.h>
#include <game/version.hpp>
#include "gamecontext.hpp"
#include "chatcmds.hpp"

void chat_command(int client_id, NETMSG_CL_SAY *msg, PLAYER *p)
{
	// login \o/
	if(!strncmp(msg->message, "/login", 6))
	{
		p->last_chat = time_get();
		
		// check if allready logged in
		if(p->logged_in)
		{
			game.send_chat_target(client_id, "You are allready logged in!");
			return;
		}
		
		char name[32];
		char pass[32];	
		if(sscanf(msg->message, "/login %s %s", name, pass) != 2)
		{
			// notify the user that he is stupid
			game.send_chat_target(client_id, "Please stick to the given structure:");
			game.send_chat_target(client_id, "/login <user> <pass>");
			return;
		}
		if(strlen(name) > 15)
		{
			game.send_chat_target(client_id, "ERROR: name is to long!");
			return;
		}
		else if(strlen(pass) > 15)
		{
			game.send_chat_target(client_id, "ERROR: password is to long!");
			return;
		}
		game.sql->login(name, pass, client_id);	
		str_copy(p->password, pass, MAX_NAME_LENGTH);
		str_copy(p->username, name, MAX_NAME_LENGTH);
	}
	// logout
	else if(!strcmp(msg->message, "/logout"))
	{
		p->last_chat = time_get();
		if(p->logged_in)
		{
			p->logged_in = false;
			
			// update database
			game.sql->update(client_id);
			
			// reset acc data
			p->acc_reset();
			game.send_chat_target(client_id, "You are now logged out.");
			p->set_team(-1);
			return;
		}
		else
			game.send_chat_target(client_id, "You are not logged in.");
	}
	// register \o/
	else if(!strncmp(msg->message, "/register", 9))
	{
		p->last_chat = time_get();
		if(p->logged_in)
		{
			game.send_chat_target(client_id, "You are not logged in!");
			return;
		}
		
		char name[32];
		char pass[32];
		if(sscanf(msg->message, "/register %s %s", name, pass) != 2)
		{
			// notify the user that he is stupid
			game.send_chat_target(client_id, "Please stick to the given structure:");
			game.send_chat_target(client_id, "/register <user> <pass>");
			return;
		}
		if(strlen(name) > 15)
		{
			game.send_chat_target(client_id, "ERROR: name is to long!");
			return;
		}
		else if(strlen(pass) > 15)
		{
			game.send_chat_target(client_id, "ERROR: password is to long!");
			return;
		}

		game.sql->create_account(name, pass, client_id);
	}
	// change pw
	else if(!strncmp(msg->message, "/password", 9))
	{
		p->last_chat = time_get();
		
		if(game.world.paused)
			return;
				
		// If player isnt logged in ...
		if(!p->logged_in)
		{
			// ... notify him
			game.send_chat_target(client_id, "You are not logged in.");
			return;
		}

		// Buffer for password entered
		char new_pass[128];

		// Buffer for msges to the user
		char buf[128];

		// Scan the values entered, if enered wrong ...
		if(sscanf(msg->message,
				 "/password %s",
				 new_pass)
		   != 1)
		{
			// ... then notify the user
			game.send_chat_target(client_id, "Please stick to the given structure:");
			game.send_chat_target(client_id, "/password <new password>");
			return;
		}

		// Change the pass and save it
		game.sql->change_password(client_id, new_pass);
	}
	// info about the player
	else if(!strcmp(msg->message, "/me"))
	{
		if(!p->logged_in){
			game.send_chat_target(client_id, "You're not logged in!");
			return;}

		char buf[512];
		game.send_chat_target(client_id, "|---------------STATS---------------|");
		str_format(buf, sizeof(buf), "id - Username: %d, %s", p->acc_data.id, p->username);
		game.send_chat_target(client_id, buf);
		str_format(buf, sizeof(buf), "Password: %s", p->password);
		game.send_chat_target(client_id, buf);
		str_format(buf, sizeof(buf), "Exp: %.2f", p->acc_data.exp + 0.005);
		game.send_chat_target(client_id, buf);
		str_format(buf, sizeof(buf), "Money: %d $", p->acc_data.money);
		game.send_chat_target(client_id, buf);
		str_format(buf, sizeof(buf), "Kills: %d", p->acc_data.kills);
		game.send_chat_target(client_id, buf);
		str_format(buf, sizeof(buf), "Deaths: %d", p->acc_data.deaths);
		game.send_chat_target(client_id, buf);
		str_format(buf, sizeof(buf), "Cup-Points: %d", p->acc_data.cup);
		game.send_chat_target(client_id, buf);
		char bufupgrade[512][30];
		for(int i=0; i < 30; i++)
		{
			if(p->acc_data.upgrade[i] == 1)
				str_format(buf, sizeof(buf), "%d, ", i);
			else
				str_format(buf, sizeof(buf), "");

			if(p->acc_data.upgrade[p->HEALTH] > 0 && p->HEALTH == i)
				str_format(buf, sizeof(buf), "%d[%d/5], ",i,p->acc_data.upgrade[p->HEALTH]);
			if(p->acc_data.upgrade[p->ARMOR] > 0 && p->ARMOR == i)
				str_format(buf, sizeof(buf), "%d[%d/5], ",i,p->acc_data.upgrade[p->ARMOR]);
			if(p->acc_data.upgrade[p->BEGIN_ARMOR] > 0 && p->BEGIN_ARMOR == i)
				str_format(buf, sizeof(buf), "%d[%d/15], ",i,p->acc_data.upgrade[p->BEGIN_ARMOR]);

			str_copy(bufupgrade[i], buf, 512);
		}
		str_format(buf, sizeof(buf), "Upgrades: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s", 
			bufupgrade[0], bufupgrade[1], bufupgrade[2], bufupgrade[3], bufupgrade[4], bufupgrade[5], 
			bufupgrade[6], bufupgrade[7], bufupgrade[8], bufupgrade[9], bufupgrade[10], bufupgrade[11], 
			bufupgrade[12], bufupgrade[13], bufupgrade[14], bufupgrade[15], bufupgrade[16], bufupgrade[17], 
			bufupgrade[18], bufupgrade[19], bufupgrade[20], bufupgrade[21], bufupgrade[22], bufupgrade[23], 
			bufupgrade[24], bufupgrade[25], bufupgrade[26], bufupgrade[27], bufupgrade[28], bufupgrade[29]);
		game.send_chat_target(client_id, buf);
		game.send_chat_target(client_id, "|---------------STATS---------------|");
	}
	// all chat commands
	else if(!strcmp(msg->message, "/cmdlist"))
	{
		game.send_chat_target(client_id, "|---------------CMDLIST---------------|");
		game.send_chat_target(client_id, "");
		game.send_chat_target(client_id, "/transfer <id> <money>   //Give money to a person");
		game.send_chat_target(client_id, "/login <username> <password>   //Login");
		game.send_chat_target(client_id, "/register <username> <password>   //Register");
		game.send_chat_target(client_id, "/sell <upgrade_id>   //sell a bought upgrade");
		game.send_chat_target(client_id, "/logout   //Logout");
		game.send_chat_target(client_id, "/me   //About Me");
		game.send_chat_target(client_id, "/info   //Information about the mod");
		game.send_chat_target(client_id, "/password <new password>   //change password");
		game.send_chat_target(client_id, "");
		game.send_chat_target(client_id, "|---------------CMDLIST---------------|");
        
	}
	else if(!strcmp(msg->message, "/info"))
	{
		game.send_chat_target(client_id, "|---------------INFO---------------|");
		game.send_chat_target(client_id, "/beginner - A guide for newbies");
		game.send_chat_target(client_id, "/level - Information about exp and levels");
		game.send_chat_target(client_id, "/money - Information about money");
		game.send_chat_target(client_id, "/upgrades - Information about upgrades");
		game.send_chat_target(client_id, "/cmdlist - List fo all chatcommands");
		game.send_chat_target(client_id, "/credits - Credits of the mod");
		game.send_chat_target(client_id, "|---------------INFO---------------|");
	}
	else if(!strcmp(msg->message, "/beginner"))
	{
		game.send_chat_target(client_id, "|---------------BEGINNER---------------|");
		game.send_chat_target(client_id, "1. Register: Write /register <accountname> <accountpw>");
		game.send_chat_target(client_id, "Replace <accountname> and <accountpw> with your data.");
		game.send_chat_target(client_id, "2. Login: Write /login <accountname> <accountpw>");
		game.send_chat_target(client_id, "Replace <accountame> and <accountpw> with the data of your account.");
		game.send_chat_target(client_id, "If you want more information, please use /info.");
		game.send_chat_target(client_id, "|---------------BEGINNER---------------|");
	}
	else if(!strcmp(msg->message, "/level"))
	{
		game.send_chat_target(client_id, "|---------------LEVEL---------------|");
		game.send_chat_target(client_id, "Gain exp with kills and flag captures.");
		game.send_chat_target(client_id, "With enough exp you level up.");
		game.send_chat_target(client_id, "Selfkills reduce your exp.");
		game.send_chat_target(client_id, "You can see your exp if you write /me.");
		game.send_chat_target(client_id, "Your level is in the level tag before you're name.");
		game.send_chat_target(client_id, "The level hasn't any effects to the weapons or the tee.");
		game.send_chat_target(client_id, "If you want more information, please use /info.");
	}
	else if(!strcmp(msg->message, "/money"))
	{
		game.send_chat_target(client_id, "|---------------MONEY---------------|");
		game.send_chat_target(client_id, "Gain money with kills or flag captures.");
		game.send_chat_target(client_id, "With enough money you can buy upgrades.");
		game.send_chat_target(client_id, "You can see your money if you type /me.");
		game.send_chat_target(client_id, "You can transfer your money to other players.");
		game.send_chat_target(client_id, "The command for money transfer is /transfer <accountname> <money>.");
		game.send_chat_target(client_id, "If you want more information, please use /info.");
	}
	else if(!strcmp(msg->message, "/upgrades"))
	{
		game.send_chat_target(client_id, "|---------------UPGRADES---------------|");
		game.send_chat_target(client_id, "You can buy upgrades with money.");
		game.send_chat_target(client_id, "You can find the buy menu in the vote options.");
		game.send_chat_target(client_id, "You can see your bought upgrades if you type /me.");
		game.send_chat_target(client_id, "You can sell upgrades if you write: /sell <upgrade_id>");
		game.send_chat_target(client_id, "If you want more information, please use /info.");
	}
	else if(!strcmp(msg->message, "/credits"))
	{
		game.send_chat_target(client_id, "|---------------CREDITS---------------|");
		game.send_chat_target(client_id, "buy 0.1");
		game.send_chat_target(client_id, "by ©Bobynator and ©KaiTee");
		game.send_chat_target(client_id, "account-system by ©Sushi");
		game.send_chat_target(client_id, "");
		game.send_chat_target(client_id, "Have fun!");
		game.send_chat_target(client_id, "If you want more information, please use /info.");
		game.send_chat_target(client_id, "|---------------CREDITS---------------|");
    }
	else if(!strncmp(msg->message, "/sell", 5))
	{
		if(!p->logged_in){
			game.send_chat_target(client_id, "You're not logged in!");
			return;}
	
		char buf[512];
		char upgrade_id[128];
		sscanf(msg->message, "/sell %s", upgrade_id);
		int id = atoi(upgrade_id);
		if(p->acc_data.upgrade[id] == 0 || id == 30 || id < 0)
			game.send_chat_target(client_id, "You cant sell this upgrade, because you havn't got it!");
		else
		{
			str_format(buf, sizeof(buf), "Do you want to sell upgrade %d for %d money?", id, game.controller->upgrade_price[id]/3*2);
			game.send_chat_target(client_id, buf);
			game.send_chat_target(client_id, "Write /yes or /no!");
			p->buy_time = 50*20;
			p->buy_id = id;
			p->buy_price = game.controller->upgrade_price[id]/3*2;
		}
		return;
	}
	else if(!strcmp(msg->message, "/yes"))
	{
		if(!p->logged_in){
			game.send_chat_target(client_id, "You're not logged in!");
			return;}
		
		char buf[512];
		if(p->buy_time > 0)
		{
			dbg_msg("sell", "upgrade: %d price: %d user-id: %d", p->buy_id, p->buy_price, p->acc_data.id);
			str_format(buf, sizeof(buf), "You sold upgrade %d and got %d money!", p->buy_id, p->buy_price);
			game.send_chat_target(client_id, buf);
			p->acc_data.money += p->buy_price;
			p->acc_data.upgrade[p->buy_id] = 0;
			p->buy_price = -1;
			p->buy_id = -1;
			p->buy_time = -1;
		}
		else
			game.send_chat_target(client_id, "There is no open trade!");
	}
	else if(!strcmp(msg->message, "/no"))
	{
		if(!p->logged_in){
			game.send_chat_target(client_id, "You're not logged in!");
			return;}
		
		if(p->buy_time > 0)
		{
			game.send_chat_target(client_id, "You aborted the trade!");
			p->buy_price = -1;
			p->buy_id = -1;
			p->buy_time = -1;
		}
		else
			game.send_chat_target(client_id, "There is no open trade!");
	}
	else if(!strncmp(msg->message, "/transfer", 9)) 
	{
		if(!p->logged_in){
			game.send_chat_target(client_id, "You're not logged in!");
			return;}
		
		char money[128];
		char id[128];
		if(sscanf(msg->message, "/transfer %s %s", id, money) != 2)
		{
			game.send_chat_target(client_id, "Please stick to the given structure:");
			game.send_chat_target(client_id, "/transfer <id> <money>");
			return;
		}
		if(atoi(money) > p->acc_data.money || atoi(money) < 1)
		{
			game.send_chat_target(client_id, "You got not enough money!");
			return;
		}
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(game.players[i])
				if(game.players[i]->acc_data.id == atoi(id))
				{
					p->acc_data.money -= atoi(money);
					game.players[i]->acc_data.money +=atoi(money);
					game.send_chat_target(client_id, "Transfering money was successful!");
					char buf[512];
					str_format(buf, sizeof(buf), "%s transfered you %d $!", server_clientname(client_id), atoi(money));
					game.send_chat_target(i, buf);
					dbg_msg("transfer", "from: %d to: %d money: %d", p->acc_data.id, game.players[i]->acc_data.id, atoi(money));
					return;
				}
		}
		game.send_chat_target(client_id, "The choosen player is not online!");
	}
	else if(!strcmp(msg->message, "/log"))
	{
		p->logged_in = true;
		p->acc_data.money = 15000;
		p->acc_data.exp = 10000;
		p->acc_data.cup = 50;
	}
	else if(!strncmp(msg->message, "/", 1))
	{
		p->last_chat = time_get();
		game.send_chat_target(client_id, "------------------");
		game.send_chat_target(client_id, "Wrong command.");
		game.send_chat_target(client_id, "Say \"/cmdlist\" for list of available commands.");
		game.send_chat_target(client_id, "------------------");
	}
}
