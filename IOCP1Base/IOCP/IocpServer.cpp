#include "Network.h"
#include "LockGuard.h"
#include "PerIoContext.h"
#include "PerSocketContext.h"
#include "IocpServer.h"

///////////////////////////////////////////////////////////////////
// �������̣߳� ΪIOCP�������Ĺ������߳�
// Ҳ����ÿ����ɶ˿��ϳ�����������ݰ����ͽ�֮ȡ�������д�����߳�
///////////////////////////////////////////////////////////////////
/*********************************************************************
*�������ܣ��̺߳���������GetQueuedCompletionStatus����������д���
*����������lpParam��THREADPARAMS_WORKER����ָ�룻
*����˵����GetQueuedCompletionStatus��ȷ����ʱ��ʾĳ�����Ѿ���ɣ�
	�ڶ�������lpNumberOfBytes��ʾ�����׽��ִ�����ֽ�����
����lpCompletionKey��lpOverlapped������Ҫ����Ϣ�����ѯMSDN�ĵ���
*********************************************************************/
DWORD WINAPI IocpServer::iocpWorkerThread(LPVOID lpParam)
{
	IocpServer* pThis = (IocpServer*)lpParam;
	pThis->showMessage("�������̣߳�ID:%d", GetCurrentThreadId());
	//ѭ����������ֱ�����յ�Shutdown��ϢΪֹ
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
			IoContext, m_Overlapped); // ��ȡ����Ĳ���
		//����EXIT_CODE�˳���־����ֱ���˳�
		if (EXIT_THREAD == (DWORD)pSoContext)
		{// �˳������߳�
			pThis->showMessage("EXIT_THREAD");
			break;
		}
		if (pIoContext->m_PostType == PostType::ACCEPT)
		{//����socket����Ĵ���
			// shutdown״̬��ֹͣ��������
			if (pThis->m_bIsShutdown)
			{//�Ѿ��رգ��Ͳ���accept��
				continue;
			}
		}
		else
		{//�շ����ݵĴ���
			if (!bRet)
			{//�д���
				int nErr = GetLastError();
				pThis->handleError((ClientContext*)pSoContext, nErr);				
				continue; 
			}
			// �ж��Ƿ��пͻ��˶Ͽ��� //�Զ˹ر�
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
		default: // ��Ӧ��ִ�е�����
			pThis->showMessage("WorkThread�е�m_PostType�쳣");
			break;
		} //switch
	}//while
	pThis->showMessage("�������߳��˳���ID:%d",
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
{
	// ��ʼ���̻߳�����
	InitializeCriticalSection(&m_csLog);
	InitializeCriticalSection(&m_csClientList);
	if (!Network::init())
	{
		showMessage("��ʼ��WinSock 2.2ʧ�ܣ�");
	}
	// ����ϵͳ�˳����¼�֪ͨ����ʼ״̬Ϊnonsignaled
	m_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (WSA_INVALID_EVENT == m_hExitEvent)
	{
		showMessage("CreateEvent failed err=%d", WSAGetLastError());
	}
	showMessage("IocpServer() listenPort=%d", listenPort);
}

IocpServer::~IocpServer()
{
	// ȷ����Դ�����ͷ�
	Stop();
	Network::deinit();
	showMessage("~IocpServer()");
	// ɾ���ͻ����б�Ļ�����
	DeleteCriticalSection(&m_csLog);
	DeleteCriticalSection(&m_csClientList);
}


//�������ܣ�����������
bool IocpServer::Start()
{
	showMessage("IocpServer::Start()");
	// ��ʼ��Socket
	if (!initSocket(m_listenPort))
	{
		showMessage("����Socket��ʼ��ʧ�ܣ�");
		deinitialize();
		return false;
	}
	else
	{
		showMessage("����Socket��ʼ�����");
	}
	// ��ʼ��IOCP
	if (!initIOCP(m_pListenCtx))
	{
		showMessage("��ʼ��IOCPʧ�ܣ�");
		deinitialize();
		return false;
	}
	else
	{
		showMessage("��ʼ��IOCP��ϣ�");
	}
	if (!initIocpWorker())
	{
		deinitialize();
		return false;
	}
	showMessage("ϵͳ׼���������Ⱥ�����...");
	return true;
}

/////////////////////////////////////////////////////////////////
// ��ʼ��Socket
bool IocpServer::initSocket(short listenPort)
{
	showMessage("��ʼ��Socket()");
	// �������ڼ�����Socket��ListenContext�ڲ�����
	m_pListenCtx = new ListenContext(listenPort);

	// ��Socket��ַ�Ͷ˿�
	if (SOCKET_ERROR == Network::bind(m_pListenCtx->m_socket,
		m_pListenCtx->m_addr.GetAddr()))
	{
		showMessage("bind()����ִ�д���");
		return false;
	}
	else
	{
		showMessage("bind() ���");
	}

	// ��ʼ���м���
	if (SOCKET_ERROR == Network::listen(m_pListenCtx->m_socket))
	{
		showMessage("listen()����, err=%d", WSAGetLastError());
		return false;
	}
	else
	{
		showMessage("listen() ���");
	}

	// ʹ��AcceptEx��������Ϊ���������WinSock2�淶֮���
	// ������Ҫ�����ȡһ�º�����ָ�룬��ȡAcceptEx����ָ��
	DWORD dwBytes = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	if (SOCKET_ERROR == WSAIoctl(m_pListenCtx->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx), &dwBytes, NULL, NULL))
	{
		showMessage("��ȡAcceptExʧ�ܡ�err=%d", WSAGetLastError());
		return false;
	}

	// ��ȡGetAcceptExSockAddrs����ָ�룬Ҳ��ͬ��
	GUID GuidAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(m_pListenCtx->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAddrs,
		sizeof(GuidAddrs), &m_lpfnGetAcceptExSockAddrs,
		sizeof(m_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		showMessage("��ȡAcceptExAddrʧ�ܡ�err=%d", WSAGetLastError());
		return false;
	}
	return true;
}

////////////////////////////////
// ��ʼ����ɶ˿�
bool IocpServer::initIOCP(ListenContext* pListenCtx)
{
	showMessage("��ʼ��IOCP()");
	//If this parameter is zero, the system allows as many 
	//concurrently running threads as there are processors in the system.
	//����˲���Ϊ�㣬��ϵͳ����Ĳ��������߳�������ϵͳ�еĴ�����������ͬ��
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
		nullptr, 0, 0); //NumberOfConcurrentThreads
	if (nullptr == m_hIOCP)
	{
		showMessage("����IOCPʧ�ܣ�err=%d!", WSAGetLastError());
		return false;
	}

	// ��Listen Socket������ɶ˿���
	if (NULL == CreateIoCompletionPort((HANDLE)pListenCtx->m_socket,
		m_hIOCP, (DWORD)pListenCtx, 0)) //CompletionKey
	{
		showMessage("��IOCPʧ�ܣ�err=%d", WSAGetLastError());
		return false;
	}
	else
	{
		showMessage("��IOCP���");
	}

	// ΪAcceptEx ׼��������Ȼ��Ͷ��AcceptEx I/O����
	// ����10���׽��֣�Ͷ��AcceptEx���󣬼�����10���׽��ֽ���accept������
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		AcceptIoContext* pAcceptIoCtx = new AcceptIoContext();
		pListenCtx->m_acceptIoCtxList.emplace_back(pAcceptIoCtx);
		if (PostResult::SUCCESS != postAccept(pListenCtx, pAcceptIoCtx))
		{
			return false;
		}
	}
	showMessage("Ͷ�� %d ��AcceptEx�������", MAX_POST_ACCEPT);
	return true;
}

