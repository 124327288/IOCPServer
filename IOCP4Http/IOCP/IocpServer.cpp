#include "Network.h"
#include "LockGuard.h"
#include "PerIoContext.h"
#include "PerSocketContext.h"
#include "IocpServer.h"
//for struct tcp_keepalive
#include <mstcpip.h>

///////////////////////////////////////////////////////////////////
// 工作者线程： 为IOCP请求服务的工作者线程
// 也就是每当完成端口上出现了完成数据包，就将之取出来进行处理的线程
///////////////////////////////////////////////////////////////////
/*********************************************************************
*函数功能：线程函数，根据GetQueuedCompletionStatus返回情况进行处理；
*函数参数：lpParam是THREADPARAMS_WORKER类型指针；
*函数说明：GetQueuedCompletionStatus正确返回时表示某操作已经完成，
	第二个参数lpNumberOfBytes表示本次套接字传输的字节数，
参数lpCompletionKey和lpOverlapped包含重要的信息，请查询MSDN文档；
*********************************************************************/
DWORD WINAPI IocpServer::iocpWorkerThread(LPVOID lpParam)
{
	IocpServer* pThis = (IocpServer*)lpParam;
	pThis->showMessage("工作者线程，ID:%d", GetCurrentThreadId());
	//循环处理请求，直到接收到Shutdown信息为止
	while (WAIT_OBJECT_0 != WaitForSingleObject(pThis->m_hExitEvent, 0))
	{
		DWORD dwBytesTransferred = 0;
		OVERLAPPED* pOverlapped = nullptr;
		SocketContext* pSoContext = nullptr;
		const BOOL bRet = GetQueuedCompletionStatus(pThis->m_hIOCP,
			&dwBytesTransferred, (PULONG_PTR)&pSoContext, &pOverlapped, INFINITE);
		pThis->showMessage("WorkerThread() tid=%d, pClientCtx=%p, pIoCtx=%p",
			GetCurrentThreadId(), pSoContext, pOverlapped);
		IoContext* pIoContext = CONTAINING_RECORD(pOverlapped,
			IoContext, m_Overlapped); // 读取传入的参数
		//接收EXIT_CODE退出标志，则直接退出
		if (EXIT_THREAD == (DWORD)pSoContext)
		{// 退出工作线程
			pThis->showMessage("EXIT_THREAD");
			break;
		}
		// shutdown状态则停止接受连接
		if (pThis->m_bIsShutdown && pIoContext->m_PostType == PostType::ACCEPT)
		{//???有啥用???
			break; //continue;
		}

		if (pIoContext->m_PostType != PostType::ACCEPT)
		{
			//文档说超时的时候触发，INFINITE不会触发
			//实际上curl命令行ctrl+c强制关闭连接也会触发
			if (!bRet)
			{
				pThis->showMessage("GetQueuedCompletionStatus error=%d",
					WSAGetLastError());
				pThis->handleClose((ClientContext*)pSoContext);
				continue;
			}
			//对端关闭
			if (0 == dwBytesTransferred)
			{
				pThis->handleClose((ClientContext*)pSoContext);
				continue;
			}
		}

		switch (pIoContext->m_PostType)
		{
		case PostType::ACCEPT:
			pThis->handleAccept((ListenContext*)pSoContext,
				pIoContext, dwBytesTransferred);
			break;
		case PostType::RECV:
			pThis->handleRecv((ClientContext*)pSoContext, 
				pIoContext, dwBytesTransferred);

			break;
		case PostType::SEND:
			pThis->handleSend((ClientContext*)pSoContext,
				pIoContext, dwBytesTransferred);
			break;
		default: // 不应该执行到这里
			pThis->showMessage("WorkThread中的m_PostType异常");
			break;
		} //switch
	}//while
	pThis->showMessage("工作者线程退出，ID:%d",
		GetCurrentThreadId());
	return 0;
}

