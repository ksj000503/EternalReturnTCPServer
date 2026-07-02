#include "DatabaseManager.h"

#include <iostream>

DatabaseManager::DatabaseManager()
{
	Connection = nullptr;

	bIsConnected = false;
}

DatabaseManager::~DatabaseManager()
{
	Disconnect();
}

bool DatabaseManager::Connect(const std::string& Host, int Port, const std::string& User, const std::string& Password, const std::string& DatabaseName)
{
	Connection = mysql_init(nullptr);

	if (Connection == nullptr)
	{
		std::cerr << "[DB] mysql_init 실패" << std::endl;

		return false;
	}

	MYSQL* Result = mysql_real_connect(Connection, Host.c_str(), User.c_str(), Password.c_str(), DatabaseName.c_str(), Port, nullptr, 0);

	if (Result == nullptr)
	{
		std::cerr << "[DB] 연결 실패: " << mysql_error(Connection) << std::endl;

		mysql_close(Connection);

		Connection = nullptr;

		return false;
	}

	bIsConnected = true;

	std::cout << "[DB] MySQL 연결 성공: " << Host << ":" << Port << "/" << DatabaseName << std::endl;

	return true;
}

void DatabaseManager::Disconnect()
{
	if (Connection != nullptr)
	{
		mysql_close(Connection);

		Connection = nullptr;
	}

	bIsConnected = false;
}

MYSQL* DatabaseManager::GetConnection()
{
	return Connection;
}