#include "Network.h"
#include "LockGuard.h"
#include "PerIoContext.h"
#include "PerSocketContext.h"
#include "IocpServer.h"
#include <assert.h>
#include <process.h>
#include <mswsock.h>
//for struct tcp_keepalive
#include <mstcpip.h>
#include <thread>
#include <iostream>
using namespace std;

IocpServer::IocpServer(short listenPort, int maxConnCount) :
	m_bIsShutdown(false), m_listenPort(listenPort)
	, m_nMaxConnClientCnt(maxConnCount)
	, m_hIOCompletionPort(nullptr)
	, m_hExitEvent(nullptr)
	, m_nWorkerCnt(0)
	, m_nConnClientCnt(0)
	, m_pListenCtx(nullptr)
	, m_lpfnGetAcceptExSockAddrs(nullptr)
	, m_lpfnAcceptEx(nullptr)
{
	InitializeCriticalSection(&m_csLog);
	showMessage("IocpServer() listenPort=%d", listenPort);
	//手动reset，初始状态为nonsignaled
	m_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (WSA_INVALID_EVENT == m_hExitEvent)
	{
		showMessage("CreateEvent failed with error: %d", WSAGetLastError());
	}
	InitializeCriticalSection(&m_csClientList);
}

IocpServer::~IocpServer()
{
	Stop();
	Network::unInit();
	DeleteCriticalSection(&m_csClientList);
	showMessage("~IocpServer()");
	DeleteCriticalSection(&m_csLog);
}

bool IocpServer::Start()
{
	showMessage("Start()");
	if (!Network::init())
	{
		showMessage("network initial failed");
		return false;
	}
	if (!createListenSocket(m_listenPort))
	{
		return false;
	}
	if (!createIocpWorker())
	{
		return false;
	}
	if (!initAcceptIoContext())
	{
		return false;
	}
	showMessage("Start() done\n");
	return true;
}

bool IocpServer::Stop()
{
	showMessage("Stop()");
	//同步等待所有工作线程退出
	exitIocpWorker();
	//关闭工作线程句柄
	for_each(m_hWorkerThreads.begin(), m_hWorkerThreads.end(),
		[](const HANDLE& h) { CloseHandle(h); });
	for_each(m_acceptIoCtxList.begin(), m_acceptIoCtxList.end(),
		[](AcceptIoContext* mAcceptIoCtx) {
			CancelIo((HANDLE)mAcceptIoCtx->m_acceptSocket);
			closesocket(mAcceptIoCtx->m_acceptSocket);
			mAcceptIoCtx->m_acceptSocket = INVALID_SOCKET;
			while (!HasOverlappedIoCompleted(&mAcceptIoCtx->m_Overlapped))
			{
				Sleep(1);
			}
			delete mAcceptIoCtx;
		});
	m_acceptIoCtxList.clear();
	if (m_hExitEvent)
	{
		CloseHandle(m_hExitEvent);
		m_hExitEvent = NULL;
	}
	if (m_hIOCompletionPort)
	{
		CloseHandle(m_hIOCompletionPort);
		m_hIOCompletionPort = NULL;
	}
	if (m_pListenCtx)
	{
		closesocket(m_pListenCtx->m_socket);
		m_pListenCtx->m_socket = INVALID_SOCKET;
		delete m_pListenCtx;
		m_pListenCtx = nullptr;
	}
	removeAllClientCtxs();
	showMessage("Stop() done\n");
	return true;
}

bool IocpServer::Shutdown()
{
	showMessage("Shutdown()");
	m_bIsShutdown = true;
	int ret = CancelIoEx((HANDLE)m_pListenCtx->m_socket, NULL);
	if (0 == ret)
	{
		showMessage("CancelIoEx failed with error: %d", WSAGetLastError());
		return false;
	}
	closesocket(m_pListenCtx->m_socket);
	m_pListenCtx->m_socket = INVALID_SOCKET;

	for_each(m_acceptIoCtxList.begin(), m_acceptIoCtxList.end(),
		[](AcceptIoContext* pAcceptIoCtx)
		{
			int ret = CancelIoEx((HANDLE)pAcceptIoCtx->m_acceptSocket,
				&pAcceptIoCtx->m_Overlapped);
			if (0 == ret)
			{
				printf("CancelIoEx failed with error: %d", WSAGetLastError());
				return; //这个是匿名函数
			}
			closesocket(pAcceptIoCtx->m_acceptSocket);
			pAcceptIoCtx->m_acceptSocket = INVALID_SOCKET;
			while (!HasOverlappedIoCompleted(&pAcceptIoCtx->m_Overlapped))
			{
				Sleep(1);
			}
			delete pAcceptIoCtx;
		});
	m_acceptIoCtxList.clear();
	return true;
}

