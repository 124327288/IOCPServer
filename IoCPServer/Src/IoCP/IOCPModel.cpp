//#include "StdAfx.h"
#include "IOCPModel.h"
#include "..\ServerEngine\MyServerEngine.h"


namespace MyServer{


	HANDLE CIOCPModel::m_hMutexServerEngine = CreateMutex(NULL,FALSE,"m_hMutexServerEngine");

	CIOCPModel::CIOCPModel(void):
								m_nThreads(0),
								m_hShutdownEvent(NULL),
								m_hIOCompletionPort(NULL),
								m_phWorkerThreads(NULL),
								m_strIP(DEFAULT_IP),
								m_lpfnAcceptEx( NULL ),
								m_pListenContext( NULL ){

		m_nPort = 12345;	//�˿�

	}


	CIOCPModel::~CIOCPModel(void){
		// ȷ����Դ�����ͷ�
		this->Stop();
	}


	/*********************************************************************
	*�������ܣ��̺߳���������GetQueuedCompletionStatus����������д���
	*����������lpParam��THREADPARAMS_WORKER����ָ�룻
	*����˵����GetQueuedCompletionStatus��ȷ����ʱ��ʾĳ�����Ѿ���ɣ��ڶ�������lpNumberOfBytes��ʾ�����׽��ִ�����ֽ�����
	����lpCompletionKey��lpOverlapped������Ҫ����Ϣ�����ѯMSDN�ĵ���
	*********************************************************************/
	DWORD WINAPI CIOCPModel::_WorkerThread(LPVOID lpParam){    

		THREADPARAMS_WORKER* pParam = (THREADPARAMS_WORKER*)lpParam;
		CIOCPModel* pIOCPModel = (CIOCPModel*)pParam->pIOCPModel;
		int nThreadNo = (int)pParam->nThreadNo;

		char aa[256];
		sprintf_s(aa,"�������߳�������ID: %d.",nThreadNo);
		g_pServerEngine->AddServerMsgs(string(aa));

		OVERLAPPED           *pOverlapped = NULL;
		PER_SOCKET_CONTEXT   *pSocketContext = NULL;
		DWORD                dwBytesTransfered = 0;

		//ѭ����������ֱ�����յ�Shutdown��ϢΪֹ
		while (WaitForSingleObject(pIOCPModel->m_hShutdownEvent, 0) != WAIT_OBJECT_0 ){
			//�����ɶ˿�״̬
			BOOL bReturn = GetQueuedCompletionStatus(
				pIOCPModel->m_hIOCompletionPort,
				&dwBytesTransfered,
				(PULONG_PTR)&pSocketContext,
				&pOverlapped,
				INFINITE);

			//����EXIT_CODE�˳���־����ֱ���˳�
			if ( (DWORD)pSocketContext == EXIT_CODE){
				break;
			}

			//����ֵΪ0����ʾ����
			if( bReturn == 0 )  {
				DWORD dwErr = GetLastError();
				// �����Իָ��Ĵ���,�˳�
				if( pIOCPModel->HandleError( pSocketContext,dwErr ) == false){
					break;
				}
				
				//���Իָ��Ĵ���,��������
				continue;  

			}  else  {  	
				// ��ȡ����Ĳ���
				PER_IO_CONTEXT* pIoContext = CONTAINING_RECORD(pOverlapped, PER_IO_CONTEXT, m_Overlapped);  

				// �ж��Ƿ��пͻ��˶Ͽ���
				if((dwBytesTransfered == 0) && ( pIoContext->m_OpType == RECV_POSTED || pIoContext->m_OpType == SEND_POSTED))  {  
					sprintf_s(aa,"�ͻ��� %s:%d �Ͽ�����.",inet_ntoa(pSocketContext->m_ClientAddr.sin_addr), ntohs(pSocketContext->m_ClientAddr.sin_port));
					g_pServerEngine->AddServerMsgs(string(aa));

					// �ͷŵ���Ӧ����Դ
					pIOCPModel->_RemoveContext( pSocketContext );

 					continue;  

				}  else{
					switch( pIoContext->m_OpType )  {  
						//�ͻ�������
					case ACCEPT_POSTED:
						{
							pIoContext->m_nTotalBytes = dwBytesTransfered;
							pIOCPModel->_DoAccpet( pSocketContext, pIoContext);						
						}
						break;

						//��������
					case RECV_POSTED:
						{
							pIoContext->m_nTotalBytes	= dwBytesTransfered;
							pIOCPModel->_DoRecv( pSocketContext,pIoContext );
						}
						break;

						//��������
					case SEND_POSTED:
						{
							pIoContext->m_nSendBytes += dwBytesTransfered;
							if (pIoContext->m_nSendBytes < pIoContext->m_nTotalBytes){
								//����δ�ܷ����꣬������������
								pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer + pIoContext->m_nSendBytes;
								pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes - pIoContext->m_nSendBytes;
								pIOCPModel->PostWrite(pIoContext);
							}else{
								pIOCPModel->PostRecv(pIoContext);
							}
						}
						break;
					default:
						// ��Ӧ��ִ�е�����
						throw ("_WorkThread�е� pIoContext->m_OpType �����쳣.\n");
						break;
					}
				}
			}

		}

		sprintf_s(aa,"�������߳� %d ���˳�.\n",nThreadNo);
		g_pServerEngine->AddServerMsgs(string(aa));

		// �ͷ��̲߳���
		RELEASE(lpParam);	

		return 0;
	}

