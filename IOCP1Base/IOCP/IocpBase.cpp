#include "stdafx.h"
#include "IOCPBase.h"
#include <mstcpip.h>

#pragma comment(lib, "WS2_32.lib")

IoContextPool SocketContext::ioContextPool;	// 初始化

CIocpBase::CIocpBase():
	completionPort(INVALID_HANDLE_VALUE),
	listenSockContext(NULL),
	workerThreads(NULL),
	workerThreadNum(0),
	port(DEFAULT_PORT),
	fnAcceptEx(NULL),
	fnGetAcceptExSockAddrs(NULL),
	acceptPostCount(0),
	connectCount(0)
{
	TRACE(L"CIocpBase()\n");
	WSADATA wsaData = { 0 };
	int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (NO_ERROR != nRet)
	{
		int nErr = GetLastError();
	}
	stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

CIocpBase::~CIocpBase()
{
	TRACE(L"~CIocpBase()\n");
	RELEASE_HANDLE(stopEvent);
	this->Stop();
	WSACleanup();
}

BOOL CIocpBase::Start(int port)
{
	TRACE(L"Start() id=%d\n", GetCurrentThreadId());
	this->port = port;
	if (!InitializeIocp())
	{
		return false;
	}
	if (!InitializeListenSocket())
	{
		DeInitialize();
		return false;
	}
	return true;
}

void CIocpBase::Stop()
{
	TRACE(L"Stop()\n");
	if (listenSockContext != NULL
		&& listenSockContext->connSocket != INVALID_SOCKET)
	{
		// 激活关闭事件
		SetEvent(stopEvent);
		for (int i = 0; i < workerThreadNum; i++)
		{
			// 通知所有完成端口退出
			PostQueuedCompletionStatus(completionPort, 0,
				(DWORD)EXIT_CODE, NULL);
		}
		// 等待所有工作线程退出
		WaitForMultipleObjects(workerThreadNum,
			workerThreads, TRUE, INFINITE);
		// 释放其他资源
		DeInitialize();
	}
}

BOOL CIocpBase::SendData(SocketContext* sockContext, char* data, int size)
{
	TRACE(L"SendData(): s=%p d=%p\n", sockContext, data);
	return false;
}

BOOL CIocpBase::InitializeIocp()
{
	TRACE(L"InitializeIocp()\n");
	workerThreadNum = WORKER_THREADS_PER_PROCESSOR * GetNumOfProcessors();
	completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
		NULL, 0, workerThreadNum); //创建完成端口
	if (NULL == completionPort)
	{
		int nErr = GetLastError();
		return false;
	}
	workerThreads = new HANDLE[workerThreadNum];
	for (int i = 0; i < workerThreadNum; i++)
	{
		workerThreads[i] = CreateThread(0, 0,
			WorkerThreadProc, (LPVOID)this, 0, 0);
	}
	return true;
}

BOOL CIocpBase::InitializeListenSocket()
{
	TRACE(L"InitializeListenSocket()\n");
	// 生成用于监听的socket的Context
	listenSockContext = new SocketContext;
	listenSockContext->connSocket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == listenSockContext->connSocket)
	{
		int nErr = GetLastError();
		return false;
	}
	// 将connSocket绑定到完成端口中
	if (NULL == CreateIoCompletionPort((HANDLE)listenSockContext->connSocket,
		completionPort, (DWORD)listenSockContext, 0)) //dwNumberOfConcurrentThreads
	{
		int nErr = GetLastError();
		return false;
	}
	//服务器地址信息，用于绑定socket
	sockaddr_in serverAddr = { 0 };
	// 填充地址信息
	ZeroMemory((char*)&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);
	// 绑定地址和端口
	if (SOCKET_ERROR == bind(listenSockContext->connSocket,
		(sockaddr*)&serverAddr, sizeof(serverAddr)))
	{
		int nErr = GetLastError();
		return false;
	}
	// 开始监听
	if (SOCKET_ERROR == listen(listenSockContext->connSocket, SOMAXCONN))
	{
		int nErr = GetLastError();
		return false;
	}
	// 提取扩展函数指针 win8.1以后才支持
	DWORD dwBytes = 0;
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	GUID guidGetAcceptSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(listenSockContext->connSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx,
		sizeof(guidAcceptEx), &fnAcceptEx, sizeof(fnAcceptEx),
		&dwBytes, NULL, NULL))
	{
		int nErr = GetLastError();
		return false;
	}
	if (SOCKET_ERROR == WSAIoctl(listenSockContext->connSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &guidGetAcceptSockAddrs,
		sizeof(guidGetAcceptSockAddrs), &fnGetAcceptExSockAddrs,
		sizeof(fnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		int nErr = GetLastError();
		return false;
	}
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		IoContext* ioContext = listenSockContext->GetNewIoContext();
		if (ioContext && !PostAccept(listenSockContext, ioContext))
		{
			listenSockContext->RemoveContext(ioContext);
			return false;
		}
	}
	return true;
}

