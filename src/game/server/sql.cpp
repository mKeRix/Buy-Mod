/* SQL class by Sushi */
#include "gamecontext.hpp"

#include <engine/e_config.h>
#include <engine/e_server_interface.h>

static LOCK sql_lock = 0;

SQL::SQL()
{
	if(sql_lock == 0)
		sql_lock = lock_create();
		
	// set database info
	database = config.sv_sql_database;
	prefix = config.sv_sql_prefix;
	user = config.sv_sql_user;
	pass = config.sv_sql_pw;
	ip = config.sv_sql_ip;
	port = config.sv_sql_port;
}

bool SQL::connect()
{
	try 
	{
		// Create connection
		driver = get_driver_instance();
		char buf[256];
		str_format(buf, sizeof(buf), "tcp://%s:%d", ip, port);
		connection = driver->connect(buf, user, pass);
		
		// Create Statement
		statement = connection->createStatement();
		
		// Create database if not exists
		str_format(buf, sizeof(buf), "CREATE DATABASE IF NOT EXISTS %s", database);
		statement->execute(buf);
		
		// Connect to specific database
		connection->setSchema(database);
		dbg_msg("SQL", "SQL connection established");
		return true;
	} 
	catch (sql::SQLException &e)
	{
		dbg_msg("SQL", "ERROR: SQL connection failed");
		return false;
	}
}

void SQL::disconnect()
{
	try
	{
		delete connection;
		dbg_msg("SQL", "SQL connection disconnected");
	}
	catch (sql::SQLException &e)
	{
		dbg_msg("SQL", "ERROR: No SQL connection");
	}
}

// create tables... should be done only once
void SQL::create_tables()
{
	// create connection
	if(connect())
	{
		try
		{
			// create tables
			char buf[2048];
			str_format(buf, sizeof(buf), "CREATE TABLE IF NOT EXISTS %s_account (ID INT AUTO_INCREMENT PRIMARY KEY, Name VARCHAR(31) NOT NULL, Pass VARCHAR(32) NOT NULL, Kills BIGINT DEFAULT 0, Deaths BIGINT DEFAULT 0, Betrayals BIGINT DEFAULT 0, last_logged_in DATETIME NOT NULL, register_date DATETIME NOT NULL, Exp DOUBLE DEFAULT 0, Money BIGINT DEFAULT 0, Upgrade_0 INT DEFAULT 0, Upgrade_1 INT DEFAULT 0, Upgrade_2 INT DEFAULT 0, Upgrade_3 INT DEFAULT 0, Upgrade_4 INT DEFAULT 0, Upgrade_5 INT DEFAULT 0, Upgrade_6 INT DEFAULT 0, Upgrade_7 INT DEFAULT 0, Upgrade_8 INT DEFAULT 0, Upgrade_9 INT DEFAULT 0, Upgrade_10 INT DEFAULT 0, Upgrade_11 INT DEFAULT 0, Upgrade_12 INT DEFAULT 0, Upgrade_13 INT DEFAULT 0, Upgrade_14 INT DEFAULT 0, Upgrade_15 INT DEFAULT 0, Upgrade_16 INT DEFAULT 0, Upgrade_17 INT DEFAULT 0, Upgrade_18 INT DEFAULT 0, Upgrade_19 INT DEFAULT 0, Upgrade_20 INT DEFAULT 0, Upgrade_21 INT DEFAULT 0, Upgrade_22 INT DEFAULT 0, Upgrade_23 INT DEFAULT 0, Upgrade_24 INT DEFAULT 0, Upgrade_25 INT DEFAULT 0, Upgrade_26 INT DEFAULT 0, Upgrade_27 INT DEFAULT 0, Upgrade_28 INT DEFAULT 0, Upgrade_29 INT DEFAULT 0, Cup BIGINT DEFAULT 0, Rang BIGINT DEFAULT 0);", prefix);
			statement->execute(buf);
			dbg_msg("SQL", "Tables were created successfully");

			// delete statement
			delete statement;
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("SQL", "ERROR: Tables were NOT created");
		}
		
		// disconnect from database
		disconnect();
	}	
}

