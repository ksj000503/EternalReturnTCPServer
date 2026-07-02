#include "NetworkUtils.h"

using namespace std;

bool RecvExact(SOCKET Socket, char* Buffer, int Length)
{
	int TotalReceived = 0;

	while (TotalReceived < Length)
	{
		int Received = recv(Socket, Buffer + TotalReceived, Length - TotalReceived, 0);
		
		if (Received <= 0)
		{
			return false;
		}

		TotalReceived += Received;
	}

	return true;
}

bool SendExact(SOCKET Socket, const char* Buffer, int Length)
{
	int TotalSend = 0;

	while (TotalSend < Length)
	{
		int Sent = send(Socket, Buffer + TotalSend, Length - TotalSend, 0);

		if (Sent == SOCKET_ERROR)
		{
			return false;
		}

		TotalSend += Sent;
	}

	return true;
}

bool RecvMessage(SOCKET Socket, string& OutJson)
{
	// 1. 4바이트 길이 수신
	uint32_t NetworkLength = 0;

	if (!RecvExact(Socket, reinterpret_cast<char*>(&NetworkLength), sizeof(NetworkLength)))
	{
		return false;
	}

	uint32_t BodyLength = ntohl(NetworkLength);

	constexpr uint32_t kMaxMessageSize = 1024 * 1024;

	if (BodyLength == 0 || BodyLength > kMaxMessageSize)
	{
		return false;
	}

	// 2. JSON 본문 수신
	string Buffer(BodyLength, '\0');

	if (!RecvExact(Socket, &Buffer[0], static_cast<int>(BodyLength)))
	{
		return false;
	}

	OutJson = move(Buffer);

	return true;
}

bool SendMessage(SOCKET Socket, const string& Json)
{
	uint32_t BodyLength = static_cast<uint32_t>(Json.size());

	uint32_t NetworkLength = htonl(BodyLength);

	if (!SendExact(Socket, reinterpret_cast<const char*>(&NetworkLength), sizeof(NetworkLength)))
	{
		return false;
	}

	return SendExact(Socket, Json.data(), static_cast<int>(Json.size()));
}