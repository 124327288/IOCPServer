#include "IOCPModel.h"

CIocpModel::CIocpModel(void) :
	m_nThreads(0),
	m_hShutdownEvent(nullptr),
	m_hIOCompletionPort(nullptr),
	m_phWorkerThreads(nullptr),
	m_strIP(DEFAULT_IP),
	m_nPort(DEFAULT_PORT),
	m_lpfnAcceptEx(nullptr),
	m_lpfnGetAcceptExSockAddrs(nullptr),
	m_pListenContext(nullptr)
{
	m_LogFunc = nullptr;
}

CIocpModel::~CIocpModel(void)
{
	// ȷ����Դ�����ͷ�
	this->Stop();
}

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
DWORD WINAPI CIocpModel::_WorkerThread(LPVOID lpParam)
{
	WorkerThreadParam* pParam = (WorkerThreadParam*)lpParam;
	CIocpModel* pIocpModel = (CIocpModel*)pParam->pIocpModel;
	const int nThreadNo = pParam->nThreadNo;

	pIocpModel->_ShowMessage("�������߳�������ID: %d.", nThreadNo);
	//ѭ����������ֱ�����յ�Shutdown��ϢΪֹ
	while (WAIT_OBJECT_0 != WaitForSingleObject(pIocpModel->m_hShutdownEvent, 0))
	{
		DWORD dwBytesTransfered = 0;
		OVERLAPPED* pOverlapped = nullptr;
		SocketContext* pSocketContext = nullptr;
		const BOOL bRet = GetQueuedCompletionStatus(pIocpModel->m_hIOCompletionPort,
			&dwBytesTransfered, (PULONG_PTR)&pSocketContext, &pOverlapped, INFINITE);
		//����EXIT_CODE�˳���־����ֱ���˳�
		if (EXIT_CODE == (DWORD)pSocketContext)
		{
			break;
		}
		if (!bRet)
		{	//����ֵΪfalse����ʾ����
			const DWORD dwErr = GetLastError();
			// ��ʾһ����ʾ��Ϣ
			if (!pIocpModel->HandleError(pSocketContext, dwErr))
			{
				break;
			}
			continue;
		}
		else
		{
			// ��ȡ����Ĳ���
			IoContext* pIoContext = CONTAINING_RECORD(pOverlapped,
				IoContext, m_Overlapped);
			// �ж��Ƿ��пͻ��˶Ͽ���
			if ((0 == dwBytesTransfered)
				&& (OPERATION_TYPE::RECV == pIoContext->m_OpType
					|| OPERATION_TYPE::SEND == pIoContext->m_OpType))
			{
				pIocpModel->_ShowMessage("�ͻ��� %s:%d �Ͽ�����.",
					inet_ntoa(pSocketContext->m_ClientAddr.sin_addr),
					ntohs(pSocketContext->m_ClientAddr.sin_port));
				// �ͷŵ���Ӧ����Դ
				pIocpModel->_RemoveContext(pSocketContext);
				continue;
			}
			else
			{
				switch (pIoContext->m_OpType)
				{
				case OPERATION_TYPE::ACCEPT:
				{
					// Ϊ�����Ӵ���ɶ��ԣ�������ר�ŵ�_DoAccept�������д�����������
					pIoContext->m_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoAccpet(pSocketContext, pIoContext);
				}
				break;

				case OPERATION_TYPE::RECV:
				{
					// Ϊ�����Ӵ���ɶ��ԣ�������ר�ŵ�_DoRecv�������д����������
					pIoContext->m_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoRecv(pSocketContext, pIoContext);
				}
				break;

				// �����Թ���д�ˣ�Ҫ������̫���ˣ���������⣬Send�������������һЩ
				case OPERATION_TYPE::SEND:
				{
					pIoContext->m_nSendBytes += dwBytesTransfered;
					if (pIoContext->m_nSendBytes < pIoContext->m_nTotalBytes)
					{
						//����δ�ܷ����꣬������������
						pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer
							+ pIoContext->m_nSendBytes;
						pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes
							- pIoContext->m_nSendBytes;
						pIocpModel->PostWrite(pIoContext);
					}
					else
					{
						pIocpModel->PostRecv(pIoContext);
					}
				}
				break;
				default:
					// ��Ӧ��ִ�е�����
					pIocpModel->_ShowMessage("_WorkThread�е�m_OpType �����쳣.\n");
					break;
				} //switch
			}//if
		}//if
	}//while
	pIocpModel->_ShowMessage("�������߳� %d ���˳�.\n", nThreadNo);
	// �ͷ��̲߳���
	RELEASE_POINTER(lpParam);
	return 0;
}

