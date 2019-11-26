#ifndef __CLIENT_CONTEXT_H__
#define __CLIENT_CONTEXT_H__
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
	RecvIoContext* m_recvIoCtx;
	SendIoContext* m_sendIoCtx;	
	std::queue<Buffer> m_outBufQueue;
	Buffer m_inBuf;
	Buffer m_outBuf;
	CRITICAL_SECTION m_csLock; //����ClientContext
	//Avoids Access Violation����ֵΪ0ʱ�����ͷ�ClientContext
	ULONG m_nPendingIoCnt;
	
	ClientContext(const SOCKET& socket = INVALID_SOCKET);	
	~ClientContext(); //socket��IocpServer�ͷ�
	void reset();

	void appendToBuffer(PBYTE pInBuf, size_t len);
	void appendToBuffer(const std::string& inBuf);
};

#endif // !__CLIENT_CONTEXT_H__

