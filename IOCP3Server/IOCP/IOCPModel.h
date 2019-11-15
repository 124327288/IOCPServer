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

#define WORKER_THREADS_PER_PROCESSOR 2 // ÿһ���������ϲ������ٸ��߳�
#define MAX_POST_ACCEPT 10 // ͬʱͶ�ݵ�AcceptEx���������
#define EXIT_CODE NULL // ���ݸ�Worker�̵߳��˳��ź�
#define RELEASE_POINTER(x) {if(x != NULL ){delete x;x=NULL;}} // �ͷ�ָ���
#define RELEASE_HANDLE(x) {if(x != NULL && x!=INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x = NULL;}} // �ͷž����
#define RELEASE_SOCKET(x) {if(x != NULL && x !=INVALID_SOCKET) \
	{ closesocket(x);x=INVALID_SOCKET;}} // �ͷ�Socket��
#define DEFAULT_IP "127.0.0.1" //Ĭ��IP��ַ
#define DEFAULT_PORT 10240 //Ĭ�϶˿ں�

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
// �������̵߳��̲߳���
class CIocpModel;
struct WorkerThreadParam
{
	CIocpModel* pIocpModel; //��ָ�룬���ڵ������еĺ���
	int nThreadNo; //�̱߳��
};

class CIocpModel
{
private:
	HANDLE m_hShutdownEvent; // ����֪ͨ�̣߳�Ϊ���ܹ����õ��˳�
	HANDLE m_hIOCompletionPort; // ��ɶ˿ڵľ��
	HANDLE* m_phWorkerThreads; // �������̵߳ľ��ָ��
	int m_nThreads; // ���ɵ��߳�����
	string m_strIP; // �������˵�IP��ַ
	int m_nPort; // �������˵ļ����˿�
	CRITICAL_SECTION m_csContextList; // ����Worker�߳�ͬ���Ļ�����
	vector<SocketContext*> m_arrayClientContext; // �ͻ���Socket��Context��Ϣ 
	SocketContext* m_pListenContext; // ���ڼ�����Socket��Context��Ϣ
	// AcceptEx �� GetAcceptExSockaddrs �ĺ���ָ�룬���ڵ�����������չ����
	LPFN_ACCEPTEX m_lpfnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;

public:
	CIocpModel(void);
	~CIocpModel(void);

	// ����Socket��
	bool LoadSocketLib();
	// ж��Socket�⣬��������
	void UnloadSocketLib() { WSACleanup(); }
	// ����������
	bool Start();
	//	ֹͣ������
	void Stop();
	// ��ñ�����IP��ַ
	string GetLocalIP();
	// ���ü����˿�
	void SetPort(const int& nPort) { m_nPort = nPort; }
	//Ͷ��WSASend�����ڷ�������
	bool PostWrite(IoContext* pAcceptIoContext);
	//Ͷ��WSARecv���ڽ�������
	bool PostRecv(IoContext* pIoContext);

protected:
	// ��ʼ��IOCP
	bool _InitializeIOCP();
	// ��ʼ��Socket
	bool _InitializeListenSocket();
	// ����ͷ���Դ
	void _DeInitialize();
	//Ͷ��AcceptEx����
	bool _PostAccept(IoContext* pAcceptIoContext);
	//���пͻ��������ʱ�򣬽��д���
	bool _DoAccpet(SocketContext* pSocketContext, IoContext* pIoContext);
	//���ӳɹ�ʱ�����ݵ�һ���Ƿ���յ����Կͻ��˵����ݽ��е���
	bool _DoFirstRecvWithData(IoContext* pIoContext);
	bool _DoFirstRecvWithoutData(IoContext* pIoContext);
	//���ݵ����������pIoContext������
	bool _DoRecv(SocketContext* pSocketContext, IoContext* pIoContext);
	//���ͻ���socket�������Ϣ�洢��������
	void _AddToContextList(SocketContext* pSocketContext);
	//���ͻ���socket����Ϣ���������Ƴ�
	void _RemoveContext(SocketContext* pSocketContext);
	// ��տͻ���socket��Ϣ
	void _ClearContextList();
	// ������󶨵���ɶ˿���
	bool _AssociateWithIOCP(SocketContext* pContext);
	// ������ɶ˿��ϵĴ���
	bool HandleError(SocketContext* pContext, const DWORD& dwErr);
	//��ñ����Ĵ���������
	int _GetNoOfProcessors();
	//�жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
	bool _IsSocketAlive(SOCKET s);
	//�̺߳�����ΪIOCP�������Ĺ������߳�
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);
	//������������ʾ��Ϣ
	void _ShowMessage(const char* szFormat, ...) const;

public:
	typedef void (*fnAddInfo)(const string strInfo);
	void SetAddInfoFunc(fnAddInfo fn) { m_fnAddInfo = fn; }
protected:
	fnAddInfo m_fnAddInfo;
};