bool IocpServer::Send(ClientContext* pClientCtx, PBYTE pData, UINT len)
{
	LockGuard lk(&pClientCtx->m_csLock);
	showMessage("Send() pClientCtx=%p len=%d", pClientCtx, len);
	Buffer sendBuf;
	sendBuf.write(pData, len);
	if (0 == pClientCtx->m_outBuf.getBufferLen())
	{
		//第一次投递，++m_nPendingIoCnt
		enterIoLoop(pClientCtx);
		pClientCtx->m_outBuf.copy(sendBuf);
		pClientCtx->m_sendIoCtx->m_wsaBuf.buf = (PCHAR)pClientCtx->m_outBuf.getBuffer();
		pClientCtx->m_sendIoCtx->m_wsaBuf.len = pClientCtx->m_outBuf.getBufferLen();

		PostResult result = postSend(pClientCtx);
		if (PostResult::FAILED == result)
		{
			handleClose(pClientCtx);
			return false;
		}
	}
	else
	{
		pClientCtx->m_outBufQueue.push(sendBuf);
	}
	//int ret = WaitForSingleObject(m_hWriteCompletedEvent, INFINITE);
	//PostQueuedCompletionStatus(m_hComPort, 0, (ULONG_PTR)pClientCtx,
	//	&pClientCtx->m_sendIoCtx->m_overlapped);
	return true;
}

unsigned WINAPI IocpServer::IocpWorkerThread(LPVOID arg)
{
	IocpServer* pThis = static_cast<IocpServer*>(arg);
	LPOVERLAPPED    lpOverlapped = nullptr;
	ULONG_PTR       lpCompletionKey = 0;
	DWORD           dwMilliSeconds = INFINITE;
	DWORD           dwBytesTransferred;
	int             ret;

	pThis->showMessage("IocpWorkerThread() tid=%d", GetCurrentThreadId());
	while (WAIT_OBJECT_0 != WaitForSingleObject(pThis->m_hExitEvent, 0))
	{
		ret = GetQueuedCompletionStatus(pThis->m_hIOCompletionPort, &dwBytesTransferred,
			&lpCompletionKey, &lpOverlapped, dwMilliSeconds);
		pThis->showMessage("IocpWorkerThread() tid=%d, pClientCtx=%p, pIoCtx=%p",
			GetCurrentThreadId(), lpCompletionKey, lpOverlapped);
		if (EXIT_THREAD == lpCompletionKey)
		{
			//退出工作线程
			pThis->showMessage("EXIT_THREAD");
			break;
		}
		// shutdown状态则停止接受连接
		if (pThis->m_bIsShutdown && lpCompletionKey == (ULONG_PTR)pThis)
		{
			continue;
		}

		if (lpCompletionKey != (ULONG_PTR)pThis)
		{
			ClientContext* pClientCtx = (ClientContext*)lpCompletionKey;
			//文档说超时的时候触发，INFINITE不会触发
			//实际上curl命令行ctrl+c强制关闭连接也会触发
			if (0 == ret)
			{
				pThis->showMessage("GetQueuedCompletionStatus failed with error: %d",
					WSAGetLastError());
				pThis->handleClose(pClientCtx);
				continue;
			}
			//对端关闭
			if (0 == dwBytesTransferred)
			{
				pThis->handleClose(pClientCtx);
				continue;
			}
		}

		ClientContext* pClientCtx = (ClientContext*)lpCompletionKey;
		IoContext* pIoCtx = (IoContext*)lpOverlapped;
		switch (pIoCtx->m_PostType)
		{
		case PostType::ACCEPT:
			pThis->handleAccept(lpOverlapped, dwBytesTransferred);
			break;
		case PostType::RECV:
			pThis->handleRecv(pClientCtx, lpOverlapped, dwBytesTransferred);
			break;
		case PostType::SEND:
			pThis->handleSend(pClientCtx, lpOverlapped, dwBytesTransferred);
			break;
		default:
			break;
		}
	}
	pThis->showMessage("IocpWorkerThread() tid=", GetCurrentThreadId(), " exit");
	return 0;
}

