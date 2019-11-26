#pragma once
#include "PerIoContext.h"
#include <vector>
using std::vector;

//============================================================================
//	单句柄数据结构体定义(用于每一个完成端口，也就是每一个Socket的参数)
//============================================================================
//每个SOCKET对应的数据结构(调用GetQueuedCompletionStatus传入)：-
//SOCKET，该SOCKET对应的客户端地址，作用在该SOCKET操作集合(对应结构IoContext)；
struct SocketContext
{
	SOCKET m_Socket; // 每一个客户端连接的Socket
	SOCKADDR_IN m_ClientAddr; // 客户端的地址
	// 客户端网络操作的上下文数据，也就是说
	// 对于每一个客户端Socket，是可以在上面同时投递多个IO请求的
	//套接字操作，本例是WSARecv和WSASend共用一个IoContext
	vector<IoContext*> m_arrayIoContext;

	//构造函数
	SocketContext()
	{
		m_Socket = INVALID_SOCKET;
		memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
	}

	//析构函数
	~SocketContext()
	{
		if (m_Socket != INVALID_SOCKET)
		{
			closesocket(m_Socket); //!
			m_Socket = INVALID_SOCKET;
		}
		// 释放掉所有的IO上下文数据
		for (size_t i = 0; i < m_arrayIoContext.size(); i++)
		{
			delete m_arrayIoContext.at(i);
		}
		m_arrayIoContext.clear();
	}

	//进行套接字操作时，调用此函数返回PER_IO_CONTEX结构
	IoContext* GetNewIoContext()
	{
		IoContext* p = new IoContext;
		m_arrayIoContext.push_back(p);
		return p;
	}

	// 从数组中移除一个指定的IoContext
	void RemoveContext(IoContext* pContext)
	{
		if (pContext == nullptr)
		{
			return;
		}
		vector<IoContext*>::iterator it;
		it = m_arrayIoContext.begin();
		while (it != m_arrayIoContext.end())
		{
			IoContext* pObj = *it;
			if (pContext == pObj)
			{
				delete pContext;
				pContext = nullptr;
				it = m_arrayIoContext.erase(it);
				break;
			}
			it++;
		}
	}
};