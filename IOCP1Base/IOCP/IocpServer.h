/*==========================================================================
Purpose:
	* ������Ǳ�����ĺ����࣬����˵��WinSock�������˱��ģ���е�
	 ��ɶ˿�(IOCP)��ʹ�÷�������ʹ��MFC�Ի�����������������ʵ���˻�����
	 ����������ͨ�ŵĹ��ܡ�
	* ���е�IoContext�ṹ���Ƿ�װ������ÿһ���ص������Ĳ���
	 SocketContext �Ƿ�װ������ÿһ��Socket�Ĳ�����Ҳ��������ÿһ����ɶ˿ڵĲ���
	* ��ϸ���ĵ�˵����ο� http://blog.csdn.net/PiggyXP
Notes:
	* ���彲���˷������˽�����ɶ˿ڡ������������̡߳�Ͷ��Recv����
	 Ͷ��Accept����ķ��������еĿͻ��������Socket����Ҫ�󶨵�IOCP�ϣ�
	 ���дӿͻ��˷��������ݣ�����ʵʱ��ʾ����������ȥ��
==========================================================================*/
#pragma once
#include "Addr.h"
#include "PerSocketContext.h"
#include <mswsock.h>
#include <vector>
#include <list>

constexpr int EXIT_THREAD = 0; //�����߳��˳���־
constexpr int MAX_POST_ACCEPT = 10; //���Ͷ��AcceptEx���������
constexpr int MAX_LISTEN_SOCKET = SOMAXCONN; // ͬʱ������SOCKET����
constexpr int WORKER_THREADS_PER_PROCESSOR= 2; // CPUÿ�˵��߳���
constexpr int MAX_CONN_COUNT = 100000; //��󲢷�������
constexpr int DEFAULT_PORT = 10240; //Ĭ�϶˿ں�

//#define RELEASE_ARRAY(x) {if(x != nullptr ){delete[] x;x=nullptr;}} 
#define RELEASE_POINTER(x) {if(x != nullptr ){delete x;x=nullptr;}} 
#define RELEASE_HANDLE(x) {if(x != nullptr && x!=INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x = INVALID_HANDLE_VALUE;}} // �ͷž����
#define RELEASE_SOCKET(x) {if(x != nullptr && x !=INVALID_SOCKET) \
	{ closesocket(x);x=INVALID_SOCKET;}} // �ͷ�Socket��

//============================================================
//				IocpServer�ඨ��
//============================================================
class IocpServer
{
private:
	CRITICAL_SECTION m_csLog; // ����Worker�߳�ͬ���Ļ�����
	bool m_bIsShutdown; //�ر�ʱ���˳������߳�
	short m_listenPort; //�����������ļ����˿ں�
	LONG m_nMaxConnClientCnt; //���ͻ�������
	LONG m_nConnClientCnt; //�����ӿͻ�������
	LONG m_nWorkerCnt; //IO�����߳�����
	HANDLE m_hIOCP; //��ɶ˿ڵľ��
	HANDLE m_hExitEvent; //�˳��߳��¼���Ϊ���ܹ����õ��˳�
	std::vector<HANDLE> m_hWorkerThreads; //�����߳̾���б�
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
	LPFN_ACCEPTEX m_lpfnAcceptEx; //acceptEx����ָ��
	ListenContext* m_pListenCtx; // ���ڼ�����Socket��Context��Ϣ
	CRITICAL_SECTION m_csClientList; // ����Worker�߳�ͬ���Ļ�����
	std::list<ClientContext*> m_connectedClientList; //�����ӿͻ�������
	std::list<ClientContext*> m_freeClientList; //���е�ClientContext����
	//vector<SocketContext*> m_arrayClientContext; // �ͻ���Socket��Context��Ϣ 
	LONG acceptPostCount; // ��ǰͶ�ݵĵ�Accept����
	LONG errorCount; // ��ǰ�Ĵ�������

public:
	IocpServer(short listenPort = DEFAULT_PORT, int maxConnCount = MAX_CONN_COUNT);
	IocpServer& operator=(const IocpServer&) = delete;
	IocpServer(const IocpServer&) = delete;
	virtual ~IocpServer();
	
	bool Start(); // ����������	
	bool Stop(); //	ֹͣ������

	// ��ָ���ͻ��˷�������
	bool SendData(ClientContext* pClientCtx, PBYTE pData, UINT len);

	// ��ȡ��ǰ������
	int GetConnectCount() { return m_nConnClientCnt; }
	// ��ȡ��ǰ�����˿�
	unsigned int GetPort() { return m_listenPort; }

protected:
	bool initSocket(short listenPort);  // ��ʼ��Socket	
	bool initIOCP(ListenContext * pListenCtx);// ��ʼ��IOCP
	bool initIocpWorker(); // �����������߳�
	bool exitIocpWorker(); // �˳��������߳�
	void deinitialize(); // �ͷ���Դ	
		
	// Used to avoid access violation.
	void enterIoLoop(SocketContext* pSocketCtx);
	int exitIoLoop(SocketContext* pSocketCtx);
	//Ͷ��AcceptEx��WSARecv��WSASend����
	bool postAccept(AcceptIoContext* pIoCtx);
	bool postRecv(ClientContext* pClientCtx, IoContext* pIoContext);
	bool postSend(ClientContext* pClientCtx, IoContext* pIoContext);
	//���пͻ��������ʱ�򣬽��д��� // ������ɶ˿��ϵĴ���
	bool handleError(ClientContext* pClientCtx, const DWORD& dwErr);
	bool handleAccept(ClientContext* pClientCtx, IoContext* pIoContext);
	bool handleRecv(ClientContext* pClientCtx, IoContext* pIoContext);
	bool handleSend(ClientContext* pClientCtx, IoContext* pIoContext);
	bool handleClose(ClientContext* pClientCtx);
	//���ͻ���socket�������Ϣ�洢��������
	void _AddToContextList(SocketContext* pSoContext);
	//���ͻ���socket����Ϣ���������Ƴ�
	void _RemoveContext(SocketContext* pSoContext);
	// ��տͻ���socket��Ϣ
	void _ClearContextList();

	bool setKeepAlive(ClientContext* pClientCtx, 
		LPOVERLAPPED lpOverlapped, int time = 1, int interval = 1);
	//�жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
	bool isSocketAlive(SOCKET s) noexcept;
	void echo(ClientContext* pClientCtx);

	//�̺߳�����ΪIOCP�������Ĺ������߳�
	static DWORD WINAPI iocpWorkerThread(LPVOID lpParam);
	
	//������������ʾ��Ϣ
	virtual void showMessage(const char* szFormat, ...);

	// �¼�֪ͨ����(���������ش��庯��)
	virtual void OnConnectionAccepted(ClientContext* pClientCtx);
	virtual void OnConnectionClosed(ClientContext* pClientCtx);
	virtual void OnConnectionClosed(SOCKET s, Addr addr);
	virtual void OnConnectionError(ClientContext* pClientCtx, int error);
	virtual void OnRecvCompleted(ClientContext* pClientCtx);
	virtual void OnSendCompleted(ClientContext* pClientCtx);
};