bool IocpServer::getAcceptExPtr()
{
	DWORD dwBytes;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	LPFN_ACCEPTEX lpfnAcceptEx = NULL;
	int ret = WSAIoctl(m_pListenCtx->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx, sizeof(GuidAcceptEx),
		&lpfnAcceptEx, sizeof(lpfnAcceptEx),
		&dwBytes, NULL, NULL);
	if (SOCKET_ERROR == ret)
	{
		showMessage("WSAIoctl failed with error: %d", WSAGetLastError());
		closesocket(m_pListenCtx->m_socket);
		m_pListenCtx->m_socket = INVALID_SOCKET;
		return false;
	}
	m_lpfnAcceptEx = lpfnAcceptEx;
	return true;
}

bool IocpServer::getAcceptExSockAddrs()
{
	DWORD dwBytes;
	GUID GuidAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	LPFN_GETACCEPTEXSOCKADDRS pfnGetAcceptExSockAddrs = NULL;
	int ret = WSAIoctl(m_pListenCtx->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAddrs, sizeof(GuidAddrs),
		&pfnGetAcceptExSockAddrs, 
		sizeof(pfnGetAcceptExSockAddrs),
		&dwBytes, NULL, NULL);
	if (SOCKET_ERROR == ret)
	{
		showMessage("WSAIoctl failed with error: %d", WSAGetLastError());
		closesocket(m_pListenCtx->m_socket);
		m_pListenCtx->m_socket = INVALID_SOCKET;
		return false;
	}
	m_lpfnGetAcceptExSockAddrs = pfnGetAcceptExSockAddrs;
	return true;
}

bool IocpServer::setKeepAlive(ClientContext* pClientCtx,
	LPOVERLAPPED lpOverlapped, int time, int interval)
{
	showMessage("setKeepAlive() pClientCtx=%p", pClientCtx);
	if (!Network::setKeepAlive(pClientCtx->m_socket, true))
	{
		return false;
	}
	LPWSAOVERLAPPED pOl = lpOverlapped;
	tcp_keepalive keepAlive;
	keepAlive.onoff = 1;
	keepAlive.keepalivetime = time * 1000;
	keepAlive.keepaliveinterval = interval * 1000;
	DWORD dwBytes;
	//根据msdn这里要传一个OVERLAPPED结构
	int ret = WSAIoctl(pClientCtx->m_socket, SIO_KEEPALIVE_VALS,
		&keepAlive, sizeof(tcp_keepalive), NULL, 0,
		&dwBytes, pOl, NULL);
	if (SOCKET_ERROR == ret && WSA_IO_PENDING != WSAGetLastError())
	{
		showMessage("WSAIoctl failed with error: %d", WSAGetLastError());
		return false;
	}
	return true;
}

bool IocpServer::createListenSocket(short listenPort)
{
	m_pListenCtx = new ListenContext(listenPort);
	showMessage("createListenClient() listenPort=%d pListenCtx=%p, s=%d",
		listenPort, m_pListenCtx, m_pListenCtx->m_socket);
	//创建完成端口
	m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 
		NULL, 0, 0); //NumberOfConcurrentThreads
	if (NULL == m_hIOCompletionPort)
	{
		return false;
	}
	//关联监听socket和完成端口，这里将this指针作为completionKey给完成端口
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenCtx->m_socket,
		m_hIOCompletionPort, (ULONG_PTR)this, 0))
	{
		return false;
	}
	if (SOCKET_ERROR == Network::bind(m_pListenCtx->m_socket, &m_pListenCtx->m_addr))
	{
		showMessage("bind failed");
		return false;
	}
	if (SOCKET_ERROR == Network::listen(m_pListenCtx->m_socket))
	{
		showMessage("listen failed");
		return false;
	}
	//获取acceptEx函数指针
	if (!getAcceptExPtr())
	{
		return false;
	}
	//获取GetAcceptExSockaddrs函数指针
	if (!getAcceptExSockAddrs())
	{
		return false;
	}
	return true;
}