	//�������ܣ���ʼ���׽���
	bool CIOCPModel::LoadSocketLib()
	{    
		WSADATA wsaData;
		int nResult;
		nResult = WSAStartup(MAKEWORD(2,2), &wsaData);
		// ����(һ�㶼�����ܳ���)
		if (nResult != NO_ERROR ){
			g_pServerEngine->AddServerMsgs(string("��ʼ��WinSock 2.2ʧ�ܣ�\n"));
			return false; 
		}

		return true;
	}


	//�������ܣ�����������
	bool CIOCPModel::Start(){
		// ��ʼ���̻߳�����
		InitializeCriticalSection(&m_csContextList);

		// ����ϵͳ�˳����¼�֪ͨ
		m_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		// ��ʼ��IOCP
		if (_InitializeIOCP() == false){
			g_pServerEngine->AddServerMsgs(string("��ʼ��IOCPʧ�ܣ�\n"));
			return false;
		}else{
			g_pServerEngine->AddServerMsgs(string("\nIOCP��ʼ�����\n."));
		}

		// ��ʼ��Socket
		if( _InitializeListenSocket() == false ){
			g_pServerEngine->AddServerMsgs(string("Listen Socket��ʼ��ʧ�ܣ�\n"));
			this->_DeInitialize();
			return false;
		}else{
			g_pServerEngine->AddServerMsgs(string("Listen Socket��ʼ�����."));
		}

		g_pServerEngine->AddServerMsgs(string("ϵͳ׼���������Ⱥ�����....\n"));

		return true;
	}


	////////////////////////////////////////////////////////////////////
	//	��ʼ����ϵͳ�˳���Ϣ���˳���ɶ˿ں��߳���Դ
	void CIOCPModel::Stop(){
		if( m_pListenContext!=NULL && m_pListenContext->m_Socket!=INVALID_SOCKET ){
			// ����ر���Ϣ֪ͨ
			SetEvent(m_hShutdownEvent);

			for (int i = 0; i < m_nThreads; i++){
				// ֪ͨ���е���ɶ˿ڲ����˳�
				PostQueuedCompletionStatus(m_hIOCompletionPort, 0, (DWORD)EXIT_CODE, NULL);
			}

			// �ȴ����еĿͻ�����Դ�˳�
			WaitForMultipleObjects(m_nThreads, m_phWorkerThreads, TRUE, INFINITE);

			// ����ͻ����б���Ϣ
			this->_ClearContextList();

			// �ͷ�������Դ
			this->_DeInitialize();

			g_pServerEngine->AddServerMsgs(string("ֹͣ����\n"));
		}	
	}