IocpServer::IocpServer(short listenPort, int maxConnCount) :
	m_bIsShutdown(false), m_listenPort(listenPort)
	, m_nMaxConnClientCnt(maxConnCount)
	, m_hExitEvent(INVALID_HANDLE_VALUE)
	, m_hIOCP(INVALID_HANDLE_VALUE)
	, m_nWorkerCnt(0)
	, m_nConnClientCnt(0)
	, m_pListenCtx(nullptr)
	, m_lpfnGetAcceptExSockAddrs(nullptr)
	, m_lpfnAcceptEx(nullptr)
	, acceptPostCount(0)
{
	errorCount = 0;
	// 初始化线程互斥量
	InitializeCriticalSection(&m_csLog);
	InitializeCriticalSection(&m_csClientList);
	if (!Network::init())
	{
		showMessage("初始化WinSock 2.2失败！");
	}
	// 建立系统退出的事件通知，初始状态为nonsignaled
	m_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (WSA_INVALID_EVENT == m_hExitEvent)
	{
		showMessage("CreateEvent failed err=%d", WSAGetLastError());
	}
	showMessage("IocpServer() listenPort=%d", listenPort);
}

IocpServer::~IocpServer()
{
	// 确保资源彻底释放
	this->Stop();
	Network::deinit();
	showMessage("~IocpServer()");
	// 删除客户端列表的互斥量
	DeleteCriticalSection(&m_csLog);
	DeleteCriticalSection(&m_csClientList);
}


//函数功能：启动服务器
bool IocpServer::Start()
{
	showMessage("IocpServer::Start()");
	// 初始化Socket
	if (!initSocket(m_listenPort))
	{
		this->showMessage("监听Socket初始化失败！");
		this->deinitialize();
		return false;
	}
	else
	{
		this->showMessage("监听Socket初始化完毕");
	}
	// 初始化IOCP
	if (!initIOCP(m_pListenCtx))
	{
		this->showMessage("初始化IOCP失败！");
		this->deinitialize();
		return false;
	}
	else
	{
		this->showMessage("初始化IOCP完毕！");
	}
	if (!initIocpWorker())
	{
		this->deinitialize();
		return false;
	}
	this->showMessage("系统准备就绪，等候连接...");
	return true;
}

/////////////////////////////////////////////////////////////////
// 初始化Socket
bool IocpServer::initSocket(short listenPort)
{
	this->showMessage("初始化Socket()");
	// 生成用于监听的Socket，ListenContext内部创建
	m_pListenCtx = new ListenContext(listenPort);

	// 绑定Socket地址和端口
	if (SOCKET_ERROR == Network::bind(m_pListenCtx->m_socket,
		m_pListenCtx->m_addr.GetAddr()))
	{
		this->showMessage("bind()函数执行错误");
		return false;
	}
	else
	{
		this->showMessage("bind() 完成");
	}

	// 开始进行监听
	if (SOCKET_ERROR == Network::listen(m_pListenCtx->m_socket))
	{
		this->showMessage("listen()出错, err=%d", WSAGetLastError());
		return false;
	}
	else
	{
		this->showMessage("listen() 完成");
	}

	// 使用AcceptEx函数，因为这个是属于WinSock2规范之外的
	// 所以需要额外获取一下函数的指针，获取AcceptEx函数指针
	DWORD dwBytes = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	if (SOCKET_ERROR == WSAIoctl(m_pListenCtx->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx), &dwBytes, NULL, NULL))
	{
		this->showMessage("获取AcceptEx失败。err=%d", WSAGetLastError());
		return false;
	}

	// 获取GetAcceptExSockAddrs函数指针，也是同理
	GUID GuidAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(m_pListenCtx->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAddrs,
		sizeof(GuidAddrs), &m_lpfnGetAcceptExSockAddrs,
		sizeof(m_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		this->showMessage("获取AcceptExAddr失败。err=%d", WSAGetLastError());
		return false;
	}
	return true;
}

