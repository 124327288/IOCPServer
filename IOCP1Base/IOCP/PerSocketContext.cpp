#include <ws2tcpip.h>
#include "Network.h"
#include "PerIoContext.h"
#include "PerSocketContext.h"
#include <iostream>


SocketContext::SocketContext(const SOCKET& socket,
	ULONG nPendingIoCnt) : m_socket(socket)
	, m_nPendingIoCnt(nPendingIoCnt)
{
	SecureZeroMemory(&m_addr, sizeof(SOCKADDR_IN));
}

SocketContext::~SocketContext()
{
	RELEASE_SOCKET(m_socket);
}

void SocketContext::reset()
{
	SecureZeroMemory(&m_addr, sizeof(SOCKADDR_IN));
	RELEASE_SOCKET(m_socket);
	m_nPendingIoCnt = 0;
}


//ListenContext
ListenContext::ListenContext(short port, const std::string& ip)
{
	SocketContext::SocketContext();
	SOCKADDR_IN addr; //监听地址	
	SecureZeroMemory(&addr, sizeof(SOCKADDR_IN));
	inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
	//m_addr.sin_addr.s_addr = inet_addr(ip.c_str());
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	m_addr = addr;
	m_socket = Network::socket();
	assert(SOCKET_ERROR != m_socket);
}

ListenContext::~ListenContext()
{
	std::vector<AcceptIoContext*>::iterator it;
	for (it = m_acceptIoCtxList.begin();
		it != m_acceptIoCtxList.end(); ++it)
	{//将所有的acceptIoCtx释放掉，对应的socket关闭掉
		AcceptIoContext* pAcceptIoCtx = (*it);
		int bRet = CancelIoEx((HANDLE)pAcceptIoCtx->m_acceptSocket,
			&pAcceptIoCtx->m_Overlapped);  //取消掉IO
		//int bRet = CancelIo((HANDLE)pAcceptIoCtx->m_acceptSocket);
		if (bRet)
		{
			while (!HasOverlappedIoCompleted(&pAcceptIoCtx->m_Overlapped))
			{//等IO结束
				Sleep(1);
			}
		}
		else if(ERROR_NOT_FOUND != WSAGetLastError())
		{
			printf("CancelIoEx failed! err=%d\n", WSAGetLastError());
			//continue; // return; //这个是匿名函数
		}
		RELEASE_POINTER(pAcceptIoCtx);
	}
	m_acceptIoCtxList.clear();
}

//ClientContext
ClientContext::ClientContext(const SOCKET& socket) :
	SocketContext(socket), m_recvIoCtx(new RecvIoContext())
	, m_sendIoCtx(new SendIoContext())
{
	InitializeCriticalSection(&m_csLock);
}

ClientContext::~ClientContext()
{
	reset(); //socket可以先释放
	RELEASE_POINTER(m_recvIoCtx);
	RELEASE_POINTER(m_sendIoCtx);
	LeaveCriticalSection(&m_csLock);
}

void ClientContext::reset()
{
	SocketContext::reset();
	while (!m_outBufQueue.empty())
	{//pop时，元素会自动析构
		m_outBufQueue.pop();
	}
	m_outBuf.clear();
	m_inBuf.clear();
}

void ClientContext::appendToBuffer(PBYTE pInBuf, size_t len)
{
	m_inBuf.write((PBYTE)pInBuf, len);
}

void ClientContext::appendToBuffer(const std::string& inBuf)
{
	m_inBuf.write(inBuf);
}