// create account
void static create_account_thread(void *user)
{
	lock_wait(sql_lock);
	
	SQL_DATA *data = (SQL_DATA *)user;
	
	if(game.players[data->client_id] && !game.players[data->client_id]->logged_in)
	{
		// Connect to database
		if(data->sql_data->connect())
		{
			try
			{
				// check if allready exists
				char buf[512];
				str_format(buf, sizeof(buf), "SELECT * FROM %s_account WHERE NAME='%s';", data->sql_data->prefix, data->name);
				data->sql_data->results = data->sql_data->statement->executeQuery(buf);
				if(data->sql_data->results->next())
				{
					// account found
					dbg_msg("SQL", "Account '%s' allready exists", data->name);
					
					game.send_chat_target(data->client_id, "This acoount allready exists!");
				}
				else
				{
					// create account \o/
					str_format(buf, sizeof(buf), "INSERT IGNORE INTO %s_account(Name, Pass, register_date) VALUES ('%s', MD5('%s'), NOW());", data->sql_data->prefix, data->name, data->pass);
					data->sql_data->statement->execute(buf);
					dbg_msg("SQL", "Account '%s' was successfully created", data->name);
					
					game.send_chat_target(data->client_id, "Acoount was created successfully.");
					game.send_chat_target(data->client_id, "You may login now. (/login <user> <pass>)");
				}
				
				// delete statement
				delete data->sql_data->statement;
				delete data->sql_data->results;
			}
			catch (sql::SQLException &e)
			{
				dbg_msg("SQL", "ERROR: Could not create account");
			}
			
			// disconnect from database
			data->sql_data->disconnect();
		}
	}
	
	delete data;
	
	lock_release(sql_lock);
}

void SQL::create_account(const char* name, const char* pass, int client_id)
{
	SQL_DATA *tmp = new SQL_DATA();
	str_copy(tmp->name, name, sizeof(tmp->name));
	str_copy(tmp->pass, pass, sizeof(tmp->pass));
	tmp->client_id = client_id;
	tmp->sql_data = this;
	
	void *register_thread = thread_create(create_account_thread, tmp);
#if defined(CONF_FAMILY_UNIX)
	pthread_detach((pthread_t)register_thread);
#endif
}

// delete account
void static delete_account_name_thread(void *user)
{
	lock_wait(sql_lock);
	
	SQL_DATA *data = (SQL_DATA *)user;
	
	// Connect to database
	if(data->sql_data->connect())
	{
		try
		{	
			// check if exists
			char buf[512];
			str_format(buf, sizeof(buf), "SELECT * FROM %s_account WHERE NAME='%s';", data->sql_data->prefix, data->name);
			data->sql_data->results = data->sql_data->statement->executeQuery(buf);
			if(data->sql_data->results->next())
			{
				// delete the account
				str_format(buf, sizeof(buf), "DELETE FROM %s_account WHERE NAME='%s';", data->sql_data->prefix, data->name);
				data->sql_data->statement->execute(buf);
				dbg_msg("SQL", "Account '%s' was successfully deleted", data->name);
				
				// tell it all
				str_format(buf, sizeof(buf), "The account '%s' was deleted by admin", data->name);
				game.send_chat(-1, GAMECONTEXT::CHAT_ALL, buf);
			}
			else
			{
				// account doesnt exist
				dbg_msg("SQL", "Account '%s' does not exist", data->name);
			}
			
			// delete statement
			delete data->sql_data->statement;
			delete data->sql_data->results;
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("SQL", "ERROR: Could not delete account");
		}
		
		// disconnect from database
		data->sql_data->disconnect();
	}
	
	delete data;
	
	lock_release(sql_lock);
}

void SQL::delete_account(const char* name)
{
	SQL_DATA *tmp = new SQL_DATA();
	str_copy(tmp->name, name, sizeof(tmp->name));
	tmp->sql_data = this;
	
	void *delete_name_thread = thread_create(delete_account_name_thread, tmp);
#if defined(CONF_FAMILY_UNIX)
	pthread_detach((pthread_t)delete_name_thread);
#endif
}

