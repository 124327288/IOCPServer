#pragma once
#include "PerIoContext.h"

//=================================================================================
//
//				��������ݽṹ�嶨��(����ÿһ����ɶ˿ڣ�Ҳ����ÿһ��Socket�Ĳ���)
//
//=================================================================================
//ÿ��SOCKET��Ӧ�����ݽṹ(����GetQueuedCompletionStatus����)��-
//SOCKET����SOCKET��Ӧ�Ŀͻ��˵�ַ�������ڸ�SOCKET��������(��Ӧ�ṹIoContext)��
struct SocketContext
{
	SOCKET m_Socket; // ÿһ���ͻ������ӵ�Socket
	SOCKADDR_IN m_ClientAddr; // �ͻ��˵ĵ�ַ
	// �ͻ���������������������ݣ�
	// Ҳ����˵����ÿһ���ͻ���Socket���ǿ���������ͬʱͶ�ݶ��IO�����
	//�׽��ֲ�����������WSARecv��WSASend����һ��IoContext
	CArray<IoContext*> m_arrayIoContext;

	//���캯��
	SocketContext()
	{
		m_Socket = INVALID_SOCKET;
		memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
	}

	//��������
	~SocketContext()
	{
		if (m_Socket != INVALID_SOCKET)
		{
			closesocket(m_Socket);
			m_Socket = INVALID_SOCKET;
		}
		// �ͷŵ����е�IO����������
		for (int i = 0; i < m_arrayIoContext.GetCount(); i++)
		{
			delete m_arrayIoContext.GetAt(i);
		}
		m_arrayIoContext.RemoveAll();
	}

	//�����׽��ֲ���ʱ�����ô˺�������PER_IO_CONTEX�ṹ
	IoContext* GetNewIoContext()
	{
		IoContext* p = new IoContext;
		m_arrayIoContext.Add(p);
		return p;
	}

	// ���������Ƴ�һ��ָ����IoContext
	void RemoveContext(IoContext* pContext)
	{
		ASSERT(pContext != NULL);
		for (int i = 0; i < m_arrayIoContext.GetCount(); i++)
		{
			if (pContext == m_arrayIoContext.GetAt(i))
			{
				delete pContext;
				pContext = NULL;
				m_arrayIoContext.RemoveAt(i);
				break;
			}
		}
	}
};