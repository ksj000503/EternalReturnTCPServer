#pragma once

#include <mysql.h>

#include <string>

class DatabaseManager
{
public:

	DatabaseManager();

	~DatabaseManager();

	bool Connect(const std::string& Host, int Port, const std::string& User, const std::string& Password, const std::string& DatabaseName);

	void Disconnect();

	MYSQL* GetConnection();

private:

	MYSQL* Connection;

	bool bIsConnected;
};