void static delete_account_id_thread(void *user)
{
	lock_wait(sql_lock);
	
	SQL_DATA *data = (SQL_DATA *)user;
	
	// Connect to database
	if(data->sql_data->connect())
	{
		try
		{
			// check if exists
			char buf[512];
			str_format(buf, sizeof(buf), "SELECT * FROM %s_account WHERE ID='%d';", data->sql_data->prefix, data->account_id[data->client_id]);
			data->sql_data->results = data->sql_data->statement->executeQuery(buf);
			if(data->sql_data->results->next())
			{
				// get account name from database
				str_format(buf, sizeof(buf), "SELECT name FROM %s_account WHERE ID='%d';", data->sql_data->prefix, data->account_id[data->client_id]);

				// create results
				data->sql_data->results = data->sql_data->statement->executeQuery(buf);

				// jump to result
				data->sql_data->results->next();

				// finally the name is there \o/
				char acc_name[32];			
				str_copy(acc_name, data->sql_data->results->getString("name").c_str(), sizeof(acc_name));

				// delete the account
				str_format(buf, sizeof(buf), "DELETE FROM %s_account WHERE ID='%d';", data->sql_data->prefix, data->account_id[data->client_id]);
				data->sql_data->statement->execute(buf);
				dbg_msg("SQL", "Account '%s' was successfully deleted", acc_name);

				// tell it all
				str_format(buf, sizeof(buf), "The account '%s' was deleted by admin", acc_name);
				game.send_chat(-1, GAMECONTEXT::CHAT_ALL, buf);
			}
			else
			{
				// account doesnt exist
				dbg_msg("SQL", "Account '%d' does not exist", data->account_id[data->client_id]);
			}
			
			// delete statement
			delete data->sql_data->statement;
			delete data->sql_data->results;
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("SQL", "ERROR: Could not delete account");
		}
		
		// disconnect from database
		data->sql_data->disconnect();
	}
	
	delete data;
	
	lock_release(sql_lock);
}

void SQL::delete_account(int client_id)
{
	if(game.players[client_id] && game.players[client_id]->logged_in)
	{
		SQL_DATA *tmp = new SQL_DATA();
		tmp->client_id = client_id;
		tmp->account_id[client_id] = game.players[client_id]->acc_data.id;
		tmp->sql_data = this;
	
		void *delete_id_thread = thread_create(delete_account_id_thread, tmp);
	#if defined(CONF_FAMILY_UNIX)
		pthread_detach((pthread_t)delete_id_thread);
	#endif
	}
	else
		dbg_msg("SQL", "Player '%d' is not logged in", client_id);
}

// change password
void static change_password_thread(void *user)
{
	lock_wait(sql_lock);
	
	SQL_DATA *data = (SQL_DATA *)user;
	
	// Connect to database
	if(data->sql_data->connect())
	{
		try
		{
			// Connect to database
			data->sql_data->connect();
			
			// check if account exists
			char buf[512];
			str_format(buf, sizeof(buf), "SELECT * FROM %s_account WHERE ID='%d';", data->sql_data->prefix, data->account_id[data->client_id]);
			data->sql_data->results = data->sql_data->statement->executeQuery(buf);
			if(data->sql_data->results->next())
			{
				// update account data
				str_format(buf, sizeof(buf), "UPDATE %s_account SET Pass=MD5('%s') WHERE ID='%d'", data->sql_data->prefix, data->pass, data->account_id[data->client_id]);
				data->sql_data->statement->execute(buf);
				
				// get account name from database
				str_format(buf, sizeof(buf), "SELECT name FROM %s_account WHERE ID='%d';", data->sql_data->prefix, data->account_id[data->client_id]);
				
				// create results
				data->sql_data->results = data->sql_data->statement->executeQuery(buf);

				// jump to result
				data->sql_data->results->next();
				
				// finally the name is there \o/
				char acc_name[32];
				str_copy(acc_name, data->sql_data->results->getString("name").c_str(), sizeof(acc_name));	
				dbg_msg("SQL", "Account '%s' changed password.", acc_name);
				
				// Success
				str_format(buf, sizeof(buf), "Successfully changed your password to '%s'.", data->pass);
				game.send_broadcast(buf, data->client_id);
				game.send_chat_target(data->client_id, buf);
			}
			else
				dbg_msg("SQL", "Account seems to be deleted");
			
			// delete statement and results
			delete data->sql_data->statement;
			delete data->sql_data->results;
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("SQL", "ERROR: Could not update account");
		}
		
		// disconnect from database
		data->sql_data->disconnect();
	}
	
	delete data;
	
	lock_release(sql_lock);
}