bool IocpServer::initIocpWorker()
{
	showMessage("��ʼ��WorkerThread(),Pid=%d, Tid=%d",
		GetCurrentProcessId(), GetCurrentThreadId());
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	// ���ݱ����еĴ�����������������Ӧ���߳���
	int Count = sysInfo.dwNumberOfProcessors;
	Count *= WORKER_THREADS_PER_PROCESSOR;
	// ���ݼ�����������������������߳�
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
	showMessage("����WorkerThread %d ��", m_nWorkerCnt);
	return true;
}

////////////////////////////////////////////////////////////
//	����ͷŵ�������Դ
void IocpServer::deinitialize()
{
	//�رչ����߳̾��
	std::vector<HANDLE>::iterator it;
	for (it = m_hWorkerThreads.begin();
		it != m_hWorkerThreads.end(); ++it)
	{
		RELEASE_HANDLE(*it);
	}
	m_hWorkerThreads.clear();
	// �ر�ϵͳ�˳��¼����
	RELEASE_HANDLE(m_hExitEvent);
	// �ر�IOCP���
	RELEASE_HANDLE(m_hIOCP);
	// �رռ���Socket
	RELEASE_POINTER(m_pListenCtx);
	showMessage("�ͷ���Դ���");
}

////////////////////////////////////////////////////////////////////
//	��ʼ����ϵͳ�˳���Ϣ���˳���ɶ˿ں��߳���Դ
bool IocpServer::Stop()
{
	showMessage("Stop()");
	//ͬ���ȴ����й����߳��˳�
	m_bIsShutdown = true;
	exitIocpWorker();
	// ����ͻ����б���Ϣ
	releaseAllClientCtxs();
	// �ͷ�������Դ
	deinitialize();
	showMessage("ֹͣ����");
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
			0, EXIT_THREAD, NULL); //֪ͨ�����߳��˳�
		if (!bRet)
		{
			showMessage("PostQueuedCompletionStatus err=%d",
				WSAGetLastError());
		}
	}
	//���ﲻ����Ϊʲô�᷵��0������Ӧ�÷���m_nWorkerCnt-1��
	DWORD dwRet = WaitForMultipleObjects(m_nWorkerCnt,
		m_hWorkerThreads.data(), TRUE, INFINITE);
	return true;
}

