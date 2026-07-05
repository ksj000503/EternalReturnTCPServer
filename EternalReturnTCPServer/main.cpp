#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "DatabaseManager.h"

#include "NetworkUtils.h"

#pragma comment(lib, "Ws2_32.lib")

using json = nlohmann::json;

// DB 매니저 전역 인스턴스 (모든 클라이언트 스레드가 공유)
DatabaseManager GDatabaseManager;

using namespace std;

// TCP 서버가 대기할 포트
constexpr int kListenPort = 9000;

// 맵 구조체
struct RoomInfo
{
    string HostIP;

    int HostPort;
};

// 방을 담을 map
unordered_map<string, RoomInfo> GRoomMap;

// 클라이언트 1명당 별도 스레드로 실행되는 메인 처리 루프
void HandleClient(SOCKET ClientSocket, string ClientIP)
{
    // 이 스레드에서 DB 커넥션 사용 준비 (필수)
    mysql_thread_init();

    string RawJson;

    // 클라이언트가 연결을 끊을 때까지 계속 메시지 수신
    while (RecvMessage(ClientSocket, RawJson))
    {
        json Request;

        // 수신한 문자열을 JSON으로 파싱
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

        // 로그인 처리 (완성됨)
        if (Type == "LOGIN_REQUEST")
        {
            string UserId = Request.value("id", "");
            string Password = Request.value("password", "");

            string OutNickname;
            string OutErrorMessage;

            bool bSuccess = GDatabaseManager.LoginUser(UserId, Password, OutNickname, OutErrorMessage);

            if (bSuccess)
            {
                Response = { {"type","LOGIN_RESPONSE"}, {"success",true}, {"nickname",OutNickname} };
            }
            else
            {
                Response = { {"type","LOGIN_RESPONSE"}, {"success",false}, {"errorMessage",OutErrorMessage} };
            }
        }
        // 회원가입 처리 (완성됨)
        else if (Type == "REGISTER_REQUEST")
        {
            string UserId = Request.value("id", "");
            string Password = Request.value("password", "");
            string Nickname = Request.value("nickname", "");

            string OutErrorMessage;

            bool bSuccess = GDatabaseManager.RegisterUser(UserId, Password, Nickname, OutErrorMessage);

            if (bSuccess)
            {
                Response = { {"type","REGISTER_RESPONSE"}, {"success",true} };
            }
            else
            {
                Response = { {"type","REGISTER_RESPONSE"}, {"success",false}, {"errorMessage",OutErrorMessage} };
            }
        }
        // [구현 대상] 방 생성 요청 처리
        else if (Type == "ROOM_CREATE_REQUEST")
        {
            int ListenPort = Request.value("listenPort", 0);

            string RoomCode;

            do
            {
                int RandomNumber = rand() % 1000000;

                char Buffer[7];

                snprintf(Buffer, sizeof(Buffer), "%06d", RandomNumber);

                RoomCode = string(Buffer);
            } while (GRoomMap.find(RoomCode) != GRoomMap.end());

            GRoomMap[RoomCode] = { ClientIP, ListenPort };

            Response = { {"type","ROOM_CREATE_RESPONSE"}, {"success",true}, {"roomCode",RoomCode} };

            cout << "[Room Created] code=" << RoomCode << " ip=" << ClientIP << " port=" << ListenPort << endl;
        }
        // [구현 대상] 방 참가 요청 처리
        else if (Type == "ROOM_JOIN_REQUEST")
        {
            string RoomCode = Request.value("roomCode", "");

            auto Found = GRoomMap.find(RoomCode);

            if (Found != GRoomMap.end())
            {
                RoomInfo& Info = Found->second;

                Response = { {"type","ROOM_JOIN_RESPONSE"}, {"success",true}, {"ip",Info.HostIP}, {"port",Info.HostPort} };

                cout << "[Room Joined] code= " << RoomCode << " ip =" << Info.HostIP << " port=" << Info.HostPort << endl;
            }

            else
            {
                Response = { {"type","ROOM_JOIN_RESPONSE"}, {"success",false}, {"errorMessage","존재하지 않는 방입니다"} };
            }
            
        }
        // 방 종료 알림 (현재는 아무 처리 없이 무시)
        else if (Type == "ROOM_CLOSE_NOTIFY")
        {
            continue;
        }
        // 정의되지 않은 요청 타입에 대한 에러 응답
        else
        {
            Response = { {"type","ERROR_RESPONSE"}, {"errorMessage","알 수 없는 요청 타입"} };
        }

        string ResponseJson;

        // 응답 JSON을 문자열로 직렬화
        try
        {
            ResponseJson = Response.dump();
        }
        catch (const std::exception& e)
        {
            cerr << "[JSON Dump Error] " << e.what() << endl;
            continue;
        }

        // 클라이언트에게 응답 전송
        if (!SendMessage(ClientSocket, ResponseJson))
        {
            std::cerr << "[Send Failed] 연결 종료" << std::endl;
            break;
        }

        cout << "[Sent] " << ResponseJson << endl;
    }
    cout << "[Disconnected] socket=" << ClientSocket << endl;

    // 스레드 종료 전 DB 커넥션 정리 (필수)
    mysql_thread_end();

    closesocket(ClientSocket);
}