//================================================================================
//				 ϵͳ��ʼ������ֹ
//================================================================================
////////////////////////////////////////////////////////////////////
// ��ʼ��WinSock 2.2
//�������ܣ���ʼ���׽���
bool CIocpModel::LoadSocketLib()
{
	WSADATA wsaData = { 0 };
	const int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	// ����(һ�㶼�����ܳ���)
	if (NO_ERROR != nRet)
	{
		this->_ShowMessage("��ʼ��WinSock 2.2ʧ�ܣ�\n");
		return false;
	}
	return true;
}


//�������ܣ�����������
bool CIocpModel::Start()
{
	// ��ʼ���̻߳�����
	InitializeCriticalSection(&m_csContextList);
	// ����ϵͳ�˳����¼�֪ͨ
	m_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	// ��ʼ��IOCP
	if (!_InitializeIOCP())
	{
		this->_ShowMessage("��ʼ��IOCPʧ�ܣ�\n");
		return false;
	}
	else
	{
		this->_ShowMessage("\nIOCP��ʼ�����\n.");
	}
	// ��ʼ��Socket
	if (!_InitializeListenSocket())
	{
		this->_ShowMessage("Listen Socket��ʼ��ʧ�ܣ�\n");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("Listen Socket��ʼ�����.");
	}
	this->_ShowMessage("ϵͳ׼���������Ⱥ�����....\n");
	return true;
}

////////////////////////////////////////////////////////////////////
//	��ʼ����ϵͳ�˳���Ϣ���˳���ɶ˿ں��߳���Դ
void CIocpModel::Stop()
{
	if (m_pListenContext != nullptr
		&& m_pListenContext->m_Socket != INVALID_SOCKET)
	{
		// ����ر���Ϣ֪ͨ
		SetEvent(m_hShutdownEvent);
		for (int i = 0; i < m_nThreads; i++)
		{
			// ֪ͨ���е���ɶ˿ڲ����˳�
			PostQueuedCompletionStatus(m_hIOCompletionPort,
				0, (DWORD)EXIT_CODE, NULL);
		}
		// �ȴ����еĿͻ�����Դ�˳�
		WaitForMultipleObjects(m_nThreads, m_phWorkerThreads, 
			TRUE, INFINITE);
		// ����ͻ����б���Ϣ
		this->_ClearContextList();
		// �ͷ�������Դ
		this->_DeInitialize();
		this->_ShowMessage("ֹͣ����\n");
	}
}

/*************************************************************
*�������ܣ�Ͷ��WSARecv����
*����������
IoContext* pIoContext:	���ڽ���IO���׽����ϵĽṹ����ҪΪWSARecv������WSASend������
**************************************************************/
bool CIocpModel::PostRecv(IoContext* pIoContext)
{
	pIoContext->ResetBuffer();
	pIoContext->m_OpType = OPERATION_TYPE::RECV;
	pIoContext->m_nSendBytes = 0;
	pIoContext->m_nTotalBytes = 0;
	// ��ʼ������
	DWORD dwFlags = 0, dwBytes = 0;
	// ��ʼ����ɺ�Ͷ��WSARecv����
	const int nBytesRecv = WSARecv(pIoContext->m_sockAccept,
		&pIoContext->m_wsaBuf, 1, &dwBytes, &dwFlags,
		&pIoContext->m_Overlapped, NULL);
	// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
	if ((SOCKET_ERROR == nBytesRecv) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		this->_ShowMessage("Ͷ�ݵ�һ��WSARecvʧ�ܣ�");
		return false;
	}
	return true;
}

/*************************************************************
*�������ܣ�Ͷ��WSASend����
*����������
IoContext* pIoContext:	���ڽ���IO���׽����ϵĽṹ����ҪΪWSARecv������WSASend����
*����˵��������PostWrite֮ǰ��Ҫ����pIoContext��m_wsaBuf, m_nTotalBytes, m_nSendBytes��
**************************************************************/
bool CIocpModel::PostWrite(IoContext* pIoContext)
{
	// ��ʼ������
	////pIoContext->ResetBuffer(); //�ⲿ����m_wsaBuf
	pIoContext->m_OpType = OPERATION_TYPE::SEND;
	//Ͷ��WSASend���� -- ��Ҫ�޸�
	const DWORD dwFlags = 0;
	DWORD dwSendNumBytes = 0;
	const int nRet = WSASend(pIoContext->m_sockAccept,
		&pIoContext->m_wsaBuf, 1, &dwSendNumBytes, dwFlags,
		&pIoContext->m_Overlapped, NULL);
	// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
	if ((SOCKET_ERROR == nRet) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		this->_ShowMessage("Ͷ��WSASendʧ�ܣ�");
		return false;
	}
	return true;
}