void CIocpBase::DeInitialize()
{
	TRACE(L"DeInitialize()\n");
	// 关闭系统退出事件句柄
	RELEASE_HANDLE(stopEvent);
	// 释放工作者线程句柄指针
	for (int i = 0; i < workerThreadNum; i++)
	{
		RELEASE_HANDLE(workerThreads[i]);
	}
	RELEASE_POINTER(workerThreads);
	// 关闭IOCP句柄
	RELEASE_HANDLE(completionPort);
	// 关闭监听Socket
	if (listenSockContext != NULL)
	{
		RELEASE_SOCKET(listenSockContext->connSocket);
		RELEASE_POINTER(listenSockContext);
	}
}

BOOL CIocpBase::IsSocketAlive(SOCKET sock)
{
	int nByteSent = send(sock, "", 0, 0);
	if (SOCKET_ERROR == nByteSent)
	{
		int nErr = GetLastError();
		return false;
	}
	return true;
}

int CIocpBase::GetNumOfProcessors()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

BOOL CIocpBase::AssociateWithIocp(SocketContext* sockContext)
{
	// 将用于和客户端通信的SOCKET绑定到完成端口中
	HANDLE hTemp = CreateIoCompletionPort((HANDLE)sockContext->connSocket,
		completionPort, (DWORD)sockContext, 0); //dwNumberOfConcurrentThreads
	if (NULL == hTemp)
	{
		int nErr = GetLastError();
		return false;
	}
	return true;
}

BOOL CIocpBase::PostAccept(SocketContext*& sockContext, IoContext*& ioContext)
{//这里的sockContext是listenSockContext
	TRACE(L"PostAccept(): s=%p io=%p\n", sockContext, ioContext);
	DWORD dwBytes = 0;
	ioContext->Reset();
	ioContext->ioType = IOTYPE::ACCEPT;
	ioContext->hSocket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == ioContext->hSocket)
	{
		int nErr = WSAGetLastError();
		return false;
	}
	//https://docs.microsoft.com/zh-cn/windows/win32/api/mswsock/nf-mswsock-acceptex
	// 将接收缓冲置为0,令AcceptEx直接返回,防止拒绝服务攻击
	DWORD dwAddrLen = sizeof(SOCKADDR_IN) + 16; //必须至少+16
	if (!fnAcceptEx(listenSockContext->connSocket, ioContext->hSocket,
		ioContext->wsaBuf.buf, 0, dwAddrLen, dwAddrLen, 
		&dwBytes, &ioContext->overLapped))
	{
		if (WSA_IO_PENDING != WSAGetLastError())
		{
			return false;
		}
	}
	InterlockedIncrement(&acceptPostCount);
	return true;
}

BOOL CIocpBase::PostRecv(SocketContext*& sockContext, IoContext*& ioContext)
{
	TRACE(L"PostRecv(): s=%p io=%p\n", sockContext, ioContext);
	ioContext->Reset();
	ioContext->ioType = IOTYPE::RECV;
	DWORD dwFlags = 0, dwBytes = 0;
	int nBytesRecv = WSARecv(ioContext->hSocket, &ioContext->wsaBuf, 1,
		&dwBytes, &dwFlags, &ioContext->overLapped, NULL);
	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if ((SOCKET_ERROR == nBytesRecv) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		DoClose(sockContext);
		return false;
	}
	return true;
}

