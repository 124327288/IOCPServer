#pragma once
#include "Addr.h"
#include "Buffer.h"
#include "PerIoContext.h"
#include <algorithm>
#include <string>
#include <queue>
#include <map>


struct SocketContext
{
	SOCKET m_socket; //socket连接
	Addr m_addr; //客户端地址
	ULONG m_nPendingIoCnt;

	SocketContext(const SOCKET& socket = INVALID_SOCKET, 
		ULONG nPendingIoCnt = 0);
};

struct ListenContext : public SocketContext
{
	//接收连接的IO上下文列表
	std::vector<AcceptIoContext*> m_acceptIoCtxList; 
	ListenContext(short port, const std::string& ip = "0.0.0.0");
};

//============================================================================
//	单句柄数据结构体定义(用于每一个完成端口，也就是每一个Socket的参数)
//============================================================================
//每个SOCKET对应的数据结构(调用GetQueuedCompletionStatus传入)：-
//SOCKET，该SOCKET对应的客户端地址，作用在该SOCKET操作集合(对应结构IoContext)；
struct ClientContext : public SocketContext
{
	CRITICAL_SECTION m_csLock; //保护ClientContext
	RecvIoContext* m_recvIoCtx;
	SendIoContext* m_sendIoCtx;	
	std::queue<Buffer> m_outBufQueue;
	Buffer m_inBuf;
	Buffer m_outBuf;
	
	ClientContext(const SOCKET& socket = INVALID_SOCKET);	
	~ClientContext(); //socket由IocpServer释放
	void reset();

	void appendToBuffer(PBYTE pInBuf, size_t len);
	void appendToBuffer(const std::string& inBuf);
};