	/*************************************************************
	*�������ܣ�Ͷ��WSARecv����
	*����������
	PER_IO_CONTEXT* pIoContext:	���ڽ���IO���׽����ϵĽṹ����ҪΪWSARecv������WSASend������
	**************************************************************/
	bool CIOCPModel::PostRecv( PER_IO_CONTEXT* pIoContext )
	{
		// ��ʼ������
		DWORD dwFlags = 0;
		DWORD dwBytes = 0;
		WSABUF *p_wbuf   = &pIoContext->m_wsaBuf;
		OVERLAPPED *p_ol = &pIoContext->m_Overlapped;

		pIoContext->ResetBuffer();
		pIoContext->m_OpType = RECV_POSTED;
		pIoContext->m_nSendBytes = 0;
		pIoContext->m_nTotalBytes= 0;

		// ��ʼ����ɺ󣬣�Ͷ��WSARecv����
		int nBytesRecv = WSARecv( pIoContext->m_sockAccept, p_wbuf, 1, &dwBytes, &dwFlags, p_ol, NULL );

		// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
		if ((nBytesRecv == SOCKET_ERROR ) && ( WSAGetLastError() != WSA_IO_PENDING)){
			g_pServerEngine->AddServerMsgs(string("Ͷ��һ��WSARecvʧ�ܣ�"));
			return false;
		}

		return true;
	}

	/*************************************************************
	*�������ܣ�Ͷ��WSASend����
	*����������
	PER_IO_CONTEXT* pIoContext:	���ڽ���IO���׽����ϵĽṹ����ҪΪWSARecv������WSASend����
	*����˵��������PostWrite֮ǰ��Ҫ����pIoContext��m_wsaBuf, m_nTotalBytes, m_nSendBytes��
	**************************************************************/
	bool CIOCPModel::PostWrite(PER_IO_CONTEXT* pIoContext)
	{
		// ��ʼ������
		DWORD dwFlags = 0;
		DWORD dwSendNumBytes = 0;
		WSABUF *p_wbuf   = &pIoContext->m_wsaBuf;
		OVERLAPPED *p_ol = &pIoContext->m_Overlapped;

		pIoContext->m_OpType = SEND_POSTED;

		//Ͷ��WSASend���� -- ��Ҫ�޸�
		int nRet = WSASend(pIoContext->m_sockAccept, &pIoContext->m_wsaBuf, 1, &dwSendNumBytes, dwFlags,
			&pIoContext->m_Overlapped, NULL);

		// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
		if (( nRet == SOCKET_ERROR) && ( WSAGetLastError() != WSA_IO_PENDING)){
			g_pServerEngine->AddServerMsgs(string("Ͷ��WSASendʧ�ܣ�"));
			return false;
		}
		return true;
	}


	////////////////////////////////
	// ��ʼ����ɶ˿�
	bool CIOCPModel::_InitializeIOCP()
	{
		// ������һ����ɶ˿�
		m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0 );
		if ( m_hIOCompletionPort == NULL ){
			char aa[256];
			sprintf_s(aa,"������ɶ˿�ʧ�ܣ��������: %d!\n", WSAGetLastError());
			g_pServerEngine->AddServerMsgs(string(aa));
			return false;
		}

		// ���ݱ����еĴ�����������������Ӧ���߳���
		m_nThreads = WORKER_THREADS_PER_PROCESSOR * _GetNoOfProcessors();
		
		// Ϊ�������̳߳�ʼ�����
		m_phWorkerThreads = new HANDLE[m_nThreads];
		
		// ���ݼ�����������������������߳�
		DWORD nThreadID;
		for (int i = 0; i < m_nThreads; i++){
			THREADPARAMS_WORKER* pThreadParams = new THREADPARAMS_WORKER;
			pThreadParams->pIOCPModel = this;
			pThreadParams->nThreadNo  = i+1;
			m_phWorkerThreads[i] = ::CreateThread(0, 0, _WorkerThread, (void *)pThreadParams, 0, &nThreadID);
		}

		//TRACE(" ���� _WorkerThread %d ��.\n", m_nThreads );
		char aa[256];
		sprintf_s(aa," ���� _WorkerThread %d ��.\n", m_nThreads);
		g_pServerEngine->AddServerMsgs(string(aa));

