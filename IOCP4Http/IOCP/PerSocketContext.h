#pragma once
#include "Addr.h"
#include "Buffer.h"
#include "PerIoContext.h"
#include <algorithm>
#include <string>
#include <queue>
#include <map>

struct ListenContext
{
	SOCKET m_socket; //����socket
	SOCKADDR_IN m_addr; //������ַ	
	ULONG m_nPendingIoCnt;
	//�������ӵ�IO�������б�
	std::vector<AcceptIoContext*> m_acceptIoCtxList; 

	ListenContext(short port, const std::string& ip = "0.0.0.0");
};

//============================================================================
//	��������ݽṹ�嶨��(����ÿһ����ɶ˿ڣ�Ҳ����ÿһ��Socket�Ĳ���)
//============================================================================
//ÿ��SOCKET��Ӧ�����ݽṹ(����GetQueuedCompletionStatus����)��-
//SOCKET����SOCKET��Ӧ�Ŀͻ��˵�ַ�������ڸ�SOCKET��������(��Ӧ�ṹIoContext)��
struct ClientContext
{
	SOCKET m_socket; //�ͻ���socket
	Addr m_addr; //�ͻ��˵�ַ
	ULONG m_nPendingIoCnt;	
	RecvIoContext* m_recvIoCtx;
	SendIoContext* m_sendIoCtx;	
	std::queue<Buffer> m_outBufQueue;
	Buffer m_inBuf;
	Buffer m_outBuf;
	CRITICAL_SECTION m_csLock; //����ClientContext
	
	ClientContext(const SOCKET& socket = INVALID_SOCKET);	
	~ClientContext(); //socket��IocpServer�ͷ�
	void reset();

	void appendToBuffer(PBYTE pInBuf, size_t len);
	void appendToBuffer(const std::string& inBuf);
};
