#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
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


// 방 정보 (방장 IP / 포트)
struct RoomInfo
{
    string HostIP;

    int HostPort;
};

// ★ 접속한 클라이언트 1명의 상태 (소켓 / 닉네임 / 어느 방인지)
//   채팅할 때 "이 소켓이 어느 방 소속인지"를 여기서 바로 찾는다
struct ClientInfo
{
    SOCKET Socket;

    string Nickname;

    string RoomCode;
};


// 방 목록 : 방코드 -> 방정보
unordered_map<string, RoomInfo> GRoomMap;

// ★ 접속자 목록 : 소켓 -> 클라이언트 정보 (채팅 대상 찾을 때 이걸 순회)
unordered_map<SOCKET, ClientInfo> GClients;


// ★ 맵 데이터(GRoomMap, GClients) 보호용 락
//   스레드가 클라마다 따로 돌기 때문에, 맵을 건드릴 땐 반드시 잠가야 크래시가 안 난다
mutex GRoomMutex;

// ★ 소켓 전송 보호용 락
//   한 소켓에 두 스레드가 동시에 write 하면 메시지가 섞여 깨지므로, 전송을 한 번에 하나씩만
mutex GSendMutex;


// ★ 모든 전송은 이 함수를 통한다 (전송 락으로 감싼 SendMessage)
static bool SafeSend(SOCKET Target, const string& Data)
{
    lock_guard<mutex> slock(GSendMutex);

    return SendMessage(Target, Data);
}


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

        // 처리 중 발생하는 타입 예외를 여기서 전부 잡아, 요청 하나가 서버를 죽이지 않게 함
        try
        {
            // ── 로그인 ───────────────────────────────
            if (Type == "LOGIN_REQUEST")
            {
                string UserId = Request.value("id", "");

                string Password = Request.value("password", "");

                string OutNickname;

                string OutErrorMessage;

                bool bSuccess = GDatabaseManager.LoginUser(UserId, Password, OutNickname, OutErrorMessage);

                if (bSuccess)
                {
                    // ★ 로그인 성공 → 닉네임을 서버에 저장해 둔다
                    //   (채팅 보낼 때 이 값을 쓴다. 클라가 보낸 닉네임을 믿으면 위조 가능하므로)
                    {
                        lock_guard<mutex> lock(GRoomMutex);

                        GClients[ClientSocket].Nickname = OutNickname;
                    }

                    Response = { {"type","LOGIN_RESPONSE"}, {"success",true}, {"nickname",OutNickname} };
                }
                else
                {
                    Response = { {"type","LOGIN_RESPONSE"}, {"success",false}, {"errorMessage",OutErrorMessage} };
                }
            }
            // ── 회원가입 ─────────────────────────────
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
            // ── 방 생성 ──────────────────────────────
            else if (Type == "ROOM_CREATE_REQUEST")
            {
                int ListenPort = 0;

                if (Request.contains("listenPort"))
                {
                    const json& ListenPortField = Request["listenPort"];

                    if (ListenPortField.is_number())
                    {
                        ListenPort = ListenPortField.get<int>();
                    }
                    else if (ListenPortField.is_string())
                    {
                        try
                        {
                            ListenPort = stoi(ListenPortField.get<string>());
                        }
                        catch (const std::exception& e)
                        {
                            cerr << "[ListenPort Parse Error] " << e.what() << endl;

                            ListenPort = 0;
                        }
                    }
                }

                string RoomCode;

                // ★ 방코드 생성 + 등록 + 내 RoomCode 저장을 한 락 안에서 처리 (중복코드 방지)
                {
                    lock_guard<mutex> lock(GRoomMutex);

                    do
                    {
                        int RandomNumber = rand() % 1000000;

                        char Buffer[7];

                        snprintf(Buffer, sizeof(Buffer), "%06d", RandomNumber);

                        RoomCode = string(Buffer);

                    } while (GRoomMap.find(RoomCode) != GRoomMap.end());

                    GRoomMap[RoomCode] = { ClientIP, ListenPort };

                    // ★ 방장도 이 방 소속으로 기록 (채팅 대상에 포함되도록)
                    GClients[ClientSocket].RoomCode = RoomCode;
                }

                Response = { {"type","ROOM_CREATE_RESPONSE"}, {"success",true}, {"roomCode",RoomCode} };

                cout << "[Room Created] code=" << RoomCode << " ip=" << ClientIP << " port=" << ListenPort << endl;
            }
            // ── 방 참가 ──────────────────────────────
            else if (Type == "ROOM_JOIN_REQUEST")
            {
                string RoomCode = Request.value("roomCode", "");

                lock_guard<mutex> lock(GRoomMutex);

                auto Found = GRoomMap.find(RoomCode);

                if (Found != GRoomMap.end())
                {
                    RoomInfo& Info = Found->second;

                    // ★ 참가자를 이 방 소속으로 기록 (채팅 대상에 포함되도록)
                    GClients[ClientSocket].RoomCode = RoomCode;

                    Response = { {"type","ROOM_JOIN_RESPONSE"}, {"success",true}, {"ip",Info.HostIP}, {"port",Info.HostPort} };

                    cout << "[Room Joined] code= " << RoomCode << " ip =" << Info.HostIP << " port=" << Info.HostPort << endl;
                }
                else
                {
                    Response = { {"type","ROOM_JOIN_RESPONSE"}, {"success",false}, {"errorMessage","존재하지 않는 방입니다"} };
                }
            }
            // ── 채팅 전송 (핵심) ─────────────────────
            else if (Type == "CHAT_SEND_REQUEST")
            {
                string Message = Request.value("message", "");

                string RoomCode;

                string Nickname;

                // ★ 보낼 대상 소켓 목록
                vector<SOCKET> Targets;

                // ★ (1) 맵 락 : 내 방코드/닉네임 확인 + 같은 방 사람 소켓만 수집
                //     락을 잡은 채로 전송하면 다른 스레드가 막히므로, 대상만 복사하고 바로 락을 푼다
                {
                    lock_guard<mutex> lock(GRoomMutex);

                    auto MeIt = GClients.find(ClientSocket);

                    if (MeIt != GClients.end())
                    {
                        RoomCode = MeIt->second.RoomCode;

                        Nickname = MeIt->second.Nickname;

                        // 방에 들어가 있을 때만 대상 수집
                        if (!RoomCode.empty())
                        {
                            for (auto& Pair : GClients)
                            {
                                // 나 자신은 제외
                                if (Pair.first == ClientSocket) continue;

                                // 다른 방 사람은 제외
                                if (Pair.second.RoomCode != RoomCode) continue;

                                Targets.push_back(Pair.first);
                            }
                        }
                    }
                }

                // ★ 방에 없는 상태(RoomCode 빈 값)에서 채팅 시도 → 차단
                //   막지 않으면 방 없는 사람들끼리 서로 채팅이 새는 버그가 생긴다
                if (RoomCode.empty())
                {
                    Response = { {"type","CHAT_SEND_RESPONSE"}, {"success",false}, {"errorMessage","방에 참가하지 않았습니다"} };
                }
                else
                {
                    // ★ (2) 같은 방 사람들에게 CHAT_RECEIVE 를 직접 push
                    json Packet = { {"type","CHAT_RECEIVE"}, {"nickname",Nickname}, {"message",Message} };

                    string PacketStr = Packet.dump();

                    for (SOCKET Target : Targets)
                    {
                        SafeSend(Target, PacketStr);
                    }

                    // ★ (3) 보낸 본인에게는 "성공 확인"만 응답 (아래 공통 전송부에서 나감)
                    Response = { {"type","CHAT_SEND_RESPONSE"}, {"success",true} };
                }
            }
            // ── 방 종료 알림 (방장이 나감) ───────────
            else if (Type == "ROOM_CLOSE_NOTIFY")
            {
                // ★ 그 클라가 속한 방을 방 목록에서 제거
                {
                    lock_guard<mutex> lock(GRoomMutex);

                    auto MeIt = GClients.find(ClientSocket);

                    if (MeIt != GClients.end() && !MeIt->second.RoomCode.empty())
                    {
                        GRoomMap.erase(MeIt->second.RoomCode);
                    }
                }

                continue;
            }
            // ── 알 수 없는 타입 ──────────────────────
            else
            {
                Response = { {"type","ERROR_RESPONSE"}, {"errorMessage","알 수 없는 요청 타입"} };
            }
        }
        catch (const json::exception& e)
        {
            cerr << "[JSON Field Error] type=" << Type << " what=" << e.what() << endl;

            Response = { {"type","ERROR_RESPONSE"}, {"errorMessage","잘못된 요청 형식"} };
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

        // ★ 응답 전송도 SafeSend 로 (다른 스레드의 전송과 충돌 방지)
        if (!SafeSend(ClientSocket, ResponseJson))
        {
            std::cerr << "[Send Failed] 연결 종료" << std::endl;

            break;
        }

        cout << "[Sent] " << ResponseJson << endl;
    }

    cout << "[Disconnected] socket=" << ClientSocket << endl;

    // ★ 접속 종료 → 접속자 목록에서 제거 (죽은 소켓에 채팅 쏘는 것 방지)
    {
        lock_guard<mutex> lock(GRoomMutex);

        GClients.erase(ClientSocket);
    }

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

        // 클라이언트 IP 추출
        char ClientIP[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &ClientAddr.sin_addr, ClientIP, INET_ADDRSTRLEN);

        cout << "[Connected] " << ClientIP << endl;

        // ★ 접속자 목록에 등록 (워커 스레드들과 공유되는 맵이므로 락 필요)
        {
            lock_guard<mutex> lock(GRoomMutex);

            GClients[ClientSocket] = { ClientSocket, "", "" };
        }

        // 클라이언트마다 별도 스레드에서 HandleClient 실행
        thread(HandleClient, ClientSocket, string(ClientIP)).detach();
    }

    closesocket(ListenSocket);

    WSACleanup();

    return 0;
}