void SQL::change_password(int client_id, const char* new_pass)
{
	SQL_DATA *tmp = new SQL_DATA();
	tmp->client_id = client_id;
	tmp->account_id[client_id] = game.players[client_id]->acc_data.id;
	str_copy(tmp->pass, new_pass, sizeof(tmp->pass));
	tmp->sql_data = this;
	
	void *change_pw_thread = thread_create(change_password_thread, tmp);
#if defined(CONF_FAMILY_UNIX)
	pthread_detach((pthread_t)change_pw_thread);
#endif
}

// login stuff
void static login_thread(void *user)
{
	lock_wait(sql_lock);
	
	SQL_DATA *data = (SQL_DATA *)user;
	
	if(game.players[data->client_id] && !game.account_data->logged_in[data->client_id])
	{
		// Connect to database
		if(data->sql_data->connect())
		{
			try
			{		
				// check if account exists
				char buf[1024];
				str_format(buf, sizeof(buf), "SELECT * FROM %s_account WHERE Name='%s';", data->sql_data->prefix, data->name);
				data->sql_data->results = data->sql_data->statement->executeQuery(buf);
				if(data->sql_data->results->next())
				{
					// check for right pw and get data
					str_format(buf, sizeof(buf), "SELECT ID, Kills, Deaths, Betrayals, Exp, Money, Upgrade_0, Upgrade_1, Upgrade_2, Upgrade_3, Upgrade_4, Upgrade_5, Upgrade_6, Upgrade_7, Upgrade_8, Upgrade_9, Upgrade_10, Upgrade_11, Upgrade_12, Upgrade_13, Upgrade_14, Upgrade_15, Upgrade_16, Upgrade_17, Upgrade_18, Upgrade_19, Upgrade_20, Upgrade_21, Upgrade_22, Upgrade_23, Upgrade_24, Upgrade_25, Upgrade_26, Upgrade_27, Upgrade_28, Upgrade_29, Cup, Rang FROM %s_account WHERE Name='%s' AND Pass=MD5('%s');", data->sql_data->prefix, data->name, data->pass);
					
					// create results
					data->sql_data->results = data->sql_data->statement->executeQuery(buf);
					
					// if match jump to it
					if(data->sql_data->results->next())
					{
						// never use player directly!
						// finally save the result to account_data \o/

						// check if account allready is logged in
						for(int i = 0; i < MAX_CLIENTS; i++)
						{
							if(game.account_data->id[i] == data->sql_data->results->getInt("ID"))
							{
								dbg_msg("SQL", "Account '%s' allready is logged in", data->name);
								
								game.send_chat_target(data->client_id, "This account is already logged in.");
								
								// delete statement and results
								delete data->sql_data->statement;
								delete data->sql_data->results;
								
								// disconnect from database
								data->sql_data->disconnect();
								
								// delete data
								delete data;
	
								// release lock
								lock_release(sql_lock);
								
								return;
							}
						}

						game.account_data->id[data->client_id] = data->sql_data->results->getInt("ID");
						game.account_data->kills[data->client_id] = data->sql_data->results->getInt("Kills");
						game.account_data->deaths[data->client_id] = data->sql_data->results->getInt("Deaths");
						game.account_data->betrayals[data->client_id] = data->sql_data->results->getInt("Betrayals");
						game.account_data->exp[data->client_id] = (float)data->sql_data->results->getDouble("Exp");
						game.account_data->money[data->client_id] = data->sql_data->results->getInt("Money");
						game.account_data->cup[data->client_id] = data->sql_data->results->getInt("Cup");
						for(int i = 0; i < 30; i++)
						{
							char buf[512];
							str_format(buf, sizeof(buf), "Upgrade_%d", i);
							game.account_data->upgrade[i][data->client_id] = data->sql_data->results->getInt(buf);
						}
						game.account_data->rang[data->client_id] = data->sql_data->results->getInt("Rang");

						// login should be the last thing
						game.account_data->logged_in[data->client_id] = true;
						dbg_msg("SQL", "Account '%s' logged in sucessfully", data->name);
						
						game.send_chat_target(data->client_id, "You are now logged in.");
						char buf[512];
						str_format(buf, sizeof(buf), "Welcome %s!", data->name);
						game.send_broadcast(buf, data->client_id);
					}
					else
					{
						// wrong password
						dbg_msg("SQL", "Account '%s' is not logged in due to wrong password", data->name);
						
						game.send_chat_target(data->client_id, "The password you entered is wrong.");
					}
				}
				else
				{
					// no account
					dbg_msg("SQL", "Account '%s' does not exists", data->name);
					
					game.send_chat_target(data->client_id, "This account does not exists.");
					game.send_chat_target(data->client_id, "Please register first. (/register <user> <pass>)");
				}
				
				// delete statement and results
				delete data->sql_data->statement;
				delete data->sql_data->results;
			}
			catch (sql::SQLException &e)
			{
				dbg_msg("SQL", "ERROR: Could not login account");
			}
			
			// disconnect from database
			data->sql_data->disconnect();
		}
	}
	
	delete data;
	
	lock_release(sql_lock);
}

