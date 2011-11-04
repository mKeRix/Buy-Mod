/* SQL Class by Sushi */

#include <mysql_connection.h>
	
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

class SQL
{
public:
	SQL();
	
	sql::Driver *driver;
	sql::Connection *connection;
	sql::Statement *statement;
	sql::ResultSet *results;
	
	// copy of config vars
	const char* database;
	const char* prefix;
	const char* user;
	const char* pass;
	const char* ip;
	int port;
	
	bool connect();
	void disconnect();
	
	void create_tables();
	void create_account(const char* name, const char* pass, int client_id);
	void delete_account(const char* name);
	void delete_account(int client_id);
	void change_password(int client_id, const char* new_pass);
	void login(const char* name, const char* pass, int client_id);
	void update(int client_id);
	void update_all();
};

struct SQL_DATA
{
	SQL *sql_data;
	char name[32];
	char pass[32];
	int client_id;
	int account_kills[16];
	int account_deaths[16];
	int account_betrayals[16];
	int account_id[16];
	float account_exp[16];
	int account_money[16];
	int account_upgrade[30][16];
	int account_cup[16];
	int account_rang[16];
};

struct ACCOUNT_DATA
{
	int kills[16];
	int deaths[16];
	int betrayals[16];
	int id[16];
	
	bool logged_in[16];

	float exp[16];
	int money[16];
	int upgrade[30][16];
	int cup[16];
	int rang[16];
};