BOOL CIocpBase::PostSend(SocketContext*& sockContext, IoContext*& ioContext)
{
	TRACE(L"PostSend(): s=%p io=%p\n", sockContext, ioContext);
	ioContext->Reset();
	ioContext->ioType = IOTYPE::SEND;
	DWORD dwBytes = 0, dwFlags = 0;
	int nRet = WSASend(ioContext->hSocket, &ioContext->wsaBuf, 1,
		&dwBytes, dwFlags, &ioContext->overLapped, NULL);
	if ((nRet != NO_ERROR) && (WSAGetLastError() != WSA_IO_PENDING))
	{
		DoClose(sockContext);
		return false;
	}
	return true;
}

BOOL CIocpBase::DoAccept(SocketContext*& sockContext, IoContext*& ioContext)
{//这里的sockContext是listenSockContext
	TRACE(L"DoAccept(): s=%p io=%p\n", sockContext, ioContext);
	InterlockedIncrement(&connectCount);
	InterlockedDecrement(&acceptPostCount);
	SOCKADDR_IN* clientAddr = NULL;
	SOCKADDR_IN* localAddr = NULL;
	INT clientAddrLen, localAddrLen;
	DWORD dwAddrLen = sizeof(SOCKADDR_IN) + 16; //必须至少+16
	// 1. 获取地址信息 （GetAcceptExSockAddrs可以获取地址，还可以取出数据）
	fnGetAcceptExSockAddrs(ioContext->wsaBuf.buf, 0, dwAddrLen,
		dwAddrLen, (LPSOCKADDR*)&localAddr, &localAddrLen,
		(LPSOCKADDR*)&clientAddr, &clientAddrLen);
	/*if (!fnAcceptEx(listenSockContext->connSocket, ioContext->hSocket,
		ioContext->wsaBuf.buf, 0, sizeof(sockaddr_in),
		sizeof(sockaddr_in), &dwBytes, &ioContext->overLapped))*/

	// 2. 为新连接建立一个SocketContext 
	SocketContext* newSockContext = new SocketContext;
	newSockContext->connSocket = ioContext->hSocket;
	memcpy_s(&(newSockContext->clientAddr), sizeof(SOCKADDR_IN),
		&clientAddr, sizeof(SOCKADDR_IN));

	// 3. 将listenSocketContext的IOContext 重置后继续投递AcceptEx
	if (!PostAccept(sockContext, ioContext))
	{
		sockContext->RemoveContext(ioContext);
	}

	// 4. 将新socket和完成端口绑定
	if (NULL == CreateIoCompletionPort((HANDLE)newSockContext->connSocket,
		completionPort, (DWORD)newSockContext, 0)) //dwNumberOfConcurrentThreads
	{
		DWORD dwErr = WSAGetLastError();
		if (dwErr != ERROR_INVALID_PARAMETER)
		{
			DoClose(newSockContext);
			return false;
		}
	}

	// 并设置tcp_keepalive
	tcp_keepalive alive_in;
	tcp_keepalive alive_out;
	alive_in.onoff = TRUE;
	// 60s  多长时间（ ms ）没有数据就开始 send 心跳包
	alive_in.keepalivetime = 1000 * 60;
	//10s  每隔多长时间（ ms ） send 一个心跳包
	alive_in.keepaliveinterval = 1000 * 10;
	unsigned long ulBytesReturn = 0;
	if (SOCKET_ERROR == WSAIoctl(newSockContext->connSocket,
		SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in), &alive_out,
		sizeof(alive_out), &ulBytesReturn, NULL, NULL))
	{
		TRACE(L"WSAIoctl() failed: %d\n", WSAGetLastError());
	}
	OnConnectionAccepted(newSockContext);

	// 5. 建立recv操作所需的ioContext，在新连接的socket上投递recv请求
	IoContext* newIoContext = newSockContext->GetNewIoContext();
	if (newIoContext != NULL)
	{//不成功，会怎么样？
		newIoContext->ioType = IOTYPE::RECV;
		newIoContext->hSocket = newSockContext->connSocket;
		// 投递recv请求
		return PostRecv(newSockContext, newIoContext);
	}
	else 
	{
		DoClose(newSockContext);
		return false;
	}
}

