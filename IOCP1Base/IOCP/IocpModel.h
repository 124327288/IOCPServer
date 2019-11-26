/*==========================================================================
Purpose:
	* �����CIocpModel�Ǳ�����ĺ����࣬����˵��WinSock�������˱��ģ���е�
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
#include "PerSocketContext.h"
#include <vector>
#include <list>

#define WORKER_THREADS_PER_PROCESSOR 2 // ÿһ���������ϲ������ٸ��߳�
#define MAX_LISTEN_SOCKET SOMAXCONN // ͬʱ������SOCKET����//SOMAXCONN
#define MAX_POST_ACCEPT 10 // ͬʱͶ�ݵ�AcceptEx���������
#define EXIT_THREAD NULL // ���ݸ�Worker�̵߳��˳��ź�
#define DEFAULT_IP "127.0.0.1" //Ĭ��IP��ַ
#define DEFAULT_PORT 10240 //Ĭ�϶˿ں�

#define RELEASE_ARRAY(x) {if(x != nullptr ){delete[] x;x=nullptr;}} 
#define RELEASE_POINTER(x) {if(x != nullptr ){delete x;x=nullptr;}} 
#define RELEASE_HANDLE(x) {if(x != nullptr && x!=INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x = nullptr;}} // �ͷž����
#define RELEASE_SOCKET(x) {if(x != NULL && x !=INVALID_SOCKET) \
	{ closesocket(x);x=INVALID_SOCKET;}} // �ͷ�Socket��

/****************************************************************
BOOL WINAPI GetQueuedCompletionStatus(
__in   HANDLE CompletionPort,
__out  LPDWORD lpNumberOfBytes,
__out  PULONG_PTR lpCompletionKey,
__out  LPOVERLAPPED *lpOverlapped,
__in   DWORD dwMilliseconds
);
lpCompletionKey [out] ��Ӧ��SocketContext�ṹ��
����CreateIoCompletionPort���׽��ֵ���ɶ˿�ʱ���룻
A pointer to a variable that receives the completion key value
associated with the file handle whose I/O operation has completed.
A completion key is a per-file key that is specified
in a call to CreateIoCompletionPort.

lpOverlapped [out] ��Ӧ��IoContext�ṹ��
�磺����accept����ʱ������AcceptEx����ʱ���룻
A pointer to a variable that receives the address of
the OVERLAPPED structure that was specified
when the completed I/O operation was started.
****************************************************************/
//============================================================
//				CIocpModel�ඨ��
//============================================================
typedef void (*LOG_FUNC)(const string& strInfo);
// �������̵߳��̲߳���
class IocpServer;
struct WorkerThreadParam
{
	IocpServer* pIocpModel; //��ָ�룬���ڵ������еĺ���
	int nThreadNo; //�̱߳��
	int nThreadId; //�߳�ID
};

class IocpServer
{
private:
	bool m_bIsShutdown; //�ر�ʱ���˳������߳�
	short m_listenPort; //�����������ļ����˿ں�
	LONG m_nMaxConnClientCnt; //���ͻ�������
	LONG m_nConnClientCnt; // ��ǰ����������
	LONG m_nWorkerCnt; // ���ɵ��߳�����
	HANDLE m_hIOCompletionPort; // ��ɶ˿ڵľ��
	HANDLE m_hExitEvent; // ����֪ͨ�̣߳�Ϊ���ܹ����õ��˳�
	std::vector<HANDLE> m_hWorkerThreads; //�����߳̾���б�
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
	LPFN_ACCEPTEX m_lpfnAcceptEx;
	SocketContext* m_pListenCtx; // ���ڼ�����Socket��Context��Ϣ
	CRITICAL_SECTION m_csClientList; // ����Worker�߳�ͬ���Ļ�����
	//std::list<ClientContext*> m_connectedClientList; //�����ӿͻ�������
	//std::list<ClientContext*> m_freeClientList; //���е�ClientContext����
	vector<SocketContext*> m_arrayClientContext; // �ͻ���Socket��Context��Ϣ 
	LONG acceptPostCount; // ��ǰͶ�ݵĵ�Accept����
	LONG errorCount; // ��ǰ�Ĵ�������
	string m_strIP; // �������˵�IP��ַ


public:
	IocpServer(short listenPort = DEFAULT_PORT, int maxConnectionCount = 10000);
	~IocpServer(void);