		return true;
	}


	/////////////////////////////////////////////////////////////////
	// ��ʼ��Socket
	bool CIOCPModel::_InitializeListenSocket()
	{
		// AcceptEx �� GetAcceptExSockaddrs ��GUID�����ڵ�������ָ��
		GUID GuidAcceptEx = WSAID_ACCEPTEX;  
		GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS; 

		// ��������ַ��Ϣ�����ڰ�Socket
		struct sockaddr_in ServerAddress;

		// �������ڼ�����Socket����Ϣ
		m_pListenContext = new PER_SOCKET_CONTEXT;

		// ��Ҫʹ���ص�IO�������ʹ��WSASocket������Socket���ſ���֧���ص�IO����
		m_pListenContext->m_Socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (INVALID_SOCKET == m_pListenContext->m_Socket) {
			char aa[256];
			sprintf_s(aa,"��ʼ��Socketʧ�ܣ��������: %d!\n", WSAGetLastError());
			g_pServerEngine->AddServerMsgs(string(aa));
			return false;
		}else{
			g_pServerEngine->AddServerMsgs(string("WSASocket() ���.\n"));
		}

		// ��Listen Socket������ɶ˿���
		if( NULL== CreateIoCompletionPort( (HANDLE)m_pListenContext->m_Socket, m_hIOCompletionPort,(DWORD)m_pListenContext, 0))  {  
			//this->_ShowMessage("�� Listen Socket����ɶ˿�ʧ�ܣ��������: %d/n", WSAGetLastError());  
			char aa[256];
			sprintf_s(aa,"�� Listen Socket����ɶ˿�ʧ�ܣ��������: %d!\n", WSAGetLastError());
			g_pServerEngine->AddServerMsgs(string(aa));

			RELEASE_SOCKET( m_pListenContext->m_Socket );
			return false;
		}else{
			//TRACE("Listen Socket����ɶ˿� ���.\n");
			g_pServerEngine->AddServerMsgs(string("Listen Socket����ɶ˿� ���.\n"));
		}

		// ����ַ��Ϣ
		ZeroMemory((char *)&ServerAddress, sizeof(ServerAddress));
		ServerAddress.sin_family = AF_INET;
		// ������԰��κο��õ�IP��ַ�����߰�һ��ָ����IP��ַ 
		//ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);                      
		ServerAddress.sin_addr.s_addr = inet_addr(m_strIP.c_str());         
		ServerAddress.sin_port = htons(m_nPort);                          

		// �󶨵�ַ�Ͷ˿�
		if (SOCKET_ERROR == bind(m_pListenContext->m_Socket, (struct sockaddr *) &ServerAddress, sizeof(ServerAddress))) {
			g_pServerEngine->AddServerMsgs(string("bind()����ִ�д���.\n"));
			return false;
		}else{
			g_pServerEngine->AddServerMsgs(string("bind() ���.\n"));
		}

		// ��ʼ���м���
		if (SOCKET_ERROR == listen(m_pListenContext->m_Socket,SOMAXCONN)){
			g_pServerEngine->AddServerMsgs(string("Listen()����ִ�г��ִ���.\n"));
			return false;
		}else{
			g_pServerEngine->AddServerMsgs(string("Listen() ���.\n"));
		}

		// ʹ��AcceptEx��������Ϊ���������WinSock2�淶֮���΢�������ṩ����չ����
		// ������Ҫ�����ȡһ�º�����ָ�룬
		// ��ȡAcceptEx����ָ��
		DWORD dwBytes = 0;  
		if(SOCKET_ERROR == WSAIoctl(
			m_pListenContext->m_Socket, 
			SIO_GET_EXTENSION_FUNCTION_POINTER, 
			&GuidAcceptEx, 
			sizeof(GuidAcceptEx), 
			&m_lpfnAcceptEx, 
			sizeof(m_lpfnAcceptEx), 
			&dwBytes, 
			NULL, 
			NULL))  
		{  
			char aa[256];
			sprintf_s(aa,"WSAIoctl δ�ܻ�ȡAcceptEx����ָ�롣�������: %d\n", WSAGetLastError());
			g_pServerEngine->AddServerMsgs(string(aa));
			this->_DeInitialize();
			return false;  
		}  

		// ��ȡGetAcceptExSockAddrs����ָ�룬Ҳ��ͬ��
		if(SOCKET_ERROR == WSAIoctl(
			m_pListenContext->m_Socket, 
			SIO_GET_EXTENSION_FUNCTION_POINTER, 
			&GuidGetAcceptExSockAddrs,
			sizeof(GuidGetAcceptExSockAddrs), 
			&m_lpfnGetAcceptExSockAddrs, 
			sizeof(m_lpfnGetAcceptExSockAddrs),   
			&dwBytes, 
			NULL, 
			NULL))  
		{  
			char aa[256];
			sprintf_s(aa,"WSAIoctl δ�ܻ�ȡGuidGetAcceptExSockAddrs����ָ�롣�������: %d\n", WSAGetLastError());
			g_pServerEngine->AddServerMsgs(string(aa));
			this->_DeInitialize();
			return false; 
		}  


		// ΪAcceptEx ׼��������Ȼ��Ͷ��AcceptEx I/O����
		//����10���׽��֣�Ͷ��AcceptEx���󣬼�����10���׽��ֽ���accept������
		for( int i=0;i<MAX_POST_ACCEPT;i++ ){
			// �½�һ��IO_CONTEXT
			PER_IO_CONTEXT* pAcceptIoContext = m_pListenContext->GetNewIoContext();
			if( false==this->_PostAccept( pAcceptIoContext ) ){
				m_pListenContext->RemoveContext(pAcceptIoContext);
				return false;
			}
		}

		//this->_ShowMessage( "Ͷ�� %d ��AcceptEx�������",MAX_POST_ACCEPT );
		char aa[256];
		sprintf_s(aa,"Ͷ�� %d ��AcceptEx�������",MAX_POST_ACCEPT);
		g_pServerEngine->AddServerMsgs(string(aa));

		return true;
	}

	////////////////////////////////////////////////////////////
	//	����ͷŵ�������Դ
	void CIOCPModel::_DeInitialize(){
		// ɾ���ͻ����б�Ļ�����
		DeleteCriticalSection(&m_csContextList);

		// �ر�ϵͳ�˳��¼����
		RELEASE_HANDLE(m_hShutdownEvent);

		// �ͷŹ������߳̾��ָ��
		for( int i=0;i<m_nThreads;i++ ){
			RELEASE_HANDLE(m_phWorkerThreads[i]);
		}
		
		RELEASE(m_phWorkerThreads);

		// �ر�IOCP���
		RELEASE_HANDLE(m_hIOCompletionPort);

		// �رռ���Socket
		RELEASE(m_pListenContext);

		g_pServerEngine->AddServerMsgs(string("�ͷ���Դ���.\n"));
	}


	//====================================================================================
	//
	//				    Ͷ����ɶ˿�����
	//
	//====================================================================================


	//////////////////////////////////////////////////////////////////
	// Ͷ��Accept����
	bool CIOCPModel::_PostAccept( PER_IO_CONTEXT* pAcceptIoContext )
	{
		//ASSERT( INVALID_SOCKET!=m_pListenContext->m_Socket );
		if(m_pListenContext->m_Socket == INVALID_SOCKET) {
			throw "_PostAccept,m_pListenContext->m_Socket != INVALID_SOCKET";
		}

		// ׼������
		DWORD dwBytes = 0;  
		pAcceptIoContext->m_OpType = ACCEPT_POSTED;  
		WSABUF *p_wbuf   = &pAcceptIoContext->m_wsaBuf;
		OVERLAPPED *p_ol = &pAcceptIoContext->m_Overlapped;
		
		// Ϊ�Ժ�������Ŀͻ�����׼����Socket( ������봫ͳaccept�������� ) 
		pAcceptIoContext->m_sockAccept  = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);  
		if( pAcceptIoContext->m_sockAccept == INVALID_SOCKET)  {  
			char aa[256];
			sprintf_s(aa,"��������Accept��Socketʧ�ܡ��������: %d\n", WSAGetLastError());
			g_pServerEngine->AddServerMsgs(string(aa));
			return false;  
		} 

		// Ͷ��AcceptEx
		if(FALSE == m_lpfnAcceptEx( m_pListenContext->m_Socket, pAcceptIoContext->m_sockAccept, p_wbuf->buf, p_wbuf->len - ((sizeof(SOCKADDR_IN)+16)*2),   
									sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16, &dwBytes, p_ol))  {  
			if(WSAGetLastError() != WSA_IO_PENDING)  {  
				char aa[256];
				sprintf_s(aa,"Ͷ�� AcceptEx ����ʧ�ܡ��������: %d\n", WSAGetLastError());
				g_pServerEngine->AddServerMsgs(string(aa));
				return false;  
			}  
		} 

		return true;
	}


	/********************************************************************
	*�������ܣ��������пͻ��˽��봦��
	*����˵����
	PER_SOCKET_CONTEXT* pSocketContext:	����accept������Ӧ���׽��֣����׽�������Ӧ�����ݽṹ��
	PER_IO_CONTEXT* pIoContext:			����accept������Ӧ�����ݽṹ��
	DWORD		dwIOSize:				���β�������ʵ�ʴ�����ֽ���
	********************************************************************/
	bool CIOCPModel::_DoAccpet( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext ){
		
		if (pIoContext->m_nTotalBytes > 0){
			//�ͻ�����ʱ����һ�ν���dwIOSize�ֽ�����
			_DoFirstRecvWithData(pIoContext);
		}else{
			//�ͻ��˽���ʱ��û�з������ݣ���Ͷ��WSARecv���󣬽�������
			_DoFirstRecvWithoutData(pIoContext);

		}

		// 5. ʹ�����֮�󣬰�Listen Socket���Ǹ�IoContext���ã�Ȼ��׼��Ͷ���µ�AcceptEx
		pIoContext->ResetBuffer();
		return this->_PostAccept( pIoContext ); 	
	}


	/////////////////////////////////////////////////////////////////
	//�������ܣ����н��յ����ݵ����ʱ�򣬽��д���
	bool CIOCPModel::_DoRecv( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext ){
		//������յ�����
		SOCKADDR_IN* ClientAddr = &pSocketContext->m_ClientAddr;
		char aa[256];
		sprintf_s(aa,"�յ�  %s:%d ��Ϣ��%s",inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),pIoContext->m_wsaBuf.buf );
		g_pServerEngine->AddServerMsgs(string(aa));

		//��������
		pIoContext->m_nSendBytes = 100;
		pIoContext->m_nTotalBytes= pIoContext->m_nTotalBytes;
		pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes;
		//pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer;
		pIoContext->m_wsaBuf.buf = "abababab";
		return PostWrite( pIoContext );
	}

	/*************************************************************
	*�������ܣ�AcceptEx���տͻ����ӳɹ������տͻ���һ�η��͵����ݣ���Ͷ��WSASend����
	*����������
	PER_IO_CONTEXT* pIoContext:	���ڼ����׽����ϵĲ���
	**************************************************************/
	bool CIOCPModel::_DoFirstRecvWithData(PER_IO_CONTEXT* pIoContext){
		SOCKADDR_IN* ClientAddr = NULL;
		SOCKADDR_IN* LocalAddr = NULL;  
		int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);  

		//1. ����ȡ������ͻ��˵ĵ�ַ��Ϣ
		this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.len - ((sizeof(SOCKADDR_IN)+16)*2),  
			sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16, (LPSOCKADDR*)&LocalAddr, &localLen, (LPSOCKADDR*)&ClientAddr, &remoteLen);  

		//��ʾ�ͻ�����Ϣ
		char aa[256];
		sprintf_s(aa,"�ͻ��� %s:%d ��Ϣ��%s.",inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),pIoContext->m_wsaBuf.buf);
		g_pServerEngine->AddServerMsgs(string(aa));


		//2.Ϊ�½�����׽Ӵ���PER_SOCKET_CONTEXT���������׽��ְ󶨵���ɶ˿�
		PER_SOCKET_CONTEXT* pNewSocketContext = new PER_SOCKET_CONTEXT;
		pNewSocketContext->m_Socket           = pIoContext->m_sockAccept;
		memcpy(&(pNewSocketContext->m_ClientAddr), ClientAddr, sizeof(SOCKADDR_IN));

		// ����������ϣ������Socket����ɶ˿ڰ�(��Ҳ��һ���ؼ�����)
		if( this->_AssociateWithIOCP( pNewSocketContext ) == false ){
			RELEASE( pNewSocketContext );
			return false;
		}  

		//3. �������������µ�IoContext�����������Socket��Ͷ�ݵ�һ��Recv��������
		PER_IO_CONTEXT* pNewIoContext = pNewSocketContext->GetNewIoContext();
		pNewIoContext->m_OpType       = SEND_POSTED;
		pNewIoContext->m_sockAccept   = pNewSocketContext->m_Socket;
		pNewIoContext->m_nTotalBytes  = pIoContext->m_nTotalBytes;
		pNewIoContext->m_nSendBytes   = 0;
		pNewIoContext->m_wsaBuf.len	  = pIoContext->m_nTotalBytes;
		memcpy(pNewIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.buf, pIoContext->m_nTotalBytes);	//�������ݵ�WSASend�����Ĳ���������

		//��ʱ�ǵ�һ�ν������ݳɹ�����������Ͷ��PostWrite����ͻ��˷�������
		if( this->PostWrite( pNewIoContext) == false ){
			pNewSocketContext->RemoveContext( pNewIoContext );
			return false;
		}

		//4. ���Ͷ�ݳɹ�����ô�Ͱ������Ч�Ŀͻ�����Ϣ�����뵽ContextList��ȥ(��Ҫͳһ���������ͷ���Դ)
		this->_AddToContextList( pNewSocketContext ); 

		return true;
	}

	/*************************************************************
	*�������ܣ�AcceptEx���տͻ����ӳɹ�����ʱ��δ���յ����ݣ���Ͷ��WSARecv����
	*����������
	PER_IO_CONTEXT* pIoContext:	���ڼ����׽����ϵĲ���
	**************************************************************/
	bool CIOCPModel::_DoFirstRecvWithoutData(PER_IO_CONTEXT* pIoContext )
	{
		//Ϊ�½�����׽��ִ���PER_SOCKET_CONTEXT�ṹ�����󶨵���ɶ˿�
		PER_SOCKET_CONTEXT* pNewSocketContext = new PER_SOCKET_CONTEXT;
		SOCKADDR_IN ClientAddr;
		int Len = sizeof(ClientAddr);

		getpeername(pIoContext->m_sockAccept, (sockaddr*)&ClientAddr, &Len);

		pNewSocketContext->m_Socket           = pIoContext->m_sockAccept;
		memcpy(&(pNewSocketContext->m_ClientAddr), &ClientAddr, sizeof(SOCKADDR_IN));
		
		//�����׽��ְ󶨵���ɶ˿�
		if( this->_AssociateWithIOCP( pNewSocketContext ) == false ){
			RELEASE( pNewSocketContext );
			return false;
		} 

		//Ͷ��WSARecv���󣬽�������
		PER_IO_CONTEXT* pNewIoContext = pNewSocketContext->GetNewIoContext();

		//��ʱ��AcceptExδ���յ��ͻ��˵�һ�η��͵����ݣ������������PostRecv���������Կͻ��˵�����
		if( this->PostRecv( pNewIoContext) == false){
			pNewSocketContext->RemoveContext( pNewIoContext );
			return false;
		}

		//���Ͷ�ݳɹ�����ô�Ͱ������Ч�Ŀͻ�����Ϣ�����뵽ContextList��ȥ(��Ҫͳһ���������ͷ���Դ)
		this->_AddToContextList( pNewSocketContext ); 

		return true;
	}


	/////////////////////////////////////////////////////
	// �����(Socket)�󶨵���ɶ˿���
	bool CIOCPModel::_AssociateWithIOCP( PER_SOCKET_CONTEXT *pContext ){
		// �����ںͿͻ���ͨ�ŵ�SOCKET�󶨵���ɶ˿���
		HANDLE hTemp = CreateIoCompletionPort((HANDLE)pContext->m_Socket, m_hIOCompletionPort, (DWORD)pContext, 0);

		if ( hTemp == NULL){
			char aa[256];
			sprintf_s(aa,"ִ��CreateIoCompletionPort()���ִ���.������롣�������: %d\n", GetLastError());
			g_pServerEngine->AddServerMsgs(string(aa));
			return false;
		}

		return true;
	}



	//////////////////////////////////////////////////////////////
	// ���ͻ��˵������Ϣ�洢��������
	void CIOCPModel::_AddToContextList( PER_SOCKET_CONTEXT *pHandleData ){
		EnterCriticalSection(&m_csContextList);
		m_arrayClientContext.push_back(pHandleData);	
		LeaveCriticalSection(&m_csContextList);
	}

	////////////////////////////////////////////////////////////////
	//	�Ƴ�ĳ���ض���Context
	void CIOCPModel::_RemoveContext( PER_SOCKET_CONTEXT *pSocketContext ){

		EnterCriticalSection(&m_csContextList);

		vector<PER_SOCKET_CONTEXT*>::iterator it  = m_arrayClientContext.begin();
		while(it != m_arrayClientContext.end()){
			PER_SOCKET_CONTEXT* p_obj = *it;
			if(pSocketContext == p_obj){
				delete pSocketContext;
				pSocketContext = NULL;
				it = m_arrayClientContext.erase(it);
				break;
			}

			it ++;
		}

		LeaveCriticalSection(&m_csContextList);
	}

	////////////////////////////////////////////////////////////////
	// ��տͻ�����Ϣ
	void CIOCPModel::_ClearContextList(){

		EnterCriticalSection(&m_csContextList);

		for( DWORD i=0;i<m_arrayClientContext.size();i++ ){
			delete m_arrayClientContext.at(i);
		}
		m_arrayClientContext.clear();

		LeaveCriticalSection(&m_csContextList);
	}



	////////////////////////////////////////////////////////////////////
	// ��ñ�����IP��ַ
	string CIOCPModel::GetLocalIP(){
		// ��ñ���������
		char hostname[MAX_PATH] = {0};
		gethostname(hostname,MAX_PATH);                
		struct hostent FAR* lpHostEnt = gethostbyname(hostname);
		if(lpHostEnt == NULL){
			return DEFAULT_IP;
		}

		// ȡ��IP��ַ�б��еĵ�һ��Ϊ���ص�IP(��Ϊһ̨�������ܻ�󶨶��IP)
		LPSTR lpAddr = lpHostEnt->h_addr_list[0];      

		// ��IP��ַת�����ַ�����ʽ
		struct in_addr inAddr;
		memmove(&inAddr,lpAddr,4);
		m_strIP = string( inet_ntoa(inAddr) );        

		return m_strIP;
	}

	///////////////////////////////////////////////////////////////////
	// ��ñ����д�����������
	int CIOCPModel::_GetNoOfProcessors(){
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		return si.dwNumberOfProcessors;
	}


	/////////////////////////////////////////////////////////////////////
	// �жϿͻ���Socket�Ƿ��Ѿ��Ͽ���������һ����Ч��Socket��Ͷ��WSARecv����������쳣
	// ʹ�õķ����ǳ��������socket�������ݣ��ж����socket���õķ���ֵ
	// ��Ϊ����ͻ��������쳣�Ͽ�(����ͻ��˱������߰ε����ߵ�)��ʱ�򣬷����������޷��յ��ͻ��˶Ͽ���֪ͨ��
	bool CIOCPModel::_IsSocketAlive(SOCKET s){
		int nByteSent=send(s,"",0,0);
		if (-1 == nByteSent) {
			return false;
		}else{
			return true;
		}
	}

	///////////////////////////////////////////////////////////////////
	//�������ܣ���ʾ��������ɶ˿��ϵĴ���
	bool CIOCPModel::HandleError( PER_SOCKET_CONTEXT *pContext,const DWORD& dwErr ){
		// ����ǳ�ʱ�ˣ����ټ����Ȱ�  
		if(dwErr == WAIT_TIMEOUT)  {  	
			// ȷ�Ͽͻ����Ƿ񻹻���...
			if( _IsSocketAlive( pContext->m_Socket) == 0 ){
				g_pServerEngine->AddServerMsgs( string("��⵽�ͻ����쳣�˳���" ));
				this->_RemoveContext( pContext );
				return true;
			}else{
				g_pServerEngine->AddServerMsgs(string("���������ʱ��������..." ));
				return true;
			}

		}  else if( dwErr == ERROR_NETNAME_DELETED ){
			// �����ǿͻ����쳣�˳���
			g_pServerEngine->AddServerMsgs(string("��⵽�ͻ����쳣�˳���" ));
			this->_RemoveContext( pContext );
			return true;
		}else{
			char aa[256];
			sprintf_s(aa,"��ɶ˿ڲ������ִ����߳��˳���������룺%d",dwErr);
			g_pServerEngine->AddServerMsgs(string(aa));
			return false;
		}
	}

}




