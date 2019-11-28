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

#define RELEASE_ARRAY(x) {if(x != nullptr ){delete[] x;x=nullptr;}} 
#define RELEASE_POINTER(x) {if(x != nullptr ){delete x;x=nullptr;}} 
#define RELEASE_HANDLE(x) {if(x != nullptr && x!=INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x = nullptr;}} // �ͷž����
#define RELEASE_SOCKET(x) {if(x != NULL && x !=INVALID_SOCKET) \
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
	HANDLE m_hIOCompletionPort; //��ɶ˿ڵľ��
	HANDLE m_hExitEvent; //�˳��߳��¼���Ϊ���ܹ����õ��˳�
	std::vector<HANDLE> m_hWorkerThreads; //�����߳̾���б�
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
	LPFN_ACCEPTEX m_lpfnAcceptEx; //acceptEx����ָ��
	ListenContext* m_pListenCtx; //����������
	CRITICAL_SECTION m_csClientList; //�����ͻ�������std::list<ClientContext*>
	std::list<ClientContext*> m_connectedClientList; //�����ӿͻ�������
	std::list<ClientContext*> m_freeClientList; //���е�ClientContext����

public:
	IocpServer(short listenPort = DEFAULT_PORT, int maxConnCount = MAX_CONN_COUNT);
	IocpServer& operator=(const IocpServer&) = delete;
	IocpServer(const IocpServer&) = delete;
	virtual ~IocpServer();
	
	bool Start(); // ����������	
	bool Stop(); //	ֹͣ������
	bool Send(ClientContext* pClientCtx, PBYTE pData, UINT len);

	// ��ȡ��ǰ������
	int GetConnectCount() { return m_nConnClientCnt; }
	// ��ȡ��ǰ�����˿�
	unsigned int GetPort() { return m_listenPort; }

protected:
	//����Ҫstatic _beginthreadex���ܷ���
	static unsigned WINAPI IocpWorkerThread(LPVOID arg);

	bool getAcceptExPtr();
	bool getAcceptExSockAddrs();
	bool setKeepAlive(ClientContext* pClientCtx, 
		LPOVERLAPPED lpOverlapped, int time = 1, int interval = 1);

	bool createListenSocket(short listenPort);
	bool createIocpWorker();
	bool exitIocpWorker();
	bool initAcceptIoContext(ListenContext* pListenContext);
	bool clearAcceptIoContext(ListenContext* pListenContext);

	bool postAccept(AcceptIoContext* pIoCtx);
	PostResult postRecv(ClientContext* pClientCtx);
	PostResult postSend(ClientContext* pClientCtx);

	bool handleAccept(LPOVERLAPPED lpOverlapped, DWORD dwBytesTransferred);
	bool handleRecv(ClientContext* pClientCtx, LPOVERLAPPED lpOverlapped,
		DWORD dwBytesTransferred);
	bool handleSend(ClientContext* pClientCtx, LPOVERLAPPED lpOverlapped,
		DWORD dwBytesTransferred);
	bool handleClose(ClientContext* pClientCtx);

	// Used to avoid access violation.
	void enterIoLoop(ClientContext* pClientCtx);
	int exitIoLoop(ClientContext* pClientCtx);

	void closeClientSocket(ClientContext* pClientCtx);

	//���������ӿͻ��������̰߳�ȫ
	void addClientCtx(ClientContext* pClientCtx);
	void removeClientCtx(ClientContext* pClientCtx);
	void removeAllClientCtxs();

	ClientContext* allocateClientCtx(SOCKET s);
	void releaseClientCtx(ClientContext* pClientCtx);

	void echo(ClientContext* pClientCtx);

	//�ص�����
	virtual void notifyNewConnection(ClientContext* pClientCtx);
	//virtual void notifyDisconnected(ClientContext* pClientCtx);
	virtual void notifyDisconnected(SOCKET s, Addr addr);
	virtual void notifyPackageReceived(ClientContext* pClientCtx);
	virtual void notifyWritePackage();
	virtual void notifyWriteCompleted();
	virtual void showMessage(const char* szFormat, ...);
};