bool IocpServer::createIocpWorker()
{
	showMessage("createIocpWorker() pid=%d, tid=%d",
		GetCurrentProcessId(), GetCurrentThreadId());
	//根据CPU核数创建IO线程
	HANDLE hWorker;
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	for (DWORD i = 0; i < sysInfo.dwNumberOfProcessors; ++i)
	{
		hWorker = (HANDLE)_beginthreadex(NULL, 0, IocpWorkerThread, this, 0, NULL);
		if (NULL == hWorker)
		{
			return false;
		}
		m_hWorkerThreads.emplace_back(hWorker);
		++m_nWorkerCnt;
	}
	showMessage("createIocpWorker() thread count: %d", m_nWorkerCnt);
	return true;
}

bool IocpServer::exitIocpWorker()
{
	showMessage("exitIocpWorker()");
	int ret = 0;
	SetEvent(m_hExitEvent);
	for (int i = 0; i < m_nWorkerCnt; ++i)
	{
		//通知工作线程退出
		ret = PostQueuedCompletionStatus(m_hIOCompletionPort,
			0, EXIT_THREAD, NULL);
		if (FALSE == ret)
		{
			showMessage("PostQueuedCompletionStatus failed with error: %d",
				WSAGetLastError());
		}
	}
	//这里不明白为什么会返回0，不是应该返回m_nWorkerCnt-1吗？
	ret = WaitForMultipleObjects(m_nWorkerCnt,
		m_hWorkerThreads.data(), TRUE, INFINITE);
	return true;
}

bool IocpServer::initAcceptIoContext()
{
	showMessage("initAcceptIoContext()");
	//投递accept请求
	for (int i = 0; i < MAX_POST_ACCEPT; ++i)
	{
		AcceptIoContext* pAcceptIoCtx = new AcceptIoContext();
		m_acceptIoCtxList.emplace_back(pAcceptIoCtx);
		if (!postAccept(pAcceptIoCtx))
		{
			return false;
		}
	}
	return true;
}

bool IocpServer::postAccept(AcceptIoContext* pAcceptIoCtx)
{
	pAcceptIoCtx->ResetBuffer();
	//创建用于接受连接的socket
	pAcceptIoCtx->m_acceptSocket = Network::socket();
	showMessage("postAccept() pAcceptIoCtx=%p s=%d",
		pAcceptIoCtx, pAcceptIoCtx->m_acceptSocket);
	if (SOCKET_ERROR == pAcceptIoCtx->m_acceptSocket)
	{
		showMessage("create socket failed");
		return false;
	}
	/*
	* 使用acceptEx的一个问题：
	* 如果客户端连上却没发送数据，则acceptEx不会触发完成包，则浪费服务器资源
	* 解决方法：为了防止恶意连接，accpetEx不接收用户数据，
	* 	只接收地址（没办法，接口调用必须提供缓冲区）
	*/
	DWORD dwRecvByte;
	LPOVERLAPPED pOverlapped = &pAcceptIoCtx->m_Overlapped;
	LPFN_ACCEPTEX lpfnAcceptEx = (LPFN_ACCEPTEX)m_lpfnAcceptEx;
	constexpr int ACCEPT_ADDRS_SIZE = sizeof(SOCKADDR_IN) + 16;
	if (FALSE == lpfnAcceptEx(m_pListenCtx->m_socket,
		pAcceptIoCtx->m_acceptSocket, pAcceptIoCtx->m_wsaBuf.buf,
		0, ACCEPT_ADDRS_SIZE, ACCEPT_ADDRS_SIZE,
		&dwRecvByte, pOverlapped))
	{
		if (WSA_IO_PENDING != WSAGetLastError())
		{
			showMessage("acceptEx failed");
			return false;
		}
	}
	else
	{
		showMessage("postAccept() FALSE");
		// Accept completed synchronously. We need to marshal 收集
		// the data received over to the worker thread ourselves...
	}
	return true;
}

