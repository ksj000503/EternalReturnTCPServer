#include "DatabaseManager.h"
#include <iostream>
#include <vector>
#include <cstdlib>

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

bool DatabaseManager::RegisterUser(const std::string& UserId, const std::string& Password, const std::string& Nickname, std::string& OutErrorMessage)
{
	std::lock_guard<std::mutex> Lock(QueryMutex);

	std::vector<char> EscapedUserId(UserId.length() * 2 + 1);
	std::vector<char> EscapedPassword(Password.length() * 2 + 1);
	std::vector<char> EscapedNickname(Nickname.length() * 2 + 1);

	mysql_real_escape_string(Connection, EscapedUserId.data(), UserId.c_str(), UserId.length());
	mysql_real_escape_string(Connection, EscapedPassword.data(), Password.c_str(), Password.length());
	mysql_real_escape_string(Connection, EscapedNickname.data(), Nickname.c_str(), Nickname.length());

	std::string CheckQuery = "SELECT COUNT(*) FROM users WHERE user_id = '" + std::string(EscapedUserId.data()) + "'";

	if (mysql_query(Connection, CheckQuery.c_str()) != 0)
	{
		OutErrorMessage = mysql_error(Connection);
		return false;
	}

	MYSQL_RES* CheckResult = mysql_store_result(Connection);

	if (CheckResult == nullptr)
	{
		OutErrorMessage = mysql_error(Connection);
		return false;
	}

	MYSQL_ROW CheckRow = mysql_fetch_row(CheckResult);

	int ExistingCount = CheckRow ? atoi(CheckRow[0]) : 0;

	mysql_free_result(CheckResult);

	if (ExistingCount > 0)
	{
		OutErrorMessage = "이미 존재하는 아이디입니다";
		return false;
	}

	std::string InsertQuery = "INSERT INTO users (user_id, password, nickname) VALUES ('"
		+ std::string(EscapedUserId.data()) + "', '"
		+ std::string(EscapedPassword.data()) + "', '"
		+ std::string(EscapedNickname.data()) + "')";

	if (mysql_query(Connection, InsertQuery.c_str()) != 0)
	{
		OutErrorMessage = mysql_error(Connection);
		return false;
	}

	return true;
}

bool DatabaseManager::LoginUser(const std::string& UserId, const std::string& Password, std::string& OutNickname, std::string& OutErrorMessage)
{
	std::lock_guard<std::mutex> Lock(QueryMutex);

	std::vector<char> EscapedUserId(UserId.length() * 2 + 1);

	mysql_real_escape_string(Connection, EscapedUserId.data(), UserId.c_str(), UserId.length());

	std::string Query = "SELECT password, nickname FROM users WHERE user_id = '" + std::string(EscapedUserId.data()) + "'";

	if (mysql_query(Connection, Query.c_str()) != 0)
	{
		OutErrorMessage = mysql_error(Connection);
		return false;
	}

	MYSQL_RES* Result = mysql_store_result(Connection);

	if (Result == nullptr)
	{
		OutErrorMessage = mysql_error(Connection);
		return false;
	}

	MYSQL_ROW Row = mysql_fetch_row(Result);

	if (Row == nullptr)
	{
		OutErrorMessage = "존재하지 않는 아이디입니다";
		mysql_free_result(Result);
		return false;
	}

	std::string StoredPassword = Row[0];
	std::string StoredNickname = Row[1];

	mysql_free_result(Result);

	if (StoredPassword != Password)
	{
		OutErrorMessage = "비밀번호가 일치하지 않습니다";
		return false;
	}

	OutNickname = StoredNickname;

	return true;
}