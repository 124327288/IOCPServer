#pragma once
#include <WinSock2.h>
#include <MSWSock.h>
#include <vector>
using namespace std;

//���������� (1024*8)
#define MAX_BUFFER_LEN (1024*8)

namespace MyServer 
{
	// ����ɶ˿���Ͷ�ݵ�I/O����������
	enum class OPERATION_TYPE
	{
		UNKNOWN, // ���ڳ�ʼ����������
		ACCEPT, // ��־Ͷ�ݵ�Accept����
		SEND, // ��־Ͷ�ݵ��Ƿ��Ͳ���
		RECV, // ��־Ͷ�ݵ��ǽ��ղ���
	};
	
	//ÿ���׽��ֲ���(�磺AcceptEx, WSARecv, WSASend��)��Ӧ�����ݽṹ��
	//	OVERLAPPED�ṹ(��ʶ���β���)���������׽��֣����������������ͣ�
	struct IoContext
	{
		OVERLAPPED m_Overlapped; // ÿһ���ص�����������ص��ṹ
		SOCKET m_sockAccept; // ������������ʹ�õ�Socket
		WSABUF m_wsaBuf; // WSA���͵Ļ����������ڸ��ص�������������
		char m_szBuffer[MAX_BUFFER_LEN]; // �����WSABUF�������ַ��Ļ�����
		OPERATION_TYPE m_OpType; // ��ʶ�������������(��Ӧ�����ö��)
		DWORD m_nTotalBytes; //�����ܵ��ֽ���
		DWORD m_nSendBytes;	//�Ѿ����͵��ֽ�������δ��������������Ϊ0

		//���캯��
		IoContext()
		{
			this->m_OpType = OPERATION_TYPE::UNKNOWN;
			ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
			ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
			this->m_sockAccept = INVALID_SOCKET;
			this->m_wsaBuf.len = MAX_BUFFER_LEN;
			this->m_wsaBuf.buf = m_szBuffer;

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
			this->m_wsaBuf.len = MAX_BUFFER_LEN;
			this->m_wsaBuf.buf = m_szBuffer;
		}
	};
}