	// ����Socket��
	bool LoadSocketLib();
	// ж��Socket�⣬��������
	void UnloadSocketLib() noexcept
	{
		WSACleanup();
	}
	// ����������
	bool Start();
	//	ֹͣ������
	void Stop();
	// ��ñ�����IP��ַ
	string GetLocalIP();

	// ��ָ���ͻ��˷�������
	bool SendData(SocketContext* pSoContext, char* data, int size);
	bool SendData(SocketContext* pSoContext, IoContext* pIoContext);
	// ��������ָ���ͻ��˵�����
	bool RecvData(SocketContext* pSoContext, IoContext* pIoContext);

	// ��ȡ��ǰ������
	int GetConnectCount() { return m_nConnClientCnt; }
	// ��ȡ��ǰ�����˿�
	unsigned int GetPort() { return m_listenPort; }

	// �¼�֪ͨ����(���������ش��庯��)
	virtual void OnConnectionAccepted(SocketContext* pSoContext){};
	virtual void OnConnectionClosed(SocketContext* pSoContext) {};
	virtual void OnConnectionError(SocketContext* pSoContext, int error) {};
	virtual void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext) 
	{
		SendData(pSoContext, pIoContext); // ����������ɣ�ԭ�ⲻ������ȥ
	};
	virtual void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext) 
	{
		RecvData(pSoContext, pIoContext); // ����������ɣ�������������
	};

protected:
	// ��ʼ��IOCP
	bool _InitializeIOCP();
	// ��ʼ��Socket
	bool _InitializeListenSocket();
	// ����ͷ���Դ
	void _DeInitialize();
	//Ͷ��AcceptEx����
	bool _PostAccept(IoContext* pIoContext);
	//���пͻ��������ʱ�򣬽��д���
	bool _DoAccept(SocketContext* pSoContext, IoContext* pIoContext);
	//���ӳɹ�ʱ�����ݵ�һ���Ƿ���յ����Կͻ��˵����ݽ��е���
	bool _DoFirstRecvWithData(IoContext* pIoContext);
	bool _DoFirstRecvWithoutData(IoContext* pIoContext);
	//Ͷ��WSARecv���ڽ�������
	bool _PostRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//���ݵ����������pIoContext������
	bool _DoRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//Ͷ��WSASend�����ڷ�������
	bool _PostSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoClose(SocketContext* pSoContext);
	//���ͻ���socket�������Ϣ�洢��������
	void _AddToContextList(SocketContext* pSoContext);
	//���ͻ���socket����Ϣ���������Ƴ�
	void _RemoveContext(SocketContext* pSoContext);
	// ��տͻ���socket��Ϣ
	void _ClearContextList();
	// ������󶨵���ɶ˿���
	bool _AssociateWithIOCP(SocketContext* pSoContext);
	// ������ɶ˿��ϵĴ���
	bool HandleError(SocketContext* pSoContext, const DWORD& dwErr);
	//��ñ����Ĵ���������
	int _GetNumOfProcessors() noexcept;
	//�жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
	bool _IsSocketAlive(SOCKET s) noexcept;
	//�̺߳�����ΪIOCP�������Ĺ������߳�
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);
	//������������ʾ��Ϣ
	virtual void _ShowMessage(const char* szFormat, ...);

public:
	void SetLogFunc(LOG_FUNC fn) { m_LogFunc = fn; }
protected:
	LOG_FUNC m_LogFunc;
};