PostResult IocpServer::postRecv(ClientContext* pClientCtx)
{
	LockGuard lk(&pClientCtx->m_csLock);
	RecvIoContext* pRecvIoCtx = pClientCtx->m_recvIoCtx;
	showMessage("postRecv() pClientCtx=%p, s=%d, pRecvIoCtx=%p",
		pClientCtx, pClientCtx->m_socket, pRecvIoCtx);
	PostResult result = PostResult::SUCCESS;
	pRecvIoCtx->ResetBuffer();
	if (INVALID_SOCKET != pClientCtx->m_socket)
	{
		DWORD dwBytes;
		//设置这个标志，则没收完的数据下一次接收
		DWORD dwFlag = MSG_PARTIAL;
		int ret = WSARecv(pClientCtx->m_socket, &pRecvIoCtx->m_wsaBuf, 1,
			&dwBytes, &dwFlag, &pRecvIoCtx->m_Overlapped, NULL);
		if (SOCKET_ERROR == ret && WSA_IO_PENDING != WSAGetLastError())
		{
			showMessage("WSARecv failed with error: %d", WSAGetLastError());
			result = PostResult::FAILED;
		}
	}
	else
	{
		result = PostResult::INVALID;
	}
	return result;
}

PostResult IocpServer::postSend(ClientContext* pClientCtx)
{
	LockGuard lk(&pClientCtx->m_csLock);
	SendIoContext* pSendIoCtx = pClientCtx->m_sendIoCtx;
	showMessage("postSend() pClientCtx=%p, s=%d, pSendIoCtx=%p",
		pClientCtx, pClientCtx->m_socket, pSendIoCtx);
	PostResult result = PostResult::SUCCESS;
	if (INVALID_SOCKET != pClientCtx->m_socket)
	{
		DWORD dwBytesSent;
		DWORD dwFlag = MSG_PARTIAL;
		int ret = WSASend(pClientCtx->m_socket, &pSendIoCtx->m_wsaBuf, 1,
			&dwBytesSent, dwFlag, &pSendIoCtx->m_Overlapped, NULL);
		if (SOCKET_ERROR == ret && WSA_IO_PENDING != WSAGetLastError())
		{
			showMessage("WSASend failed with error: ", WSAGetLastError());
			result = PostResult::FAILED;
		}
	}
	else
	{
		result = PostResult::INVALID;
	}
	return result;
}

bool IocpServer::handleAccept(LPOVERLAPPED lpOverlapped, DWORD dwBytesTransferred)
{
	AcceptIoContext* pAcceptIoCtx = (AcceptIoContext*)lpOverlapped;
	showMessage("handleAccept() pAcceptIoCtx=%p, s=%d",
		pAcceptIoCtx, pAcceptIoCtx->m_acceptSocket);
	//达到最大连接数则关闭新的socket
	if (m_nConnClientCnt + 1 >= m_nMaxConnClientCnt)
	{//WSAECONNABORTED=(10053)//Software caused connection abort.
		//WSAECONNRESET=(10054)//Connection reset by peer.
		closesocket(pAcceptIoCtx->m_acceptSocket);
		pAcceptIoCtx->m_acceptSocket = INVALID_SOCKET;
		postAccept(pAcceptIoCtx);
		return true;
	}
	InterlockedIncrement(&m_nConnClientCnt);
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	int remoteLen = 0, localLen = 0; //必须+16,参见MSDN
	this->m_lpfnGetAcceptExSockAddrs(pAcceptIoCtx->m_wsaBuf.buf,
		0, //pIoContext->m_wsaBuf.len - (dwAddrLen * 2),
		dwAddrLen, dwAddrLen, (LPSOCKADDR*)&localAddr,
		&localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);
	Network::updateAcceptContext(m_pListenCtx->m_socket,
		pAcceptIoCtx->m_acceptSocket);
	//创建新的ClientContext，原来的IoContext要用来接收新的连接
	ClientContext* pClientCtx = allocateClientCtx(pAcceptIoCtx->m_acceptSocket);
	//SOCKADDR_IN sockaddr = Network::getpeername(pClientCtx->m_socket);
	pClientCtx->m_addr = *(SOCKADDR_IN*)&clientAddr;
	//先投递，避免下面绑定失败，还没有投递，会导致投递数减少
	postAccept(pAcceptIoCtx); //投递一个新的accpet请求
	if (NULL == CreateIoCompletionPort((HANDLE)pClientCtx->m_socket,
		m_hIOCompletionPort, (ULONG_PTR)pClientCtx, 0))
	{
		InterlockedDecrement(&m_nConnClientCnt);
		return false;
	}
	enterIoLoop(pClientCtx);
	//开启心跳机制
	//setKeepAlive(pClientCtx, &pAcceptIoCtx->m_overlapped);
	//pClientCtx->appendToBuffer((PBYTE)pBuf, dwBytesTransferred);
	notifyNewConnection(pClientCtx);
	//notifyPackageReceived(pClientCtx);
	//将客户端加入连接列表
	addClientCtx(pClientCtx);
	//投递recv请求,这里invalid socket是否要关闭客户端？
	PostResult result = postRecv(pClientCtx);
	if (PostResult::FAILED == result
		|| PostResult::INVALID == result)
	{
		handleClose(pClientCtx);
	}
	return true;
}

