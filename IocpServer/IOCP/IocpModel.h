/**********************************************************
ԭ�����ߣ�http://blog.csdn.net/piggyxp/article/details/6922277
�޸�ʱ�䣺2013��02��28��18:00:00
**********************************************************/
#pragma once
#include "SocketContext.h"
#include <vector>
#include <string>
using namespace std;

#define WORKER_THREADS_PER_PROCESSOR 2 // ÿһ���������ϲ������ٸ��߳�
#define MAX_POST_ACCEPT 10 // ͬʱͶ�ݵ�AcceptEx���������
#define EXIT_CODE NULL // ���ݸ�Worker�̵߳��˳��ź�
#define RELEASE_POINTER(x) {if(x != NULL ){delete x;x=NULL;}} // �ͷ�ָ���
#define RELEASE_HANDLE(x) {if(x != NULL && x!=INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x = NULL;}} // �ͷž����
#define RELEASE_SOCKET(x) {if(x !=INVALID_SOCKET) \
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
namespace MyServer
{
	// �������̵߳��̲߳���
	class CIocpModel;
	struct WorkerThreadParam
	{
		CIocpModel* pIOCPModel; //��ָ�룬���ڵ������еĺ���
		int nThreadNo; //�̱߳��
	};

	// CIOCPModel��
	class CIocpModel 
	{
	public:
		static HANDLE m_hMutexServerEngine;

	private:
		// ����֪ͨ�߳�ϵͳ�˳����¼���Ϊ���ܹ����õ��˳��߳�
		HANDLE m_hShutdownEvent; 
		HANDLE m_hIOCompletionPort; // ��ɶ˿ڵľ��
		HANDLE* m_phWorkerThreads; // �������̵߳ľ��ָ��
		int m_nThreads; // ���ɵ��߳�����
		string m_strIP; // �������˵�IP��ַ
		int m_nPort; // �������˵ļ����˿�

		// ����Worker�߳�ͬ���Ļ�����
		CRITICAL_SECTION m_csContextList;               
		// �ͻ���Socket��Context��Ϣ        
		vector<SocketContext*> m_arrayClientContext;          
		// ���ڼ�����Socket��Context��Ϣ
		SocketContext* m_pListenContext;              
		// AcceptEx �� GetAcceptExSockaddrs �ĺ���ָ�룬���ڵ�����������չ����
		LPFN_ACCEPTEX m_lpfnAcceptEx;                
		LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;

	public:
		CIocpModel(void);
		~CIocpModel(void);

	public:
		string GetLocalIP(); // ��ñ�����IP��ַ
		void SetPort(const int& nPort) // ���ü����˿�
		{
			m_nPort = nPort;
		}

	public:
		bool LoadSocketLib(); //����Socket��
		// ж��Socket�⣬��������
		void UnloadSocketLib() 
		{ WSACleanup(); }
		bool Start(); // ����������
		void Stop(); //	ֹͣ������
		//Ͷ��WSASend�����ڷ�������
		bool PostWrite(IoContext* pAcceptIoContext);
		//Ͷ��WSARecv���ڽ�������
		bool PostRecv(IoContext* pIoContext);

	protected:
		bool _InitializeIOCP(); // ��ʼ��IOCP
		bool _InitializeListenSocket(); // ��ʼ��Socket
		void _DeInitialize(); // ����ͷ���Դ
	 //Ͷ��AcceptEx����
		bool _PostAccept(IoContext* pAcceptIoContext);
		//���пͻ��������ʱ�򣬽��д���
		bool _DoAccpet(SocketContext* pSocketContext,
			IoContext* pIoContext);
		//���ӳɹ�ʱ�����ݵ�һ���Ƿ���յ����Կͻ��˵����ݽ��е���
		bool _DoFirstRecvWithData(IoContext* pIoContext);
		bool _DoFirstRecvWithoutData(IoContext* pIoContext);
		//���ݵ����������pIoContext������
		bool _DoRecv(SocketContext* pSocketContext,
			IoContext* pIoContext);
		//���ͻ���socket�������Ϣ�洢��������
		void _AddToContextList(SocketContext* pSocketContext);
		//���ͻ���socket����Ϣ���������Ƴ�
		void _RemoveContext(SocketContext* pSocketContext);
		void _ClearContextList(); // ��տͻ���socket��Ϣ
		// ������󶨵���ɶ˿���
		bool _AssociateWithIOCP(SocketContext* pContext);
		// ������ɶ˿��ϵĴ���
		bool HandleError(SocketContext* pContext, const DWORD& dwErr);
		bool _IsSocketAlive(SOCKET s); //�жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
		int _GetNoOfProcessors(); //��ñ����Ĵ���������

		//�̺߳�����ΪIOCP�������Ĺ������߳�
		static DWORD WINAPI _WorkerThread(LPVOID lpParam);
	};
}