////////////////////////////////
// ��ʼ����ɶ˿�
bool CIocpModel::_InitializeIOCP()
{
	// ������һ����ɶ˿�
	m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (nullptr == m_hIOCompletionPort)
	{
		this->_ShowMessage("������ɶ˿�ʧ�ܣ��������: %d!\n", WSAGetLastError());
		return false;
	}
	// ���ݱ����еĴ�����������������Ӧ���߳���
	m_nThreads = WORKER_THREADS_PER_PROCESSOR * _GetNumOfProcessors();
	// Ϊ�������̳߳�ʼ�����
	m_phWorkerThreads = new HANDLE[m_nThreads];
	// ���ݼ�����������������������߳�
	DWORD nThreadID = 0;
	for (int i = 0; i < m_nThreads; i++)
	{
		WorkerThreadParam* pThreadParams = new WorkerThreadParam;
		pThreadParams->pIocpModel = this;
		pThreadParams->nThreadNo = i + 1;
		m_phWorkerThreads[i] = ::CreateThread(0, 0, _WorkerThread,
			(void*)pThreadParams, 0, &nThreadID);
	}
	this->_ShowMessage(" ���� _WorkerThread %d ��.\n", m_nThreads);
	return true;
}

/////////////////////////////////////////////////////////////////
// ��ʼ��Socket
bool CIocpModel::_InitializeListenSocket()
{
	// AcceptEx �� GetAcceptExSockaddrs ��GUID�����ڵ�������ָ��
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;

	// ��������ַ��Ϣ�����ڰ�Socket
	struct sockaddr_in ServerAddress;

	// �������ڼ�����Socket����Ϣ
	m_pListenContext = new SocketContext;

	// ��Ҫʹ���ص�IO�������ʹ��WSASocket������Socket���ſ���֧���ص�IO����
	m_pListenContext->m_Socket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_pListenContext->m_Socket)
	{
		this->_ShowMessage("��ʼ��Socketʧ�ܣ��������: %d.\n", WSAGetLastError());
		return false;
	}
	else
	{
		this->_ShowMessage("WSASocket() ���.\n");
	}

	// ��Listen Socket������ɶ˿���
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenContext->m_Socket,
		m_hIOCompletionPort, (DWORD)m_pListenContext, 0))
	{
		this->_ShowMessage("�� Listen Socket����ɶ˿�ʧ�ܣ��������: %d/n",
			WSAGetLastError());
		RELEASE_SOCKET(m_pListenContext->m_Socket);
		return false;
	}
	else
	{
		this->_ShowMessage("Listen Socket����ɶ˿� ���.\n");
	}

	// ����ַ��Ϣ
	ZeroMemory((char*)&ServerAddress, sizeof(ServerAddress));
	ServerAddress.sin_family = AF_INET;
	// ������԰��κο��õ�IP��ַ�����߰�һ��ָ����IP��ַ 
	//ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY); 
	ServerAddress.sin_addr.s_addr = inet_addr(m_strIP.c_str());
	ServerAddress.sin_port = htons(m_nPort);

	// �󶨵�ַ�Ͷ˿�
	if (SOCKET_ERROR == bind(m_pListenContext->m_Socket,
		(struct sockaddr*) & ServerAddress, sizeof(ServerAddress)))
	{
		this->_ShowMessage("bind()����ִ�д���.\n");
		return false;
	}
	else
	{
		this->_ShowMessage("bind() ���.\n");
	}

	// ��ʼ���м���
	if (SOCKET_ERROR == listen(m_pListenContext->m_Socket, SOMAXCONN))
	{
		this->_ShowMessage("Listen()����ִ�г��ִ���.\n");
		return false;
	}
	else
	{
		this->_ShowMessage("Listen() ���.\n");
	}

	// ʹ��AcceptEx��������Ϊ���������WinSock2�淶֮���΢�������ṩ����չ����
	// ������Ҫ�����ȡһ�º�����ָ�룬��ȡAcceptEx����ָ��
	DWORD dwBytes = 0;
	if (SOCKET_ERROR == WSAIoctl(m_pListenContext->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoctl δ�ܻ�ȡAcceptEx����ָ�롣�������: %d\n",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	// ��ȡGetAcceptExSockAddrs����ָ�룬Ҳ��ͬ��
	if (SOCKET_ERROR == WSAIoctl(m_pListenContext->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs), &m_lpfnGetAcceptExSockAddrs,
		sizeof(m_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoctl δ�ܻ�ȡGuidGetAcceptExSockAddrs����ָ�롣"
			"�������: %d\n", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	// ΪAcceptEx ׼��������Ȼ��Ͷ��AcceptEx I/O����
	// ����10���׽��֣�Ͷ��AcceptEx���󣬼�����10���׽��ֽ���accept������
	for (int i = 0; i < MAX_POST_ACCEPT; i++)
	{
		// �½�һ��IO_CONTEXT
		IoContext* pAcceptIoContext = m_pListenContext->GetNewIoContext();
		if (!this->_PostAccept(pAcceptIoContext))
		{
			m_pListenContext->RemoveContext(pAcceptIoContext);
			return false;
		}
	}

	this->_ShowMessage("Ͷ�� %d ��AcceptEx�������", MAX_POST_ACCEPT);
	return true;
}

////////////////////////////////////////////////////////////
//	����ͷŵ�������Դ
void CIocpModel::_DeInitialize()
{
	// ɾ���ͻ����б�Ļ�����
	DeleteCriticalSection(&m_csContextList);
	// �ر�ϵͳ�˳��¼����
	RELEASE_HANDLE(m_hShutdownEvent);
	// �ͷŹ������߳̾��ָ��
	for (int i = 0; i < m_nThreads; i++)
	{
		RELEASE_HANDLE(m_phWorkerThreads[i]);
	}

	RELEASE_ARRAY(m_phWorkerThreads);
	// �ر�IOCP���
	RELEASE_HANDLE(m_hIOCompletionPort);
	// �رռ���Socket
	RELEASE_POINTER(m_pListenContext);
	this->_ShowMessage("�ͷ���Դ���.\n");
}

//================================================================================
//				 Ͷ����ɶ˿�����
//================================================================================
//////////////////////////////////////////////////////////////////
// Ͷ��Accept����
bool CIocpModel::_PostAccept(IoContext* pAcceptIoContext)
{
	//ASSERT(INVALID_SOCKET != m_pListenContext->m_Socket);
	if (m_pListenContext->m_Socket == INVALID_SOCKET)
	{
		throw "_PostAccept,m_pListenContext->m_Socket != INVALID_SOCKET";
	}
	// ׼������
	pAcceptIoContext->ResetBuffer(); 
	pAcceptIoContext->m_OpType = OPERATION_TYPE::ACCEPT;
	// Ϊ�Ժ�������Ŀͻ�����׼����Socket( ������봫ͳaccept�������� ) 
	pAcceptIoContext->m_sockAccept = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == pAcceptIoContext->m_sockAccept)
	{
		_ShowMessage("��������Accept��Socketʧ�ܣ��������: %d", WSAGetLastError());
		return false;
	}
	// Ͷ��AcceptEx
	DWORD dwBytes = 0;
	WSABUF* pWSAbuf = &pAcceptIoContext->m_wsaBuf;
	if (!m_lpfnAcceptEx(m_pListenContext->m_Socket,
		pAcceptIoContext->m_sockAccept, pWSAbuf->buf,
		pWSAbuf->len - ((sizeof(SOCKADDR_IN) + 16) * 2),
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, 
		&dwBytes, &pAcceptIoContext->m_Overlapped))
	{
		if (WSA_IO_PENDING != WSAGetLastError())
		{
			_ShowMessage("Ͷ�� AcceptEx ����ʧ�ܣ��������: %d", WSAGetLastError());
			return false;
		}
	}
	return true;
}

////////////////////////////////////////////////////////////
// ���пͻ��������ʱ�򣬽��д���
// �����е㸴�ӣ���Ҫ�ǿ������Ļ����Ϳ����׵��ĵ���....
// ������������Ļ�����ɶ˿ڵĻ������������һ�����

// ��֮��Ҫ֪�����������ListenSocket��Context��������Ҫ����һ�ݳ������������Socket��
// ԭ����Context����Ҫ���������Ͷ����һ��Accept����
/********************************************************************
*�������ܣ��������пͻ��˽��봦��
*����˵����
SocketContext* pSocketContext:	����accept������Ӧ���׽��֣����׽�������Ӧ�����ݽṹ��
IoContext* pIoContext:			����accept������Ӧ�����ݽṹ��
DWORD		dwIOSize:			���β�������ʵ�ʴ�����ֽ���
********************************************************************/
bool CIocpModel::_DoAccpet(SocketContext* pSocketContext, IoContext* pIoContext)
{
	if (pIoContext->m_nTotalBytes > 0)
	{
		//�ͻ�����ʱ����һ�ν���dwIOSize�ֽ�����
		_DoFirstRecvWithData(pIoContext);
	}
	else
	{
		//�ͻ��˽���ʱ��û�з������ݣ���Ͷ��WSARecv���󣬽�������
		_DoFirstRecvWithoutData(pIoContext);
	}
	// 5. ʹ�����֮�󣬰�Listen Socket���Ǹ�IoContext���ã�Ȼ��׼��Ͷ���µ�AcceptEx
	return this->_PostAccept(pIoContext);
}

/*************************************************************
*�������ܣ�AcceptEx���տͻ����ӳɹ������տͻ���һ�η��͵����ݣ���Ͷ��WSASend����
*����������IoContext* pIoContext:	���ڼ����׽����ϵĲ���
**************************************************************/
bool CIocpModel::_DoFirstRecvWithData(IoContext* pIoContext)
{
	SOCKADDR_IN* ClientAddr = NULL, * LocalAddr = NULL;
	int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);

	///////////////////////////////////////////////////////////////////////////
	// 1. ����ȡ������ͻ��˵ĵ�ַ��Ϣ
	// ��� m_lpfnGetAcceptExSockAddrs �����˰�~~~~~~
	// ��������ȡ�ÿͻ��˺ͱ��ض˵ĵ�ַ��Ϣ������˳��ȡ���ͻ��˷����ĵ�һ�����ݣ���ǿ����...
	this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf,
		pIoContext->m_wsaBuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2),
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		(LPSOCKADDR*)&LocalAddr, &localLen, (LPSOCKADDR*)&ClientAddr, &remoteLen);

	//��ʾ�ͻ�����Ϣ
	this->_ShowMessage("�ͻ��� %s:%d ����", inet_ntoa(ClientAddr->sin_addr),
		ntohs(ClientAddr->sin_port));
	this->_ShowMessage("�յ� %s:%d ��Ϣ��%s", inet_ntoa(ClientAddr->sin_addr),
		ntohs(ClientAddr->sin_port), pIoContext->m_wsaBuf.buf);

	////////////////////////////////////////////////////////////////////////////////
	// 2. ������Ҫע�⣬���ﴫ��������ListenSocket�ϵ�Context��
	// ���Context���ǻ���Ҫ���ڼ�����һ�����ӣ������һ���Ҫ��ListenSocket
	//	�ϵ�Context���Ƴ���һ�ݣ�Ϊ�������Socket�½�һ��SocketContext
	//	Ϊ�½�����׽Ӵ���SocketContext���������׽��ְ󶨵���ɶ˿�
	SocketContext* pNewSocketContext = new SocketContext;
	pNewSocketContext->m_Socket = pIoContext->m_sockAccept;
	memcpy(&(pNewSocketContext->m_ClientAddr), ClientAddr, remoteLen);
	// ����������ϣ������Socket����ɶ˿ڰ�(��Ҳ��һ���ؼ�����)
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{
		RELEASE_POINTER(pNewSocketContext);
		return false;
	}

	//3. �������������µ�IoContext�����������Socket��Ͷ�ݵ�һ��Recv��������
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	pNewIoContext->m_OpType = OPERATION_TYPE::SEND;
	pNewIoContext->m_sockAccept = pNewSocketContext->m_Socket;
	pNewIoContext->m_nTotalBytes = pIoContext->m_nTotalBytes;
	pNewIoContext->m_nSendBytes = 0;
	pNewIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes;
	memcpy(pNewIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.buf,
		pIoContext->m_nTotalBytes);	//�������ݵ�WSASend�����Ĳ���������
	//��ʱ�ǵ�һ�ν������ݳɹ�����������Ͷ��PostWrite����ͻ��˷�������
	if (!this->PostWrite(pNewIoContext)) //���ﲻӦ��ֱ��PostWrite
	{
		pNewSocketContext->RemoveContext(pNewIoContext);
		RELEASE_POINTER(pNewSocketContext);
		return false;
	}

	//4. ���Ͷ�ݳɹ�����ô�Ͱ������Ч�Ŀͻ�����Ϣ��
	//���뵽ContextList��ȥ(��Ҫͳһ���������ͷ���Դ)
	this->_AddToContextList(pNewSocketContext);

	//////////////////////////////////////////////////////////////////////////////////
	// 5. ʹ�����֮�󣬰�Listen Socket���Ǹ�IoContext���ã�Ȼ��׼��Ͷ���µ�AcceptEx
	//pIoContext->ResetBuffer();
	//return this->_PostAccept(pIoContext );
	return true;
}

