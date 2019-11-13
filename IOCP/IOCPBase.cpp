#include "stdafx.h"
#include "IOCPBase.h"
#include <mstcpip.h>

#pragma comment(lib, "WS2_32.lib")

IoContextPool SocketContext::ioContextPool;	// ��ʼ��

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
		// ����ر��¼�
		SetEvent(stopEvent);
		for (int i = 0; i < workerThreadNum; i++)
		{
			// ֪ͨ������ɶ˿��˳�
			PostQueuedCompletionStatus(completionPort, 0,
				(DWORD)EXIT_CODE, NULL);
		}
		// �ȴ����й����߳��˳�
		WaitForMultipleObjects(workerThreadNum,
			workerThreads, TRUE, INFINITE);
		// �ͷ�������Դ
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
		NULL, 0, workerThreadNum); //������ɶ˿�
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
	// �������ڼ�����socket��Context
	listenSockContext = new SocketContext;
	listenSockContext->connSocket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == listenSockContext->connSocket)
	{
		int nErr = GetLastError();
		return false;
	}
	// ��connSocket�󶨵���ɶ˿���
	if (NULL == CreateIoCompletionPort((HANDLE)listenSockContext->connSocket,
		completionPort, (DWORD)listenSockContext, 0)) //dwNumberOfConcurrentThreads
	{
		int nErr = GetLastError();
		return false;
	}
	//��������ַ��Ϣ�����ڰ�socket
	sockaddr_in serverAddr = { 0 };
	// ����ַ��Ϣ
	ZeroMemory((char*)&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);
	// �󶨵�ַ�Ͷ˿�
	if (SOCKET_ERROR == bind(listenSockContext->connSocket,
		(sockaddr*)&serverAddr, sizeof(serverAddr)))
	{
		int nErr = GetLastError();
		return false;
	}
	// ��ʼ����
	if (SOCKET_ERROR == listen(listenSockContext->connSocket, SOMAXCONN))
	{
		int nErr = GetLastError();
		return false;
	}
	// ��ȡ��չ����ָ��
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
	// �ر�ϵͳ�˳��¼����
	RELEASE_HANDLE(stopEvent);
	// �ͷŹ������߳̾��ָ��
	for (int i = 0; i < workerThreadNum; i++)
	{
		RELEASE_HANDLE(workerThreads[i]);
	}
	RELEASE_POINTER(workerThreads);
	// �ر�IOCP���
	RELEASE_HANDLE(completionPort);
	// �رռ���Socket
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
	// �����ںͿͻ���ͨ�ŵ�SOCKET�󶨵���ɶ˿���
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
{//�����sockContext��listenSockContext
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
	// �����ջ�����Ϊ0,��AcceptExֱ�ӷ���,��ֹ�ܾ����񹥻�
	if (!fnAcceptEx(listenSockContext->connSocket, ioContext->hSocket,
		ioContext->wsaBuf.buf, 0, sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16, &dwBytes, &ioContext->overLapped))
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
	// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
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
{//�����sockContext��listenSockContext
	TRACE(L"DoAccept(): s=%p io=%p\n", sockContext, ioContext);
	InterlockedIncrement(&connectCount);
	InterlockedDecrement(&acceptPostCount);
	SOCKADDR_IN* clientAddr = NULL;
	SOCKADDR_IN* localAddr = NULL;
	int clientAddrLen, localAddrLen;

	clientAddrLen = localAddrLen = sizeof(SOCKADDR_IN);
	// 1. ��ȡ��ַ��Ϣ ��GetAcceptExSockAddrs�����������Ի�ȡ��ַ��Ϣ��������˳��ȡ����һ�����ݣ�
	fnGetAcceptExSockAddrs(ioContext->wsaBuf.buf, 0, localAddrLen, clientAddrLen,
		(LPSOCKADDR*)&localAddr, &localAddrLen, (LPSOCKADDR*)&clientAddr, &clientAddrLen);

	// 2. Ϊ�����ӽ���һ��SocketContext 
	SocketContext* newSockContext = new SocketContext;
	newSockContext->connSocket = ioContext->hSocket;
	memcpy_s(&(newSockContext->clientAddr), sizeof(SOCKADDR_IN),
		&clientAddr, sizeof(SOCKADDR_IN));

	// 3. ��listenSocketContext��IOContext ���ú����Ͷ��AcceptEx
	if (!PostAccept(sockContext, ioContext))
	{
		sockContext->RemoveContext(ioContext);
	}

	// 4. ����socket����ɶ˿ڰ�
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

	// ������tcp_keepalive
	tcp_keepalive alive_in;
	tcp_keepalive alive_out;
	alive_in.onoff = TRUE;
	// 60s  �೤ʱ�䣨 ms ��û�����ݾͿ�ʼ send ������
	alive_in.keepalivetime = 1000 * 60;
	//10s  ÿ���೤ʱ�䣨 ms �� send һ��������
	alive_in.keepaliveinterval = 1000 * 10;
	unsigned long ulBytesReturn = 0;
	if (SOCKET_ERROR == WSAIoctl(newSockContext->connSocket,
		SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in), &alive_out,
		sizeof(alive_out), &ulBytesReturn, NULL, NULL))
	{
		TRACE(L"WSAIoctl() failed: %d\n", WSAGetLastError());
	}
	OnConnectionAccepted(newSockContext);

	// 5. ����recv���������ioContext���������ӵ�socket��Ͷ��recv����
	IoContext* newIoContext = newSockContext->GetNewIoContext();
	if (newIoContext != NULL)
	{//���ɹ�������ô����
		newIoContext->ioType = IOTYPE::RECV;
		newIoContext->hSocket = newSockContext->connSocket;
		// Ͷ��recv����
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
	if (sockContext != NULL)
	{
		TRACE(L"DoClose(): s=%p\n", sockContext);
		InterlockedDecrement(&connectCount);
		RELEASE_POINTER(sockContext);
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
		// ��ȡ����Ĳ���
		ioContext = CONTAINING_RECORD(ol, IoContext, overLapped);
		// �յ��˳���־
		if (EXIT_CODE == (DWORD)sockContext)
		{
			break;
		}

		if (!bRet)
		{
			DWORD dwErr = GetLastError();
			// ����ǳ�ʱ�ˣ����ټ����Ȱ�  
			if (WAIT_TIMEOUT == dwErr)
			{
				// ȷ�Ͽͻ����Ƿ񻹻���...
				if (!iocp->IsSocketAlive(sockContext->connSocket))
				{
					iocp->OnConnectionClosed(sockContext);					
					iocp->DoClose(sockContext); // ����socket
					continue;
				}
				else
				{
					continue;
				}
			}
			// �����ǿͻ����쳣�˳���(64)
			else if (ERROR_NETNAME_DELETED == dwErr)
			{
				iocp->OnConnectionError(sockContext, dwErr);				
				iocp->DoClose(sockContext); // ����socket
				continue;
			}
			else
			{
				iocp->OnConnectionError(sockContext, dwErr);				
				iocp->DoClose(sockContext); // ����socket
				continue;
			}
		}
		else
		{
			// �ж��Ƿ��пͻ��˶Ͽ�
			if ((0 == dwBytes)
				&& (IOTYPE::RECV == ioContext->ioType
					|| IOTYPE::SEND == ioContext->ioType))
			{
				iocp->OnConnectionClosed(sockContext);
				iocp->DoClose(sockContext); // ����socket
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
	// ���߳�ֻ��һ�ݣ�����ɾ��
	//RELEASE_POINTER(pThiz);
	return 0;
}