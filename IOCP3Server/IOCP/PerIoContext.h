#pragma once
#include <WinSock2.h>
#include <MSWSock.h>
#include <vector>
using namespace std; 

// ���������� (1024*8) ֮����Ϊʲô����8K��Ҳ��һ�������ϵľ���ֵ
// ���ȷʵ�ͻ��˷�����ÿ�����ݶ��Ƚ��٣���ô�����õ�СһЩ��ʡ�ڴ�
#define MAX_BUFFER_LEN 8192 

// ����ɶ˿���Ͷ�ݵ�I/O����������
enum class OPERATION_TYPE
{
	UNKNOWN, // ���ڳ�ʼ����������
	ACCEPT, // ��־Ͷ�ݵ�Accept����
	SEND, // ��־Ͷ�ݵ��Ƿ��Ͳ���
	RECV, // ��־Ͷ�ݵ��ǽ��ղ���
};

//===============================================================================
//
//				��IO���ݽṹ�嶨��(����ÿһ���ص������Ĳ���)
//
//===============================================================================
//ÿ���׽��ֲ���(�磺AcceptEx, WSARecv, WSASend��)��Ӧ�����ݽṹ��
//OVERLAPPED�ṹ(��ʶ���β���)���������׽��֣����������������ͣ�
struct IoContext
{
	// ÿһ���ص�����������ص��ṹ
	OVERLAPPED m_Overlapped; //(���ÿһ��Socket��ÿһ����������Ҫ��һ��) 
	SOCKET m_sockAccept; // ������������ʹ�õ�Socket
	WSABUF m_wsaBuf; // WSA���͵Ļ����������ڸ��ص�������������
	char m_szBuffer[MAX_BUFFER_LEN]; // �����WSABUF�������ַ��Ļ�����
	OPERATION_TYPE m_OpType; // ��ʶ�������������(��Ӧ�����ö��)

	DWORD m_nTotalBytes; //�����ܵ��ֽ���
	DWORD m_nSendBytes;	//�Ѿ����͵��ֽ�������δ��������������Ϊ0

	//���캯��
	IoContext()
	{
		ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
		ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
		m_sockAccept = INVALID_SOCKET;
		m_wsaBuf.buf = m_szBuffer;
		m_wsaBuf.len = MAX_BUFFER_LEN;
		m_OpType = OPERATION_TYPE::UNKNOWN;

		m_nTotalBytes = 0;
		m_nSendBytes = 0;
	}
	//��������
	~IoContext()
	{
		if (m_sockAccept != INVALID_SOCKET)
		{
			closesocket(m_sockAccept);
			m_sockAccept = INVALID_SOCKET;
		}
	}
	//���û���������
	void ResetBuffer()
	{
		ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
		m_wsaBuf.buf = m_szBuffer;
		m_wsaBuf.len = MAX_BUFFER_LEN;
	}
};
