#pragma once
#include <WinSock2.h>
#include <Windows.h>


// ���������� (1024*8) ֮����Ϊʲô����8K��Ҳ��һ�������ϵľ���ֵ
// ���ȷʵ�ͻ��˷�����ÿ�����ݶ��Ƚ��٣���ô�����õ�СһЩ��ʡ�ڴ�
constexpr int MAX_BUFFER_LEN = (1024 * 8);

constexpr int ACCEPT_ADDRS_SIZE = sizeof(SOCKADDR_IN) + 16;
constexpr int DOUBLE_ACCEPT_ADDRS_SIZE = (sizeof(SOCKADDR_IN) + 16) * 2;

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
	PostResultSucc, //�ɹ�
	PostResultFailed, //ʧ��
	PostResultInvalidSocket, //�׽�����Ч
};

//===============================================================================
//				��IO���ݽṹ�嶨��(����ÿһ���ص������Ĳ���)
//===============================================================================
// ÿ���׽��ֲ���(�磺AcceptEx, WSARecv, WSASend��)��Ӧ�����ݽṹ��
// OVERLAPPED�ṹ(��ʶ���β���)���������׽��֣����������������ͣ�
struct IoContext
{
	//ÿһ���ص�io������Ҫ��һ��OVERLAPPED�ṹ
	OVERLAPPED m_Overlapped;
	PostType m_PostType;
	WSABUF m_wsaBuf;

	IoContext(PostType type);
	~IoContext();
	void resetBuffer();
};

struct AcceptIoContext : public IoContext
{
	AcceptIoContext(SOCKET acceptSocket = INVALID_SOCKET);
	~AcceptIoContext();
	void resetBuffer();
	SOCKET m_acceptSocket; //�������ӵ�socket
	BYTE m_accpetBuf[MAX_BUFFER_LEN]; //����acceptEx��������
};

struct RecvIoContext : public IoContext
{
	RecvIoContext();
	~RecvIoContext();
	void resetBuffer();
	BYTE m_recvBuf[MAX_BUFFER_LEN];
};

struct SendIoContext : public IoContext
{
	SendIoContext();
	~SendIoContext();
};