int main()
{
    // 콘솔 출력 한글 깨짐 방지
    SetConsoleOutputCP(CP_UTF8);

    // Winsock 초기화
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup 실패" << endl;

        return 1;
    }

    srand(static_cast<unsigned int>(time(nullptr)));

    // MySQL DB 연결
    bool bDbConnected = GDatabaseManager.Connect("localhost", 3306, "root", "12345678", "eternalreturn_db");

    if (!bDbConnected)
    {
        std::cerr << "DB 연결 실패, 서버 종료" << std::endl;

        return 1;
    }

    // 리스닝 소켓 생성
    SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (ListenSocket == INVALID_SOCKET)
    {
        cerr << "소켓 생성 실패: " << WSAGetLastError() << endl;

        return 1;
    }

    // 포트 재사용 옵션 (서버 재시작 시 "주소 사용 중" 에러 방지)
    BOOL ReuseAddr = TRUE;

    setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&ReuseAddr), sizeof(ReuseAddr));

    sockaddr_in ServerAddr{};

    ServerAddr.sin_family = AF_INET;

    ServerAddr.sin_addr.s_addr = INADDR_ANY;

    ServerAddr.sin_port = htons(kListenPort);

    // 소켓을 포트에 바인딩
    if (::bind(ListenSocket, reinterpret_cast<sockaddr*>(&ServerAddr), sizeof(ServerAddr)) == SOCKET_ERROR)
    {
        cerr << "bind 실패:" << WSAGetLastError() << endl;

        closesocket(ListenSocket);

        WSACleanup();

        return 1;
    }

    // 연결 대기 시작
    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        cerr << "listen 실패: " << WSAGetLastError() << endl;

        closesocket(ListenSocket);

        WSACleanup();

        return 1;
    }

    cout << "[Backend Server] 포트" << kListenPort << " 에서 대기 중..." << endl;

    // 메인 루프: 클라이언트 접속을 계속 받아서 스레드로 분리
    while (true)
    {
        sockaddr_in ClientAddr{};

        int ClientAddrLen = sizeof(ClientAddr);

        // 클라이언트 접속 수락 (이 시점에 IP를 알 수 있음)
        SOCKET ClientSocket = accept(ListenSocket, reinterpret_cast<sockaddr*>(&ClientAddr), &ClientAddrLen);

        if (ClientSocket == INVALID_SOCKET)
        {
            cerr << "accept 실패: " << WSAGetLastError() << endl;

            continue;
        }

        // 클라이언트 IP 추출 (ROOM_CREATE_REQUEST 처리 시 이 값을 사용할 예정)
        char ClientIP[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &ClientAddr.sin_addr, ClientIP, INET_ADDRSTRLEN);

        cout << "[Connected] " << ClientIP << endl;

        // 클라이언트마다 별도 스레드에서 HandleClient 실행
        thread(HandleClient, ClientSocket, string(ClientIP)).detach();
    }

    closesocket(ListenSocket);

    WSACleanup();

    return 0;
}