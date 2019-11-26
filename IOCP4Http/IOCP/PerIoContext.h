#pragma once
#include <WinSock2.h>
#include <Windows.h>


// 缓冲区长度 (1024*8) 之所以为什么设置8K，也是一个江湖上的经验值
// 如果确实客户端发来的每组数据都比较少，那么就设置得小一些，省内存
constexpr int MAX_BUFFER_LEN = (1024 * 8);

constexpr int ACCEPT_ADDRS_SIZE = sizeof(SOCKADDR_IN) + 16;
constexpr int DOUBLE_ACCEPT_ADDRS_SIZE = (sizeof(SOCKADDR_IN) + 16) * 2;

// 在完成端口上投递的I/O操作的类型
enum class PostType
{
	UNKNOWN, // 用于初始化，无意义
	ACCEPT, // 标志投递的Accept操作
	SEND, // 标志投递的是发送操作
	RECV, // 标志投递的是接收操作
};

enum class PostResult
{
	PostResultSucc, //成功
	PostResultFailed, //失败
	PostResultInvalidSocket, //套接字无效
};

//===============================================================================
//				单IO数据结构体定义(用于每一个重叠操作的参数)
//===============================================================================
// 每次套接字操作(如：AcceptEx, WSARecv, WSASend等)对应的数据结构：
// OVERLAPPED结构(标识本次操作)，关联的套接字，缓冲区，操作类型；
struct IoContext
{
	//每一个重叠io操作都要有一个OVERLAPPED结构
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
	SOCKET m_acceptSocket; //接受连接的socket
	BYTE m_accpetBuf[MAX_BUFFER_LEN]; //用于acceptEx接收数据
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