bool IocpServer::handleRecv(ClientContext* pClientCtx,
	LPOVERLAPPED lpOverlapped, DWORD dwBytesTransferred)
{
	showMessage("handleRecv() pClientCtx=%p, s=%d, pRecvIoCtx=%p",
		pClientCtx, pClientCtx->m_socket, lpOverlapped);
	RecvIoContext* pRecvIoCtx = (RecvIoContext*)lpOverlapped;
	pClientCtx->appendToBuffer(pRecvIoCtx->m_recvBuf, dwBytesTransferred);
	notifyPackageReceived(pClientCtx);

	//投递recv请求
	PostResult result = postRecv(pClientCtx);
	if (PostResult::FAILED == result
		|| PostResult::INVALID == result)
	{
		handleClose(pClientCtx);
	}
	return true;
}

bool IocpServer::handleSend(ClientContext* pClientCtx,
	LPOVERLAPPED lpOverlapped, DWORD dwBytesTransferred)
{
	LockGuard lk(&pClientCtx->m_csLock);
	SendIoContext* pSendIoCtx = (SendIoContext*)lpOverlapped;
	showMessage("handleSend() pClientCtx=%p, s=%d, pSendIoCtx=%p",
		pClientCtx, pClientCtx->m_socket, pSendIoCtx);
	pClientCtx->m_outBuf.remove(dwBytesTransferred);
	if (0 == pClientCtx->m_outBuf.getBufferLen())
	{
		notifyWriteCompleted();
		pClientCtx->m_outBuf.clear();

		if (!pClientCtx->m_outBufQueue.empty())
		{
			pClientCtx->m_outBuf.copy(pClientCtx->m_outBufQueue.front());
			pClientCtx->m_outBufQueue.pop();
		}
		else
		{
			//发送完毕，不能关socket
			//handleClose(pClientCtx);
			releaseClientCtx(pClientCtx);
		}
	}
	if (0 != pClientCtx->m_outBuf.getBufferLen())
	{
		pSendIoCtx->m_wsaBuf.buf = (PCHAR)pClientCtx->m_outBuf.getBuffer();
		pSendIoCtx->m_wsaBuf.len = pClientCtx->m_outBuf.getBufferLen();

		PostResult result = postSend(pClientCtx);
		if (PostResult::FAILED == result)
		{
			handleClose(pClientCtx);
		}
	}
	return false;
}

