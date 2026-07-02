#pragma once

#include <mysql.h>
#include <string>
#include <mutex>

class DatabaseManager
{
public:

	DatabaseManager();

	~DatabaseManager();

	bool Connect(const std::string& Host, int Port, const std::string& User, const std::string& Password, const std::string& DatabaseName);

	void Disconnect();

	MYSQL* GetConnection();

	bool RegisterUser(const std::string& UserId, const std::string& Password, const std::string& Nickname, std::string& OutErrorMessage);

	bool LoginUser(const std::string& UserId, const std::string& Password, std::string& OutNickname, std::string& OutErrorMessage);

private:

	MYSQL* Connection;

	bool bIsConnected;

	std::mutex QueryMutex;
};