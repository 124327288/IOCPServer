#ifndef __IO_CONTEXT_H__
#define __IO_CONTEXT_H__
#include "Global.h"
#include "Buffer.h"

//Ϊ�˱�֤���½ṹ���һ��������Ա��OVERLAPPED��
//����ʹ���麯���������麯�������һ�����������ָ�룩
struct IoContext
{
	IoContext(PostType type);
	~IoContext();

	void resetBuffer();

	//ÿһ���ص�io������Ҫ��һ��OVERLAPPED�ṹ
	OVERLAPPED m_overlapped;
	PostType m_postType;
	WSABUF m_wsaBuf; 
};

struct AcceptIoContext : public IoContext
{
	AcceptIoContext(SOCKET acceptSocket = INVALID_SOCKET);
	~AcceptIoContext();
	void resetBuffer();
	SOCKET m_acceptSocket; //�������ӵ�socket
	BYTE m_accpetBuf[IO_BUF_SIZE]; //����acceptEx��������
};

struct RecvIoContext : public IoContext
{
	RecvIoContext();
	~RecvIoContext();
	void resetBuffer();
	BYTE m_recvBuf[IO_BUF_SIZE];
};

#endif // !__IO_CONTEXT_H__