BOOL CIocpBase::DoRecv(SocketContext*& sockContext, IoContext*& ioContext)
{
	TRACE(L"DoRecv(): s=%p io=%p\n", sockContext, ioContext);
	OnRecvCompleted(sockContext, ioContext);
	return PostRecv(sockContext, ioContext);
}

BOOL CIocpBase::DoSend(SocketContext*& sockContext, IoContext*& ioContext)
{
	TRACE(L"DoSend(): s=%p io=%p\n", sockContext, ioContext);
	OnSendCompleted(sockContext, ioContext);
	return 0;
}

BOOL CIocpBase::DoClose(SocketContext*& sockContext)
{
	if (sockContext != listenSockContext)
	{
		TRACE(L"DoClose(): s=%p\n", sockContext);
		InterlockedDecrement(&connectCount);
		RELEASE_POINTER(sockContext);
		return true;
	}
	return true;
}

DWORD CIocpBase::WorkerThreadProc(LPVOID pThiz)
{
	CIocpBase* iocp = (CIocpBase*)pThiz;
	SocketContext* sockContext = NULL;
	IoContext* ioContext = NULL;
	OVERLAPPED* ol = NULL;
	DWORD dwBytes = 0;

	TRACE(L"WorkerThreadProc(): begin. p=%p id=%d\n",
		pThiz, GetCurrentThreadId());
	while (WAIT_OBJECT_0 != WaitForSingleObject(iocp->stopEvent, 0))
	{
		BOOL bRet = GetQueuedCompletionStatus(iocp->completionPort,
			&dwBytes, (PULONG_PTR)&sockContext, &ol, INFINITE);
		// 读取传入的参数
		ioContext = CONTAINING_RECORD(ol, IoContext, overLapped);
		// 收到退出标志
		if (EXIT_CODE == (DWORD)sockContext)
		{
			break;
		}

		if (!bRet)
		{
			DWORD dwErr = GetLastError();
			// 如果是超时了，就再继续等吧  
			if (WAIT_TIMEOUT == dwErr)
			{
				// 确认客户端是否还活着...
				if (!iocp->IsSocketAlive(sockContext->connSocket))
				{
					iocp->OnConnectionClosed(sockContext);					
					iocp->DoClose(sockContext); // 回收socket
					continue;
				}
				else
				{
					continue;
				}
			}
			// 可能是客户端异常退出了(64)
			else if (ERROR_NETNAME_DELETED == dwErr)
			{
				iocp->OnConnectionError(sockContext, dwErr);				
				iocp->DoClose(sockContext); // 回收socket
				continue;
			}
			else
			{
				iocp->OnConnectionError(sockContext, dwErr);				
				iocp->DoClose(sockContext); // 回收socket
				continue;
			}
		}
		else
		{
			// 判断是否有客户端断开
			if ((0 == dwBytes)
				&& (IOTYPE::RECV == ioContext->ioType
					|| IOTYPE::SEND == ioContext->ioType))
			{
				iocp->OnConnectionClosed(sockContext);
				iocp->DoClose(sockContext); // 回收socket
				continue;
			}
			else
			{
				switch (ioContext->ioType)
				{
				case IOTYPE::ACCEPT:
					iocp->DoAccept(sockContext, ioContext);
					break;
				case IOTYPE::RECV:
					iocp->DoRecv(sockContext, ioContext);
					break;
				case IOTYPE::SEND:
					iocp->DoSend(sockContext, ioContext);
					break;
				default:
					break;
				}
			}
		}
	}

	TRACE(L"WorkerThreadProc(): end. p=%p id=%d\n",
		pThiz, GetCurrentThreadId());
	// 多线程只有一份，不能删吧
	//RELEASE_POINTER(pThiz);
	return 0;
}