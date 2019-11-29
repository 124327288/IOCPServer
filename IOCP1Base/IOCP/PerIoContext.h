#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <Assert.h>

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

	IoContext(PostType type);
	~IoContext();
	void ResetBuffer();
};

struct AcceptIoContext : public IoContext
{
	AcceptIoContext(SOCKET acceptSocket = INVALID_SOCKET);
	~AcceptIoContext();
	void ResetBuffer();
	SOCKET m_acceptSocket; //�������ӵ�socket
	BYTE m_acceptBuf[MAX_BUFFER_LEN]; //����acceptEx��������
};

struct RecvIoContext : public IoContext
{
	RecvIoContext();
	~RecvIoContext();
	void ResetBuffer();
	BYTE m_recvBuf[MAX_BUFFER_LEN];
};

struct SendIoContext : public IoContext
{
	SendIoContext();
	~SendIoContext();
};