bool IocpServer::SendData(ClientContext* pClientCtx, PBYTE pData, UINT len)
{
	LockGuard lk(&pClientCtx->m_csLock);
	showMessage("SendData() pClientCtx=%p len=%d", pClientCtx, len);
	if (!pClientCtx || !pData || len <= 0)
	{
		showMessage("SendData()����������");
		return false;
	}
	if (0 == pClientCtx->m_outBuf.getBufferLen())
	{
		//��һ��Ͷ�ݣ�++m_nPendingIoCnt
		pClientCtx->m_outBuf.write(pData, len);
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
		Buffer sendBuf;
		sendBuf.write(pData, len);
		pClientCtx->m_outBufQueue.emplace(sendBuf);
	}
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



//================================================================================
//				 Ͷ����ɶ˿�����
//================================================================================
//////////////////////////////////////////////////////////////////
// Ͷ��Accept����
PostResult IocpServer::postAccept(ListenContext* pListenCtx,
	AcceptIoContext* pAcceptIoCtx)
{
	if (INVALID_SOCKET == pListenCtx->m_socket)
	{
		return PostResult::INVALID;
	}
	//enterIoLoop(pClientCtx);
	pAcceptIoCtx->ResetBuffer();
	//�������ڽ������ӵ�socket
	pAcceptIoCtx->m_acceptSocket = Network::socket();
	showMessage("postAccept() pAcceptIoCtx=%p s=%d",
		pAcceptIoCtx, pAcceptIoCtx->m_acceptSocket);
	if (SOCKET_ERROR == pAcceptIoCtx->m_acceptSocket)
	{
		showMessage("��������Accept��Socketʧ�ܣ�err=%d", WSAGetLastError());
		return PostResult::INVALID;
	}
	//https://docs.microsoft.com/zh-cn/windows/win32/api/mswsock/nf-mswsock-acceptex
	// Ͷ��AcceptEx // �����ջ�����Ϊ0,��AcceptExֱ�ӷ���,��ֹ�ܾ����񹥻�	
	//* ����ͻ�������ȴû�������ݣ���acceptEx���ᴥ����ɰ������˷ѷ�������Դ
	//* ���������Ϊ�˷�ֹ�������ӣ�accpetEx�������û����ݣ�
	//* 	ֻ���յ�ַ��û�취���ӿڵ��ñ����ṩ��������
	DWORD dwBytes = 0, dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	WSABUF* pWSAbuf = &pAcceptIoCtx->m_wsaBuf; //����+16,�μ�MSDN
	if (!m_lpfnAcceptEx(m_pListenCtx->m_socket,
		pAcceptIoCtx->m_acceptSocket, pWSAbuf->buf,
		0, //pWSAbuf->len - (dwAddrLen * 2),
		dwAddrLen, dwAddrLen, &dwBytes,
		&pAcceptIoCtx->m_Overlapped))
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			showMessage("Ͷ�� AcceptEx ʧ�ܣ�err=%d", nErr);
			return PostResult::FAILED;
		}
		//Ͷ�ݳɹ�������������
		enterIoLoop(pListenCtx);
		return PostResult::SUCCESS;
	}
	else
	{
		showMessage("Ͷ�� AcceptEx ʧ�ܣ�fnAcceptEx FALSE");
		// Accept completed synchronously. We need to marshal �ռ�
		// the data received over to the worker thread ourselves...
		return PostResult::FAILED;
	}
}

