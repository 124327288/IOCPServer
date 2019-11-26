#pragma once
#include <WinSock2.h>
#include <Windows.h>

// ���������� (1024*8) ֮����Ϊʲô����8K��Ҳ��һ�������ϵľ���ֵ
// ���ȷʵ�ͻ��˷�����ÿ�����ݶ��Ƚ��٣���ô�����õ�СһЩ��ʡ�ڴ�
constexpr int MAX_BUFFER_LEN = (1024 * 8);

// ����ɶ˿���Ͷ�ݵ�I/O����������
enum class PostType
{
	UNKNOWN, // ���ڳ�ʼ����������
	ACCEPT, // ��־Ͷ�ݵ�Accept����
	SEND, // ��־Ͷ�ݵ��Ƿ��Ͳ���
	RECV, // ��־Ͷ�ݵ��ǽ��ղ���
};

enum class PostResult
{
	SUCCESS, //�ɹ�
	FAILED, //ʧ��
	INVALID, //�׽�����Ч
};

//===============================================================================
//				��IO���ݽṹ�嶨��(����ÿһ���ص������Ĳ���)
//===============================================================================
// ÿ���׽��ֲ���(�磺AcceptEx, WSARecv, WSASend��)��Ӧ�����ݽṹ��
// OVERLAPPED�ṹ(��ʶ���β���)���������׽��֣����������������ͣ�
struct IoContext
{
	OVERLAPPED m_Overlapped; //ÿһ���ص�io������Ҫ��һ��OVERLAPPED�ṹ
	PostType m_PostType; // ��ʶ�������������(��Ӧ�����ö��)
	WSABUF m_wsaBuf; // WSA���͵Ļ����������ڸ��ص�������������
	SOCKET m_acceptSocket; // ������������ʹ�õ�Socket
	char m_szBuffer[MAX_BUFFER_LEN]; // �����WSABUF�������ַ��Ļ�����
	DWORD m_nTotalBytes; //�����ܵ��ֽ���
	DWORD m_nSentBytes;	//�Ѿ����͵��ֽ�������δ��������������Ϊ0

	//���캯��
	IoContext()
	{
		m_PostType = PostType::UNKNOWN;
		ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
		ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
		m_acceptSocket = INVALID_SOCKET;
		m_wsaBuf.len = MAX_BUFFER_LEN;
		m_wsaBuf.buf = m_szBuffer;
		m_nTotalBytes = 0;
		m_nSentBytes = 0;
	}
	//��������
	~IoContext()
	{
		if (m_acceptSocket != INVALID_SOCKET)
		{
			//SocketContext�Ѿ��رչ���
			//closesocket(m_sockAccept); //!
			m_acceptSocket = INVALID_SOCKET;
		}
	}
	//���û���������
	void ResetBuffer()
	{
		ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
		m_wsaBuf.len = MAX_BUFFER_LEN;
		m_wsaBuf.buf = m_szBuffer;
	}
};
