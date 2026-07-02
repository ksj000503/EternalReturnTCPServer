#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>

#include "NetworkUtils.h"

#pragma comment(lib, "Ws2_32.lib")

using json = nlohmann::json;

using namespace std;

constexpr int kListenPort = 9000;

void HandleClient(SOCKET ClientSocket)
{
	string RawJson;

	while (RecvMessage(ClientSocket, RawJson))
	{
		json Request;

		try
		{
			Request = json::parse(RawJson);
		}

		catch (const json::parse_error& e)
		{
			cerr << "[JSON Parse Error]" << e.what() << endl;
			continue;
		}

		string Type = Request.value("type", "");

		cout << "[Recv] type=" << Type << endl;

		json Response;

        if (Type == "LOGIN_REQUEST")
        {
            Response = { {"type","LOGIN_RESPONSE"}, {"success",false}, {"errorCode",99}, {"errorMessage","DB 미연동"} };
        }
        else if (Type == "REGISTER_REQUEST")
        {
            Response = { {"type","REGISTER_RESPONSE"}, {"success",false}, {"errorCode",99}, {"errorMessage","DB 미연동"} };
        }
        else if (Type == "ROOM_LIST_REQUEST")
        {
            Response = { {"type","ROOM_LIST_RESPONSE"}, {"rooms", json::array()} };
        }
        else if (Type == "ROOM_CREATE_REQUEST")
        {
            Response = { {"type","ROOM_CREATE_RESPONSE"}, {"success",false}, {"errorMessage","미구현"} };
        }
        else if (Type == "ROOM_JOIN_REQUEST")
        {
            Response = { {"type","ROOM_JOIN_RESPONSE"}, {"success",false}, {"errorMessage","미구현"} };
        }
        else if (Type == "ROOM_CLOSE_NOTIFY")
        {
            // 응답 불필요한 단방향 알림 → 처리만 하고 continue
            continue;
        }
        else
        {
            Response = { {"type","ERROR_RESPONSE"}, {"errorMessage","알 수 없는 요청 타입"} };
        }

        if (!SendMessage(ClientSocket, Response.dump()))
        {
            std::cerr << "[Send Failed] 연결 종료" << std::endl;
            break;
        }
    }
    cout << "[Disconnected] socket=" << ClientSocket << endl;

    closesocket(ClientSocket);
}

int main()
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup 실패" << endl;

        return 1;
    }

    SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (ListenSocket == INVALID_SOCKET)
    {
        cerr << "소켓 생성 실패: " << WSAGetLastError() << endl;

        return 1;
    }

    BOOL ReuseAddr = TRUE;

    setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&ReuseAddr), sizeof(ReuseAddr));

    sockaddr_in ServerAddr{};

    ServerAddr.sin_family = AF_INET;

    ServerAddr.sin_addr.s_addr = INADDR_ANY;

    ServerAddr.sin_port = htons(kListenPort);

    if (::bind(ListenSocket, reinterpret_cast<sockaddr*>(&ServerAddr), sizeof(ServerAddr)) == SOCKET_ERROR)
    {
        cerr << "bind 실패:" << WSAGetLastError() << endl;

        closesocket(ListenSocket);

        WSACleanup();

        return 1;
    }

    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        cerr << "listen 실패: " << WSAGetLastError() << endl;

        closesocket(ListenSocket);

        WSACleanup();

        return 1;
    }

    cout << "[Backend Server] 포트" << kListenPort << " 에서 대기 중..." << endl;

    while (true)
    {
        sockaddr_in ClientAddr{};

        int ClientAddrLen = sizeof(ClientAddr);

        SOCKET ClientSocket = accept(ListenSocket, reinterpret_cast<sockaddr*>(&ClientAddr), &ClientAddrLen);

        if (ClientSocket == INVALID_SOCKET)
        {
            cerr << "accept 실패: " << WSAGetLastError() << endl;

            continue;
        }

        char ClientIP[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &ClientAddr.sin_addr, ClientIP, INET_ADDRSTRLEN);

        cout << "[Connected] " << ClientIP << endl;

        thread(HandleClient, ClientSocket).detach();
    }

    closesocket(ListenSocket);

    WSACleanup();

    return 0;
}