bool IocpServer::handleClose(ClientContext* pClientCtx)
{
	showMessage("handleClose() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	closeClientSocket(pClientCtx);
	releaseClientCtx(pClientCtx);
	return true;
}

void IocpServer::enterIoLoop(ClientContext* pClientCtx)
{
	InterlockedIncrement(&pClientCtx->m_nPendingIoCnt);
}

int IocpServer::exitIoLoop(ClientContext* pClientCtx)
{
	return InterlockedDecrement(&pClientCtx->m_nPendingIoCnt);
}

void IocpServer::closeClientSocket(ClientContext* pClientCtx)
{
	showMessage("closeClientSocket() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	SOCKET s;
	Addr peerAddr;
	{
		LockGuard lk(&pClientCtx->m_csLock);
		s = pClientCtx->m_socket;
		peerAddr = pClientCtx->m_addr;
		pClientCtx->m_socket = INVALID_SOCKET;
	}
	if (INVALID_SOCKET != s)
	{
		notifyDisconnected(s, peerAddr);
		if (!Network::setLinger(s))
		{
			return;
		}
		int ret = CancelIoEx((HANDLE)s, NULL);
		//ERROR_NOT_FOUND : cannot find a request to cancel
		if (0 == ret && ERROR_NOT_FOUND != WSAGetLastError())
		{
			showMessage("CancelIoEx failed with error: %d",
				WSAGetLastError());
			return;
		}

		closesocket(s);
		InterlockedDecrement(&m_nConnClientCnt);
	}
}

void IocpServer::addClientCtx(ClientContext* pClientCtx)
{
	showMessage("addClientCtx() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	LockGuard lk(&m_csClientList);
	m_connectedClientList.emplace_back(pClientCtx);
}

void IocpServer::removeClientCtx(ClientContext* pClientCtx)
{
	showMessage("removeClientCtx() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	LockGuard lk(&m_csClientList);
	{
		auto it = std::find(m_connectedClientList.begin(),
			m_connectedClientList.end(), pClientCtx);
		if (m_connectedClientList.end() != it)
		{
			m_connectedClientList.remove(pClientCtx);
			while (!pClientCtx->m_outBufQueue.empty())
			{
				pClientCtx->m_outBufQueue.pop();
			}
			pClientCtx->m_nPendingIoCnt = 0;
			m_freeClientList.emplace_back(pClientCtx);
		}
	}
}

void IocpServer::removeAllClientCtxs()
{
	showMessage("removeAllClientCtxs()");
	LockGuard lk(&m_csClientList);
	m_connectedClientList.erase(m_connectedClientList.begin(),
		m_connectedClientList.end());
}

ClientContext* IocpServer::allocateClientCtx(SOCKET s)
{
	showMessage("allocateClientCtx() s=%d", s);
	ClientContext* pClientCtx = nullptr;
	LockGuard lk(&m_csClientList);
	if (m_freeClientList.empty())
	{
		pClientCtx = new ClientContext(s);
	}
	else
	{
		pClientCtx = m_freeClientList.front();
		m_freeClientList.pop_front();
		pClientCtx->m_nPendingIoCnt = 0;
		pClientCtx->m_socket = s;
	}
	pClientCtx->reset();
	return pClientCtx;
}

void IocpServer::releaseClientCtx(ClientContext* pClientCtx)
{
	showMessage("releaseClientCtx() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	if (exitIoLoop(pClientCtx) <= 0)
	{
		removeClientCtx(pClientCtx);
		//这里不删除，而是将ClientContext移到空闲链表
		//delete pClientCtx;
	}
}

void IocpServer::echo(ClientContext* pClientCtx)
{
	showMessage("echo() pClientCtx=%p", pClientCtx);
	Send(pClientCtx, pClientCtx->m_inBuf.getBuffer(),
		pClientCtx->m_inBuf.getBufferLen());
	pClientCtx->m_inBuf.remove(pClientCtx->m_inBuf.getBufferLen());
}

void IocpServer::notifyNewConnection(ClientContext* pClientCtx)
{
	printf("m_nConnClientCnt=%d\n", m_nConnClientCnt);
	showMessage("notifyNewConnection() pClientCtx=%p, %s, s=%d",
		pClientCtx->m_addr.toString().c_str(),
		pClientCtx, pClientCtx->m_socket);
}

void IocpServer::notifyDisconnected(SOCKET s, Addr addr)
{
	showMessage("notifyDisconnected() s=%d, %s", s, addr.toString().c_str());
}

void IocpServer::notifyPackageReceived(ClientContext* pClientCtx)
{
	showMessage("notifyPackageReceived() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	echo(pClientCtx);
}

void IocpServer::notifyWritePackage()
{
	showMessage("notifyWritePackage()");
}

void IocpServer::notifyWriteCompleted()
{
	showMessage("notifyWriteCompleted()");
}

void print_time()
{
	SYSTEMTIME sysTime = { 0 };
	GetLocalTime(&sysTime);
	printf("%4d-%02d-%02d %02d:%02d:%02d.%03d：",
		sysTime.wYear, sysTime.wMonth, sysTime.wDay,
		sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
		sysTime.wMilliseconds);
}

void IocpServer::showMessage(const char* szFormat, ...)
{
	//printf(".");
	return;
	__try
	{
		EnterCriticalSection(&m_csLog);
		print_time();
		// 处理变长参数
		va_list arglist;
		va_start(arglist, szFormat);
		vprintf(szFormat, arglist);
		va_end(arglist);
		printf("\n");
		return;
	}
	__finally
	{
		::LeaveCriticalSection(&m_csLog);
	}
}