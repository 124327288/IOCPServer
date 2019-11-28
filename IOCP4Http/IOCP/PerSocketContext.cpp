#include <ws2tcpip.h>
#include <assert.h>
#include "Network.h"
#include "PerIoContext.h"
#include "PerSocketContext.h"
#include <iostream>


SocketContext::SocketContext(const SOCKET& socket,
	ULONG nPendingIoCnt ) : m_socket(socket)
	, m_nPendingIoCnt(nPendingIoCnt)
{
	SecureZeroMemory(&m_addr, sizeof(SOCKADDR_IN));
}

ListenContext::ListenContext(short port, const std::string& ip)
{
	SOCKADDR_IN addr; //¼àÌýµØÖ·	
    SecureZeroMemory(&addr, sizeof(SOCKADDR_IN));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    //m_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);
	m_addr = addr;
    m_socket = Network::socket();
    assert(SOCKET_ERROR != m_socket);
}

ClientContext::ClientContext(const SOCKET& socket) :
	SocketContext(socket), m_recvIoCtx(new RecvIoContext())
    , m_sendIoCtx(new SendIoContext())
{
    SecureZeroMemory(&m_addr, sizeof(SOCKADDR_IN));
    InitializeCriticalSection(&m_csLock);
}

ClientContext::~ClientContext()
{
    delete m_recvIoCtx;
    delete m_sendIoCtx;
    m_recvIoCtx = nullptr;
    m_sendIoCtx = nullptr;
    LeaveCriticalSection(&m_csLock);
}

void ClientContext::reset()
{
    assert(0 == m_nPendingIoCnt);
    assert(m_outBufQueue.empty());
    SecureZeroMemory(&m_addr, sizeof(SOCKADDR_IN));
}

void ClientContext::appendToBuffer(PBYTE pInBuf, size_t len)
{
    m_inBuf.write((PBYTE)pInBuf, len);
}

void ClientContext::appendToBuffer(const std::string& inBuf)
{
    m_inBuf.write(inBuf);
}