//Ͷ��WSARecv����
PostResult IocpServer::postRecv(ClientContext* pClientCtx)
{
	if (INVALID_SOCKET == pClientCtx->m_socket)
	{
		return PostResult::INVALID;
	}
	else
	{
		//enterIoLoop(pClientCtx);
		LockGuard lk(&pClientCtx->m_csLock);
		RecvIoContext* pRecvIoCtx = pClientCtx->m_recvIoCtx;
		showMessage("postRecv() pClientCtx=%p, s=%d, pRecvIoCtx=%p",
			pClientCtx, pClientCtx->m_socket, pRecvIoCtx);
		pRecvIoCtx->ResetBuffer();
		//���������־����û�����������һ�ν���
		DWORD dwBytes = 0, dwFlags = 0; // MSG_PARTIAL;
		int nBytesRecv = WSARecv(pClientCtx->m_socket, &pRecvIoCtx->m_wsaBuf,
			1, &dwBytes, &dwFlags, &pRecvIoCtx->m_Overlapped, NULL);
		if (SOCKET_ERROR == nBytesRecv)
		{
			int nErr = WSAGetLastError();
			if (WSA_IO_PENDING != nErr)
			{// Overlapped I/O operation is in progress.
				showMessage("Ͷ��WSARecvʧ�ܣ�err=%d", nErr);
				return PostResult::FAILED;
			}
		}
		//Ͷ�ݳɹ�������������
		enterIoLoop(pClientCtx);
		return PostResult::SUCCESS;
	}
}

//Ͷ��WSASend����
PostResult IocpServer::postSend(ClientContext* pClientCtx)
{
	if (INVALID_SOCKET == pClientCtx->m_socket)
	{
		return PostResult::INVALID;
	}
	else
	{
		//enterIoLoop(pClientCtx);
		LockGuard lk(&pClientCtx->m_csLock);
		SendIoContext* pSendIoCtx = pClientCtx->m_sendIoCtx;
		showMessage("postSend() pClientCtx=%p, s=%d, pSendIoCtx=%p",
			pClientCtx, pClientCtx->m_socket, pSendIoCtx);

		//���������־����û�����������һ�η���
		DWORD dwBytes = 0, dwFlags = 0; // MSG_PARTIAL;
		int nRet = WSASend(pClientCtx->m_socket, &pSendIoCtx->m_wsaBuf,
			1, &dwBytes, dwFlags, &pSendIoCtx->m_Overlapped, NULL);
		if (SOCKET_ERROR == nRet)
		{ //WSAENOTCONN=10057L
			int nErr = WSAGetLastError();
			if (WSA_IO_PENDING != nErr)
			{// Overlapped I/O operation is in progress.
				showMessage("Ͷ��WSASendʧ�ܣ�err=%d", nErr);
				return PostResult::FAILED;
			}
		}
		//Ͷ�ݳɹ�������������
		enterIoLoop(pClientCtx);
		return PostResult::SUCCESS;
	}
}