////////////////////////////////
// 初始化完成端口
bool IocpServer::initIOCP(ListenContext* pListenCtx)
{
	this->showMessage("初始化IOCP()");
	//If this parameter is zero, the system allows as many 
	//concurrently running threads as there are processors in the system.
	//如果此参数为零，则系统允许的并发运行线程数量与系统中的处理器数量相同。
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
		nullptr, 0, 0); //NumberOfConcurrentThreads
	if (nullptr == m_hIOCP)
	{
		this->showMessage("建立IOCP失败！err=%d!", WSAGetLastError());
		return false;
	}

	// 将Listen Socket绑定至完成端口中
	if (NULL == CreateIoCompletionPort((HANDLE)pListenCtx->m_socket,
		m_hIOCP, (DWORD)pListenCtx, 0)) //CompletionKey
	{
		this->showMessage("绑定IOCP失败！err=%d", WSAGetLastError());
		return false;
	}
	else
	{
		this->showMessage("绑定IOCP完成");
	}

	// 为AcceptEx 准备参数，然后投递AcceptEx I/O请求
	// 创建10个套接字，投递AcceptEx请求，即共有10个套接字进行accept操作；
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		AcceptIoContext* pAcceptIoCtx = new AcceptIoContext();
		pListenCtx->m_acceptIoCtxList.emplace_back(pAcceptIoCtx);
		if (!postAccept(pListenCtx, pAcceptIoCtx))
		{
			return false;
		}
	}
	this->showMessage("投递 %d 个AcceptEx请求完毕", MAX_POST_ACCEPT);
	return true;
}

bool IocpServer::initIocpWorker()
{
	this->showMessage("初始化WorkerThread(),Pid=%d, Tid=%d",
		GetCurrentProcessId(),GetCurrentThreadId());
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	// 根据本机中的处理器数量，建立对应的线程数
	int Count = sysInfo.dwNumberOfProcessors;
	Count *= WORKER_THREADS_PER_PROCESSOR;
	// 根据计算出来的数量建立工作者线程
	for (int i = 0; i < Count; i++)
	{
		HANDLE hWorker = ::CreateThread(0, 0, iocpWorkerThread,
			(void*)this, 0, NULL);
		if (NULL == hWorker)
		{
			return false;
		}
		m_hWorkerThreads.emplace_back(hWorker);
		++m_nWorkerCnt;
	}
	this->showMessage("建立WorkerThread %d 个", m_nWorkerCnt);
	return true;
}

////////////////////////////////////////////////////////////
//	最后释放掉所有资源
void IocpServer::deinitialize()
{
	//关闭工作线程句柄
	std::vector<HANDLE>::iterator it;
	for (it = m_hWorkerThreads.begin();
		it != m_hWorkerThreads.end(); it++)
	{
		RELEASE_HANDLE(*it);
	}
	m_hWorkerThreads.clear();
	// 关闭系统退出事件句柄
	RELEASE_HANDLE(m_hExitEvent);
	// 关闭IOCP句柄
	RELEASE_HANDLE(m_hIOCP);
	// 关闭监听Socket
	RELEASE_POINTER(m_pListenCtx);
	this->showMessage("释放资源完毕");
}

////////////////////////////////////////////////////////////////////
//	开始发送系统退出消息，退出完成端口和线程资源
bool IocpServer::Stop()
{
	showMessage("Stop()");
	//同步等待所有工作线程退出
	exitIocpWorker();
	// 清除客户端列表信息
	removeAllClientCtxs();
	// 释放其他资源
	this->deinitialize();
	this->showMessage("停止监听");
	return true;
}

bool IocpServer::exitIocpWorker()
{
	showMessage("exitIocpWorker()");
	if (m_hExitEvent != INVALID_HANDLE_VALUE)
	{
		BOOL bRet = SetEvent(m_hExitEvent);
	}
	for (int i = 0; i < m_nWorkerCnt; ++i)
	{
		BOOL bRet = PostQueuedCompletionStatus(m_hIOCP,
			0, EXIT_THREAD, NULL); //通知工作线程退出
		if (!bRet)
		{
			showMessage("PostQueuedCompletionStatus err=%d",
				WSAGetLastError());
		}
	}
	//这里不明白为什么会返回0，不是应该返回m_nWorkerCnt-1吗？
	DWORD dwRet = WaitForMultipleObjects(m_nWorkerCnt,
		m_hWorkerThreads.data(), TRUE, INFINITE);
	return true;
}