void SQL::login(const char* name, const char* pass, int client_id)
{
	SQL_DATA *tmp = new SQL_DATA();
	str_copy(tmp->name, name, sizeof(tmp->name));
	str_copy(tmp->pass, pass, sizeof(tmp->pass));
	tmp->client_id = client_id;
	tmp->sql_data = this;
	
	void *login_account_thread = thread_create(login_thread, tmp);
#if defined(CONF_FAMILY_UNIX)
	pthread_detach((pthread_t)login_account_thread);
#endif
}

// update stuff
void static update_thread(void *user)
{
	lock_wait(sql_lock);
	
	SQL_DATA *data = (SQL_DATA *)user;
	
	// Connect to database
	if(data->sql_data->connect())
	{
		try
		{
			// check if account exists
			char buf[1024];
			str_format(buf, sizeof(buf), "SELECT * FROM %s_account WHERE ID='%d';", data->sql_data->prefix, data->account_id[data->client_id]);
			data->sql_data->results = data->sql_data->statement->executeQuery(buf);
			if(data->sql_data->results->next())
			{
				// update account data
				str_format(buf, sizeof(buf), "UPDATE %s_account SET Kills='%d', Deaths='%d', Betrayals='%d', last_logged_in=NOW(), Exp='%f', Money='%d', Upgrade_0='%d', Upgrade_1='%d', Upgrade_2='%d', Upgrade_3='%d', Upgrade_4='%d', Upgrade_5='%d', Upgrade_6='%d', Upgrade_7='%d', Upgrade_8='%d', Upgrade_9='%d', Upgrade_10='%d', Upgrade_11='%d', Upgrade_12='%d', Upgrade_13='%d', Upgrade_14='%d', Upgrade_15='%d', Upgrade_16='%d', Upgrade_17='%d', Upgrade_18='%d', Upgrade_19='%d', Upgrade_20='%d', Upgrade_21='%d', Upgrade_22='%d', Upgrade_23='%d', Upgrade_24='%d', Upgrade_25='%d', Upgrade_26='%d', Upgrade_27='%d', Upgrade_28='%d', Upgrade_29='%d', Cup='%d', Rang='%d' WHERE ID='%d';", 
					data->sql_data->prefix, data->account_kills[data->client_id], data->account_deaths[data->client_id], 
					data->account_betrayals[data->client_id], data->account_exp[data->client_id], data->account_money[data->client_id], 
					data->account_upgrade[0][data->client_id], data->account_upgrade[1][data->client_id],
					data->account_upgrade[2][data->client_id], data->account_upgrade[3][data->client_id], data->account_upgrade[4][data->client_id],
					data->account_upgrade[5][data->client_id], data->account_upgrade[6][data->client_id], data->account_upgrade[7][data->client_id],
					data->account_upgrade[8][data->client_id], data->account_upgrade[9][data->client_id], data->account_upgrade[10][data->client_id],
					data->account_upgrade[11][data->client_id], data->account_upgrade[12][data->client_id], data->account_upgrade[13][data->client_id],
					data->account_upgrade[14][data->client_id], data->account_upgrade[15][data->client_id], data->account_upgrade[16][data->client_id],
					data->account_upgrade[17][data->client_id], data->account_upgrade[18][data->client_id], data->account_upgrade[19][data->client_id],
					data->account_upgrade[20][data->client_id], data->account_upgrade[21][data->client_id], data->account_upgrade[22][data->client_id], 
					data->account_upgrade[23][data->client_id], data->account_upgrade[24][data->client_id], data->account_upgrade[25][data->client_id], 
					data->account_upgrade[26][data->client_id], data->account_upgrade[27][data->client_id], data->account_upgrade[28][data->client_id], 
					data->account_upgrade[29][data->client_id], data->account_cup[data->client_id], data->account_rang[data->client_id], data->account_id[data->client_id]);
				data->sql_data->statement->execute(buf);
				
				// get account name from database
				str_format(buf, sizeof(buf), "SELECT name FROM %s_account WHERE ID='%d';", data->sql_data->prefix, data->account_id[data->client_id]);
				
				// create results
				data->sql_data->results = data->sql_data->statement->executeQuery(buf);

				// jump to result
				data->sql_data->results->next();
				
				// finally the nae is there \o/
				char acc_name[32];
				str_copy(acc_name, data->sql_data->results->getString("name").c_str(), sizeof(acc_name));	
				dbg_msg("SQL", "Account '%s' was saved successfully", acc_name);
			}
			else
				dbg_msg("SQL", "Account seems to be deleted");
			
			// delete statement and results
			delete data->sql_data->statement;
			delete data->sql_data->results;
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("SQL", "ERROR: Could not update account");
		}
		
		// disconnect from database
		data->sql_data->disconnect();
	}
	
	delete data;
	
	lock_release(sql_lock);
}

