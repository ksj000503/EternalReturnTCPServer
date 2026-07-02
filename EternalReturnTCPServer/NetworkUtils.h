#pragma once

#include <winsock2.h>
#include <string>

using namespace std;

// 소켓에서 Length 바이트 만큼 읽음
bool RecvExact(SOCKET Socket, char* Buffer, int Length);

// 소켓에 Length 바이트만큼 씀
bool SendExact(SOCKET Socket, const char* Buffer, int Length);

// 길이 온전히 수신
bool RecvMessage(SOCKET Socket, string& OutJson);

// JSON 문자열 붙여서 전송
bool SendMessage(SOCKET Socket, const string& Json);