bool IocpServer::SendData(ClientContext* pClientCtx, PBYTE pData, UINT len)
{
	LockGuard lk(&pClientCtx->m_csLock);
	showMessage("Send() pClientCtx=%p len=%d", pClientCtx, len);
	Buffer sendBuf;
	sendBuf.write(pData, len);
	if (0 == pClientCtx->m_outBuf.getBufferLen())
	{
		//第一次投递，++m_nPendingIoCnt
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

void IocpServer::enterIoLoop(SocketContext* pSocketCtx)
{
	InterlockedIncrement(&pSocketCtx->m_nPendingIoCnt);
}

int IocpServer::exitIoLoop(SocketContext* pSocketCtx)
{
	return InterlockedDecrement(&pSocketCtx->m_nPendingIoCnt);
}

#if 0
bool IocpServer::clearAcceptIoContext(ListenContext* pListenContext)
{
	showMessage("clearAcceptIoContext()");
	std::vector<AcceptIoContext*>::iterator it;
	for (it = pListenContext->m_acceptIoCtxList.begin();
		it != pListenContext->m_acceptIoCtxList.end(); it++)
	{
		AcceptIoContext* pAcceptIoCtx = *it;
		int bRet = CancelIoEx((HANDLE)pAcceptIoCtx->m_acceptSocket,
			&pAcceptIoCtx->m_Overlapped);
		//int bRet = CancelIo((HANDLE)pAcceptIoCtx->m_acceptSocket);
		if (!bRet)
		{
			printf("CancelIoEx failed with error: %d", WSAGetLastError());
			//continue; // return; //这个是匿名函数
		}
		closesocket(pAcceptIoCtx->m_acceptSocket);
		pAcceptIoCtx->m_acceptSocket = INVALID_SOCKET;
		while (!HasOverlappedIoCompleted(&pAcceptIoCtx->m_Overlapped))
		{
			Sleep(1);
		}
		delete pAcceptIoCtx;
	}
	pListenContext->m_acceptIoCtxList.clear();
	return true;
}
#endif 

bool IocpServer::postAccept(ListenContext* pListenCtx,
	AcceptIoContext* pAcceptIoCtx)
{
	//enterIoLoop(pClientCtx);
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
	DWORD dwRecvByte = 0;
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
		return false;
	}
	//投递成功，计数器增加
	enterIoLoop(pListenCtx);
	return true;
}

PostResult IocpServer::postRecv(ClientContext* pClientCtx)
{
	//enterIoLoop(pClientCtx);
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
		else
		{	//投递成功，计数器增加
			enterIoLoop(pClientCtx);
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
	//enterIoLoop(pClientCtx);
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
		else
		{	//投递成功，计数器增加
			enterIoLoop(pClientCtx);
		}
	}
	else
	{
		result = PostResult::INVALID;
	}
	return result;
}

bool IocpServer::handleError(ClientContext* pClientCtx, const DWORD& dwErr)
{
	return false;
}

bool IocpServer::handleAccept(ListenContext* pListenCtx,
	IoContext* pIoCtx, DWORD dwBytesTransferred)
{
	exitIoLoop(pListenCtx);
	AcceptIoContext* pAcceptIoCtx = (AcceptIoContext*)pIoCtx;
	showMessage("handleAccept() pAcceptIoCtx=%p, s=%d",
		pAcceptIoCtx, pAcceptIoCtx->m_acceptSocket);
	//达到最大连接数则关闭新的socket
	if (m_nConnClientCnt + 1 >= m_nMaxConnClientCnt)
	{//WSAECONNABORTED=(10053)//Software caused connection abort.
		//WSAECONNRESET=(10054)//Connection reset by peer.
		closesocket(pAcceptIoCtx->m_acceptSocket);
		pAcceptIoCtx->m_acceptSocket = INVALID_SOCKET;
		postAccept(pListenCtx, pAcceptIoCtx);
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
	pClientCtx->m_addr = *(SOCKADDR_IN*)clientAddr;
	//先投递，避免下面绑定失败，还没有投递，会导致投递数减少
	postAccept(pListenCtx, pAcceptIoCtx); //投递一个新的accpet请求
	if (NULL == CreateIoCompletionPort((HANDLE)pClientCtx->m_socket,
		m_hIOCP, (ULONG_PTR)pClientCtx, 0)) //CompletionKey
	{
		InterlockedDecrement(&m_nConnClientCnt);
		return false;
	}
	//开启心跳机制
	//setKeepAlive(pClientCtx, &pAcceptIoCtx->m_overlapped);
	//pClientCtx->appendToBuffer((PBYTE)pBuf, dwBytesTransferred);
	OnConnectionAccepted(pClientCtx);
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
	IoContext* pIoCtx, DWORD dwBytesTransferred)
{
	exitIoLoop(pClientCtx);
	showMessage("handleRecv() pClientCtx=%p, s=%d, pRecvIoCtx=%p",
		pClientCtx, pClientCtx->m_socket, pIoCtx);
	RecvIoContext* pRecvIoCtx = (RecvIoContext*)pIoCtx;
	pClientCtx->appendToBuffer(pRecvIoCtx->m_recvBuf, dwBytesTransferred);
	OnRecvCompleted(pClientCtx);

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
	IoContext* pIoCtx, DWORD dwBytesTransferred)
{
	exitIoLoop(pClientCtx);
	LockGuard lk(&pClientCtx->m_csLock);
	SendIoContext* pSendIoCtx = (SendIoContext*)pIoCtx;
	showMessage("handleSend() pClientCtx=%p, s=%d, pSendIoCtx=%p",
		pClientCtx, pClientCtx->m_socket, pSendIoCtx);
	pClientCtx->m_outBuf.remove(dwBytesTransferred);
	if (0 == pClientCtx->m_outBuf.getBufferLen())
	{
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
			OnSendCompleted(pClientCtx);
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
		OnConnectionClosed(s, peerAddr);
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
	ASSERT(pClientCtx->m_nPendingIoCnt <= 0);
	{
		removeClientCtx(pClientCtx);
		//这里不删除，而是将ClientContext移到空闲链表
		//delete pClientCtx;
	}
}

void IocpServer::OnConnectionAccepted(ClientContext* pClientCtx)
{
	//printf("m_nConnClientCnt=%d\n", m_nConnClientCnt);
	showMessage("OnConnectionAccepted() pClientCtx=%p, s=%d, %s",
		pClientCtx, pClientCtx->m_socket,
		pClientCtx->m_addr.toString().c_str());
}

void IocpServer::OnConnectionClosed(SOCKET s, Addr addr)
{
	showMessage("OnConnectionClosed() s=%d, %s", s, addr.toString().c_str());
}

void IocpServer::OnConnectionError(ClientContext* pClientCtx, int error)
{
	showMessage("OnConnectionError() pClientCtx=%p, s=%d, error=%d", 
		pClientCtx, pClientCtx->m_socket, error);
}

void IocpServer::OnRecvCompleted(ClientContext* pClientCtx)
{
	showMessage("OnRecvCompleted() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	echo(pClientCtx);
}

void IocpServer::OnSendCompleted(ClientContext* pClientCtx)
{
	showMessage("OnSendCompleted() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
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

/////////////////////////////////////////////////////////////////////
// 判断客户端Socket是否已经断开，否则在一个无效的Socket上投递WSARecv操作会出现异常
// 使用的方法是尝试向这个socket发送数据，判断这个socket调用的返回值
// 因为如果客户端网络异常断开(例如客户端崩溃或者拔掉网线等)的时候，
// 服务器端是无法收到客户端断开的通知的
bool IocpServer::isSocketAlive(SOCKET s) noexcept
{
	const int nByteSent = send(s, "", 0, 0);
	if (SOCKET_ERROR == nByteSent)
	{
		return false;
	}
	else
	{
		return true;
	}
}

void IocpServer::echo(ClientContext* pClientCtx)
{
	showMessage("echo() pClientCtx=%p", pClientCtx);
	SendData(pClientCtx, pClientCtx->m_inBuf.getBuffer(),
		pClientCtx->m_inBuf.getBufferLen());
	pClientCtx->m_inBuf.remove(pClientCtx->m_inBuf.getBufferLen());
}

void print_datetime()
{
	SYSTEMTIME sysTime = { 0 };
	GetLocalTime(&sysTime);
	printf("%4d-%02d-%02d %02d:%02d:%02d.%03d：",
		sysTime.wYear, sysTime.wMonth, sysTime.wDay,
		sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
		sysTime.wMilliseconds);
}

/////////////////////////////////////////////////////////////////////
// 在主界面中显示提示信息
void IocpServer::showMessage(const char* szFormat, ...)
{
	//printf(".");
	//return;
	__try
	{
		EnterCriticalSection(&m_csLog);
		print_datetime();
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