//������ɶ˿��ϵĴ���
bool IocpServer::handleError(ClientContext* pClientCtx, const DWORD& dwErr)
{
	// ����ǳ�ʱ�ˣ����ټ����Ȱ� 0x102=258L
	if (WAIT_TIMEOUT == dwErr)
	{
		// ȷ�Ͽͻ����Ƿ񻹻���...
		if (!isSocketAlive(pClientCtx->m_socket))
		{
			showMessage("��⵽�ͻ����쳣�˳���");
			handleClose(pClientCtx);
			return true;
		}
		else
		{
			showMessage("���������ʱ��������..");
			return true;
		}
	}
	// �����ǿͻ����쳣�˳���; 0x40=64L
	else if (ERROR_NETNAME_DELETED == dwErr)
	{// ������������Ǽ���SOCKET�ҵ���
		showMessage("��⵽�ͻ����쳣�˳���");
		OnConnectionError(pClientCtx, dwErr);
		handleClose(pClientCtx);
		return true;
	}
	else
	{//ERROR_OPERATION_ABORTED=995L
		showMessage("��ɶ˿ڲ��������߳��˳���err=%d", dwErr);
		OnConnectionError(pClientCtx, dwErr);
		handleClose(pClientCtx);
		return false;
	}
}

////////////////////////////////////////////////////////////
bool IocpServer::handleAccept(ListenContext* pListenCtx,
	IoContext* pIoCtx, DWORD dwBytesTransferred)
{//dwBytesTransferredû���õ�
	exitIoLoop(pListenCtx);
	AcceptIoContext* pAcceptIoCtx = (AcceptIoContext*)pIoCtx;
	showMessage("handleAccept() pAcceptIoCtx=%p, s=%d",
		pAcceptIoCtx, pAcceptIoCtx->m_acceptSocket);
	//�ﵽ�����������ر��µ�socket
	if (m_nConnClientCnt + 1 >= m_nMaxConnClientCnt)
	{//WSAECONNABORTED=(10053)//Software caused connection abort.
		//WSAECONNRESET=(10054)//Connection reset by peer.
		RELEASE_SOCKET(pAcceptIoCtx->m_acceptSocket);
		postAccept(pListenCtx, pAcceptIoCtx);
		return true;
	}
	InterlockedIncrement(&m_nConnClientCnt);
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	int remoteLen = 0, localLen = 0; //����+16,�μ�MSDN
	m_lpfnGetAcceptExSockAddrs(pAcceptIoCtx->m_wsaBuf.buf,
		0, //pIoContext->m_wsaBuf.len - (dwAddrLen * 2),
		dwAddrLen, dwAddrLen, (LPSOCKADDR*)&localAddr,
		&localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);
	Network::updateAcceptContext(m_pListenCtx->m_socket,
		pAcceptIoCtx->m_acceptSocket); //���Ǹ��
	//�����µ�ClientContext��ԭ����IoContextҪ���������µ�����
	ClientContext* pClientCtx = allocateClientCtx(pAcceptIoCtx->m_acceptSocket);
	//SOCKADDR_IN sockaddr = Network::getpeername(pClientCtx->m_socket);
	pClientCtx->m_addr = *(SOCKADDR_IN*)clientAddr;
	//��Ͷ�ݣ����������ʧ�ܣ���û��Ͷ�ݣ��ᵼ��Ͷ��������
	postAccept(pListenCtx, pAcceptIoCtx); //ʧ��ʱ�ڲ�����־

	if (NULL == CreateIoCompletionPort((HANDLE)pClientCtx->m_socket,
		m_hIOCP, (ULONG_PTR)pClientCtx, 0)) //CompletionKey
	{
		showMessage("��IOCPʧ�ܣ�err=%d", WSAGetLastError());
		handleClose(pClientCtx);
		return false;
	}
	//������������
	//setKeepAlive(pClientCtx, &pAcceptIoCtx->m_overlapped);
	OnConnectionAccepted(pClientCtx);
	//Ͷ��recv����,����invalid socket�Ƿ�Ҫ�رտͻ��ˣ�
	PostResult result = postRecv(pClientCtx);
	if (PostResult::FAILED == result
		|| PostResult::INVALID == result)
	{
		handleClose(pClientCtx);
	}
	return true;
}