void SQL::update(int client_id)
{
	SQL_DATA *tmp = new SQL_DATA();
	tmp->client_id = client_id;
	tmp->account_id[client_id] = game.players[client_id]->acc_data.id;
	tmp->account_kills[client_id] = game.players[client_id]->acc_data.kills;
	tmp->account_deaths[client_id] = game.players[client_id]->acc_data.deaths;
	tmp->account_betrayals[client_id] = game.players[client_id]->acc_data.betrayals;
	tmp->account_exp[client_id] = game.players[client_id]->acc_data.exp;
	tmp->account_money[client_id] = game.players[client_id]->acc_data.money;
	tmp->account_cup[client_id] = game.players[client_id]->acc_data.cup;
	for(int i = 0; i < 30; i++)
		tmp->account_upgrade[i][client_id] = game.players[client_id]->acc_data.upgrade[i];
	tmp->account_rang[client_id] = game.players[client_id]->acc_data.rang;
	tmp->sql_data = this;
	
	void *update_account_thread = thread_create(update_thread, tmp);
#if defined(CONF_FAMILY_UNIX)
	pthread_detach((pthread_t)update_account_thread);
#endif
}

// update all
void SQL::update_all()
{
	lock_wait(sql_lock);
	
	// Connect to database
	if(connect())
	{
		try
		{
			char buf[512];
			char acc_name[32];
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!game.players[i])
					continue;
				
				if(!game.players[i]->logged_in)
					continue;
				
				// check if account exists
				str_format(buf, sizeof(buf), "SELECT * FROM %s_account WHERE ID='%d';", prefix, game.players[i]->acc_data.id);
				results = statement->executeQuery(buf);
				if(results->next())
				{
					// update account data	
					str_format(buf, sizeof(buf), "UPDATE %s_account SET Kills='%d', Deaths='%d', Betrayals='%d', last_logged_in=NOW() WHERE ID='%d'", prefix, game.players[i]->acc_data.kills, game.players[i]->acc_data.deaths, game.players[i]->acc_data.betrayals, game.players[i]->acc_data.id);
					statement->execute(buf);
					
					// get account name from database
					str_format(buf, sizeof(buf), "SELECT name FROM %s_account WHERE ID='%d';", prefix, game.players[i]->acc_data.id);
					
					// create results
					results = statement->executeQuery(buf);

					// jump to result
					results->next();
					
					// finally the name is there \o/	
					str_copy(acc_name, results->getString("name").c_str(), sizeof(acc_name));	
					dbg_msg("SQL", "Account '%s' was saved successfully", acc_name);
				}
				else
					dbg_msg("SQL", "Account seems to be deleted");
				
				// delete results
				delete results;
			}
			
			// delete statement
			delete statement;
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("SQL", "ERROR: Could not update account");
		}
		
		// disconnect from database
		disconnect();
	}

	lock_release(sql_lock);
}
	
	
