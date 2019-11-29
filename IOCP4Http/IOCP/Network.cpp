#include "Network.h"
#include <MswSock.h>
#include <cstdio>

bool Network::init()
{
	WSADATA wsaData = { 0 };
	return ::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

bool Network::deinit()
{
	::WSACleanup();
	return true;
}

SOCKET Network::socket()
{
	return ::WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
}

int Network::bind(SOCKET s, const LPSOCKADDR_IN pAddr)
{
	return ::bind(s, (LPSOCKADDR)pAddr, sizeof(SOCKADDR_IN));
}

int Network::listen(SOCKET s, int backlog)
{
	return ::listen(s, backlog);
}

SOCKADDR_IN Network::getsockname(SOCKET s)
{
	SOCKADDR_IN addr[2];
	int addrLen = sizeof(addr);
	SecureZeroMemory(&addr, addrLen);
	int ret = ::getsockname(s, (PSOCKADDR)&addr, &addrLen);
	if (SOCKET_ERROR == ret)
	{
		printf("getsockname failed! err=%d\n", WSAGetLastError());
	}
	return addr[0];
}

SOCKADDR_IN Network::getpeername(SOCKET s)
{
	/*
	 * ������32���ֽڽ��գ�sizeof(SOCKADDR_IN) + 16��,
	 *	������ܻ���ִ���WSAEFAULT��code 10014��
	 * WSAEFAULT: The name or the namelen parameter is not in a valid part of
		the user address space, or the namelen parameter is too small.
	 * ʵ�����������16�ֽڣ��ҵ�������Բ������̨ʽ���Գ�����
	 * ����Ӧ���Ǹ�acceptEx���յ�ַ�Ļ��������ȱ���һ��
	*/
	SOCKADDR_IN addr[2];
	int addrLen = sizeof(addr);
	SecureZeroMemory(&addr, addrLen);
	int ret = ::getpeername(s, (PSOCKADDR)&addr, &addrLen);
	if (SOCKET_ERROR == ret)
	{
		printf("getpeername failed! err=%d\n", WSAGetLastError());
	}
	return addr[0];
}

bool Network::setKeepAlive(SOCKET s, bool on)
{
	DWORD opt = on;
	int ret = ::setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt,
		sizeof(DWORD)); //���ּ�������ӣ����͡����ֻ������
	if (SOCKET_ERROR == ret)
	{
		printf("setsockopt failed! err=%d\n", WSAGetLastError());
		return false;
	}
	return true;
}

bool Network::setLinger(SOCKET s, bool on, int timeoutSecs)
{
	LINGER linger;
	linger.l_onoff = on;
	linger.l_linger = timeoutSecs;
	int ret = ::setsockopt(s, SOL_SOCKET, SO_LINGER, (char*)&linger,
		sizeof(LINGER)); //�ر�ʱ��δ�������ݣ�������
	if (SOCKET_ERROR == ret)
	{
		printf("setsockopt failed! err=%d\n", WSAGetLastError());
		return false;
	}
	return true;
}

bool Network::updateAcceptContext(SOCKET listenSocket, SOCKET acceptSocket)
{
	int ret = ::setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&listenSocket, sizeof(SOCKET)); //��accept��socket�̳�listen������
	if (SOCKET_ERROR == ret)
	{
		printf("setsockopt failed! err=%d\n", WSAGetLastError());
		return false;
	}
	return true;
}