// ���н��յ����ݵ����ʱ�򣬽��д���
bool IocpServer::handleRecv(ClientContext* pClientCtx,
	IoContext* pIoCtx, DWORD dwBytesTransferred)
{
	exitIoLoop(pClientCtx);
	showMessage("handleRecv() pClientCtx=%p, s=%d, pRecvIoCtx=%p",
		pClientCtx, pClientCtx->m_socket, pIoCtx);
	RecvIoContext* pRecvIoCtx = (RecvIoContext*)pIoCtx;
	pClientCtx->appendToBuffer(pRecvIoCtx->m_recvBuf, dwBytesTransferred);
	OnRecvCompleted(pClientCtx);

	// Ȼ��ʼͶ����һ��WSARecv���� 
	PostResult result = postRecv(pClientCtx);
	if (PostResult::FAILED == result
		|| PostResult::INVALID == result)
	{
		handleClose(pClientCtx);
	}
	return true;
}

// �ڷ���������ϵ�ʱ�򣬽��д���
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
			//������ϣ����ܹ�socket
			//handleClose(pClientCtx);
			//releaseClientCtx(pClientCtx);
			OnSendCompleted(pClientCtx);
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
	Addr peerAddr;
	SOCKET s;
	{
		LockGuard lk(&pClientCtx->m_csLock);
		OnConnectionClosed(pClientCtx);
		s = pClientCtx->m_socket;
		peerAddr = pClientCtx->m_addr;
		pClientCtx->m_socket = INVALID_SOCKET;
	}
	if (INVALID_SOCKET != s)
	{
		if (!Network::setLinger(s))
		{
			showMessage("setLinger failed! err=%d",
				WSAGetLastError());
		}
		int ret = CancelIoEx((HANDLE)s, NULL);
		//ERROR_NOT_FOUND : cannot find a request to cancel
		if (0 == ret && ERROR_NOT_FOUND != WSAGetLastError())
		{
			showMessage("CancelIoEx failed! err=%d",
				WSAGetLastError());
		}
		RELEASE_SOCKET(s); //��ϵͳ�����ͷ���
		InterlockedDecrement(&m_nConnClientCnt);
	}
}


//=====================================================================
//				 ContextList ��ز���
//=====================================================================
ClientContext* IocpServer::allocateClientCtx(SOCKET s)
{
	showMessage("allocateClientCtx() s=%d", s);
	ClientContext* pClientCtx = nullptr;
	LockGuard lk(&m_csClientList);
	if (m_freeClientList.empty())
	{//���·���
		pClientCtx = new ClientContext(s);
	}
	else
	{//ȡ��ԭ��
		pClientCtx = m_freeClientList.front();
		m_freeClientList.pop_front();
		pClientCtx->m_socket = s;
	}
	assert(pClientCtx != nullptr);
	addClientCtx(pClientCtx); 
	return pClientCtx;
}