/*************************************************************
*�������ܣ�AcceptEx���տͻ����ӳɹ�����ʱ��δ���յ����ݣ���Ͷ��WSARecv����
*����������IoContext* pIoContext:	���ڼ����׽����ϵĲ���
**************************************************************/
bool CIocpModel::_DoFirstRecvWithoutData(IoContext* pIoContext)
{
	//Ϊ�½�����׽��ִ���SocketContext�ṹ�����󶨵���ɶ˿�
	SOCKADDR ClientAddr = { 0 };
	int Len = sizeof(ClientAddr);
	getpeername(pIoContext->m_sockAccept, &ClientAddr, &Len);
	SocketContext* pNewSocketContext = new SocketContext;
	pNewSocketContext->m_Socket = pIoContext->m_sockAccept;
	memcpy(&(pNewSocketContext->m_ClientAddr), &ClientAddr, sizeof(SOCKADDR_IN));
	//�����׽��ְ󶨵���ɶ˿�
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{
		RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	//Ͷ��WSARecv���󣬽�������
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	//��ʱ��AcceptExδ���յ��ͻ��˵�һ�η��͵����ݣ�
	//�����������PostRecv���������Կͻ��˵�����
	if (!this->PostRecv(pNewIoContext))
	{
		pNewSocketContext->RemoveContext(pNewIoContext);
		RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	//���Ͷ�ݳɹ�����ô�Ͱ������Ч�Ŀͻ�����Ϣ��
	//���뵽ContextList��ȥ(��Ҫͳһ���������ͷ���Դ)
	this->_AddToContextList(pNewSocketContext);
	return true;
}

/////////////////////////////////////////////////////////////////
// ���н��յ����ݵ����ʱ�򣬽��д���
bool CIocpModel::_DoRecv(SocketContext* pSocketContext, IoContext* pIoContext)
{
	// �Ȱ���һ�ε�������ʾ���֣�Ȼ�������״̬��������һ��Recv����
	SOCKADDR_IN* ClientAddr = &pSocketContext->m_ClientAddr;
	this->_ShowMessage("�յ� %s:%d ��Ϣ��%s", inet_ntoa(ClientAddr->sin_addr),
		ntohs(ClientAddr->sin_port), pIoContext->m_wsaBuf.buf);

	// Ȼ��ʼͶ����һ��WSARecv���� //��������
	//���ﲻӦ��ֱ��PostWrite����ʲôӦ����Ӧ�þ���
	pIoContext->m_nSendBytes = 0;
	pIoContext->m_nTotalBytes = pIoContext->m_nTotalBytes;
	pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes;
	pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer;
	return PostWrite(pIoContext);  
}

/////////////////////////////////////////////////////
// �����(Socket)�󶨵���ɶ˿���
bool CIocpModel::_AssociateWithIOCP(SocketContext* pContext)
{
	// �����ںͿͻ���ͨ�ŵ�SOCKET�󶨵���ɶ˿���
	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pContext->m_Socket,
		m_hIOCompletionPort, (DWORD)pContext, 0);
	if (nullptr == hTemp)
	{
		this->_ShowMessage("ִ��CreateIoCompletionPort()����.������룺%d",
			GetLastError());
		return false;
	}
	return true;
}

//=====================================================================
//				 ContextList ��ز���
//=====================================================================
//////////////////////////////////////////////////////////////
// ���ͻ��˵������Ϣ�洢��������
void CIocpModel::_AddToContextList(SocketContext* pSocketContext)
{
	EnterCriticalSection(&m_csContextList);
	m_arrayClientContext.push_back(pSocketContext);
	LeaveCriticalSection(&m_csContextList);
}

////////////////////////////////////////////////////////////////
//	�Ƴ�ĳ���ض���Context
void CIocpModel::_RemoveContext(SocketContext* pSocketContext)
{
	EnterCriticalSection(&m_csContextList);
	vector<SocketContext*>::iterator it;
	it = m_arrayClientContext.begin();
	while (it != m_arrayClientContext.end())
	{
		SocketContext* pContext = *it;
		if (pSocketContext == pContext)
		{
			delete pSocketContext;
			pSocketContext = nullptr;
			it = m_arrayClientContext.erase(it);
			break;
		}
		it++;
	}
	LeaveCriticalSection(&m_csContextList);
}

////////////////////////////////////////////////////////////////
// ��տͻ�����Ϣ
void CIocpModel::_ClearContextList()
{
	EnterCriticalSection(&m_csContextList);
	for (size_t i = 0; i < m_arrayClientContext.size(); i++)
	{
		delete m_arrayClientContext.at(i);
	}
	m_arrayClientContext.clear();
	LeaveCriticalSection(&m_csContextList);
}

//================================================================================
//				 ����������������
//================================================================================
////////////////////////////////////////////////////////////////////
// ��ñ�����IP��ַ
string CIocpModel::GetLocalIP()
{
	// ��ñ���������
	char hostname[MAX_PATH] = { 0 };
	gethostname(hostname, MAX_PATH);
	struct hostent FAR* lpHostEnt = gethostbyname(hostname);
	if (lpHostEnt == NULL)
	{
		return DEFAULT_IP;
	}
	// ȡ��IP��ַ�б��еĵ�һ��Ϊ���ص�IP(��Ϊһ̨�������ܻ�󶨶��IP)
	const LPSTR lpAddr = lpHostEnt->h_addr_list[0];
	// ��IP��ַת�����ַ�����ʽ
	struct in_addr inAddr;
	memmove(&inAddr, lpAddr, 4);
	m_strIP = string(inet_ntoa(inAddr));
	return m_strIP;
}

///////////////////////////////////////////////////////////////////
// ��ñ����д�����������
int CIocpModel::_GetNumOfProcessors() noexcept
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

/////////////////////////////////////////////////////////////////////
// �жϿͻ���Socket�Ƿ��Ѿ��Ͽ���������һ����Ч��Socket��Ͷ��WSARecv����������쳣
// ʹ�õķ����ǳ��������socket�������ݣ��ж����socket���õķ���ֵ
// ��Ϊ����ͻ��������쳣�Ͽ�(����ͻ��˱������߰ε����ߵ�)��ʱ��
// �����������޷��յ��ͻ��˶Ͽ���֪ͨ��
bool CIocpModel::_IsSocketAlive(SOCKET s) noexcept
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

///////////////////////////////////////////////////////////////////
//�������ܣ���ʾ��������ɶ˿��ϵĴ���
bool CIocpModel::HandleError(SocketContext* pContext, const DWORD& dwErr)
{
	// ����ǳ�ʱ�ˣ����ټ����Ȱ� 
	if (WAIT_TIMEOUT == dwErr)
	{
		// ȷ�Ͽͻ����Ƿ񻹻���...
		if (!_IsSocketAlive(pContext->m_Socket))
		{
			this->_ShowMessage("��⵽�ͻ����쳣�˳���");
			this->_RemoveContext(pContext);
			return true;
		}
		else
		{
			this->_ShowMessage("���������ʱ��������...");
			return true;
		}
	}
	// �����ǿͻ����쳣�˳���
	else if (ERROR_NETNAME_DELETED == dwErr)
	{
		this->_ShowMessage("��⵽�ͻ����쳣�˳���");
		this->_RemoveContext(pContext);
		return true;
	}
	else
	{
		this->_ShowMessage("��ɶ˿ڲ������ִ����߳��˳���������룺%d", dwErr);
		return false;
	}
}

/////////////////////////////////////////////////////////////////////
// ������������ʾ��ʾ��Ϣ
void CIocpModel::_ShowMessage(const char* szFormat, ...) const
{
	if (m_LogFunc)
	{
		char buff[256] = { 0 };
		va_list arglist;
		// ����䳤����
		va_start(arglist, szFormat);
		vsnprintf(buff, sizeof(buff), szFormat, arglist);
		va_end(arglist);

		m_LogFunc(string(buff));
	}
}