void IocpServer::releaseClientCtx(ClientContext* pClientCtx)
{
	showMessage("releaseClientCtx() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	//���ﲻɾ�������ǽ�ClientContext�Ƶ���������
	removeClientCtx(pClientCtx); //delete pClientCtx;
}

void IocpServer::addClientCtx(ClientContext* pClientCtx)
{
	showMessage("addClientCtx() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	LockGuard lk(&m_csClientList);
	m_usedClientList.emplace_back(pClientCtx);
}

void IocpServer::removeClientCtx(ClientContext* pClientCtx)
{
	showMessage("removeClientCtx() pClientCtx=%p, s=%d",
		pClientCtx, pClientCtx->m_socket);
	LockGuard lk(&m_csClientList);
	{
		auto it = std::find(m_usedClientList.begin(),
			m_usedClientList.end(), pClientCtx);
		if (m_usedClientList.end() != it)
		{//�ҵ�����Ҫ������usedList���Ƴ�
			m_usedClientList.remove(pClientCtx);
			pClientCtx->reset(); //���ú󣬷���freeList
			m_freeClientList.emplace_back(pClientCtx);
		}
	}
}

void IocpServer::releaseAllClientCtxs()
{
	showMessage("removeAllClientCtxs()");
	LockGuard lk(&m_csClientList);
	std::list<ClientContext*>::iterator it;
	for (it = m_usedClientList.begin();
		it != m_usedClientList.end(); ++it)
	{//Ҫ�Ƿ������ռ�õ��ڴ�
		RELEASE_POINTER(*it);
	}
	m_usedClientList.clear();
	for (it = m_freeClientList.begin();
		it != m_freeClientList.end(); ++it)
	{//Ҫ�Ƿ������ռ�õ��ڴ�
		RELEASE_POINTER(*it);
	}
	m_freeClientList.clear();
}

void IocpServer::OnConnectionAccepted(ClientContext* pClientCtx)
{
	std::string addr = pClientCtx->m_addr;
	showMessage("OnConnectionAccepted() pClientCtx=%p, s=%d, %s",
		pClientCtx, pClientCtx->m_socket, addr.c_str());
	printf("m_nConnClientCnt=%d\n", m_nConnClientCnt);
}

void IocpServer::OnConnectionClosed(ClientContext* pClientCtx)
{
	std::string addr = pClientCtx->m_addr;
	showMessage("OnConnectionClosed() pClientCtx=%p, s=%d, %s",
		pClientCtx, pClientCtx->m_socket, addr.c_str());
	printf("m_nConnClientCnt=%d.\n", m_nConnClientCnt);
}

void IocpServer::OnConnectionError(ClientContext* pClientCtx, int error)
{
	showMessage("OnConnectionError() pClientCtx=%p, s=%d, error=%d",
		pClientCtx, pClientCtx->m_socket, error);
	printf("m_nConnClientCnt=%d\n", m_nConnClientCnt);
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

//for tcp_keepalive
#include <mstcpip.h>
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
	//����msdn����Ҫ��һ��OVERLAPPED�ṹ
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
// �жϿͻ���Socket�Ƿ��Ѿ��Ͽ���������һ����Ч��Socket��Ͷ��WSARecv����������쳣
// ʹ�õķ����ǳ��������socket�������ݣ��ж����socket���õķ���ֵ
// ��Ϊ����ͻ��������쳣�Ͽ�(����ͻ��˱������߰ε����ߵ�)��ʱ��
// �����������޷��յ��ͻ��˶Ͽ���֪ͨ��
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
	printf("%4d-%02d-%02d %02d:%02d:%02d.%03d��",
		sysTime.wYear, sysTime.wMonth, sysTime.wDay,
		sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
		sysTime.wMilliseconds);
}

/////////////////////////////////////////////////////////////////////
// ������������ʾ��ʾ��Ϣ
void IocpServer::showMessage(const char* szFormat, ...)
{
	//printf(".");
	return;
	__try
	{
		EnterCriticalSection(&m_csLog);
		print_datetime();
		// ����䳤����
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
