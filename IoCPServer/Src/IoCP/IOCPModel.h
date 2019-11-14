/**********************************************************
ԭ�����ߣ�http://blog.csdn.net/piggyxp/article/details/6922277
�޸�ʱ�䣺2013��02��28��18:00:00
**********************************************************/
#pragma once
#include <winsock2.h>
#include <MSWSock.h>
#include <vector>
#include <string>

#include "PER_SOCKET_CONTEXT.h"

using namespace std;

#define WORKER_THREADS_PER_PROCESSOR 2	// ÿһ���������ϲ������ٸ��߳�
#define MAX_POST_ACCEPT              10	// ͬʱͶ�ݵ�AcceptEx���������
#define EXIT_CODE                    NULL	// ���ݸ�Worker�̵߳��˳��ź�
#define RELEASE(x)                      {if(x != NULL ){delete x;x=NULL;}}	// �ͷ�ָ���
#define RELEASE_HANDLE(x)               {if(x != NULL && x!=INVALID_HANDLE_VALUE){ CloseHandle(x);x = NULL;}}	// �ͷž����
#define RELEASE_SOCKET(x)               {if(x !=INVALID_SOCKET) { closesocket(x);x=INVALID_SOCKET;}}	// �ͷ�Socket��
#define DEFAULT_IP            "127.0.0.1"	//Ĭ��IP��ַ

/****************************************************************
BOOL WINAPI GetQueuedCompletionStatus(
__in   HANDLE CompletionPort,
__out  LPDWORD lpNumberOfBytes,
__out  PULONG_PTR lpCompletionKey,
__out  LPOVERLAPPED *lpOverlapped,
__in   DWORD dwMilliseconds
);
lpCompletionKey [out] ��Ӧ��PER_SOCKET_CONTEXT�ṹ������CreateIoCompletionPort���׽��ֵ���ɶ˿�ʱ���룻
A pointer to a variable that receives the completion key value associated with the file handle whose I/O operation has completed. 
A completion key is a per-file key that is specified in a call to CreateIoCompletionPort. 

lpOverlapped [out] ��Ӧ��PER_IO_CONTEXT�ṹ���磺����accept����ʱ������AcceptEx����ʱ���룻
A pointer to a variable that receives the address of the OVERLAPPED structure that was specified when the completed I/O operation was started. 

****************************************************************/
namespace MyServer{

	// �������̵߳��̲߳���
	class CIOCPModel;
	typedef struct _tagThreadParams_WORKER{
		CIOCPModel* pIOCPModel;                                   //��ָ�룬���ڵ������еĺ���
		int         nThreadNo;                                    //�̱߳��
	} THREADPARAMS_WORKER,*PTHREADPARAM_WORKER; 

	// CIOCPModel��
	class CIOCPModel{
	public:
		static HANDLE m_hMutexServerEngine;

	private:
		HANDLE                       m_hShutdownEvent;              // ����֪ͨ�߳�ϵͳ�˳����¼���Ϊ���ܹ����õ��˳��߳�
		HANDLE                       m_hIOCompletionPort;           // ��ɶ˿ڵľ��
		HANDLE*                      m_phWorkerThreads;             // �������̵߳ľ��ָ��
		int		                     m_nThreads;                    // ���ɵ��߳�����
		string						m_strIP;                       // �������˵�IP��ַ
		int                          m_nPort;                       // �������˵ļ����˿�

		CRITICAL_SECTION             m_csContextList;               // ����Worker�߳�ͬ���Ļ�����
		vector<PER_SOCKET_CONTEXT*>  m_arrayClientContext;          // �ͻ���Socket��Context��Ϣ        
		PER_SOCKET_CONTEXT*          m_pListenContext;              // ���ڼ�����Socket��Context��Ϣ
		LPFN_ACCEPTEX                m_lpfnAcceptEx;                // AcceptEx �� GetAcceptExSockaddrs �ĺ���ָ�룬���ڵ�����������չ����
		LPFN_GETACCEPTEXSOCKADDRS    m_lpfnGetAcceptExSockAddrs; 

	public:
		CIOCPModel(void);
		~CIOCPModel(void);

	public:
		string GetLocalIP();	// ��ñ�����IP��ַ
		void SetPort( const int& nPort ) { m_nPort = nPort; }// ���ü����˿�

	public:
		bool LoadSocketLib();	//����Socket��
		void UnloadSocketLib() { WSACleanup(); }	// ж��Socket�⣬��������
		bool Start();	// ����������
		void Stop();	//	ֹͣ������
		bool PostWrite( PER_IO_CONTEXT* pAcceptIoContext ); //Ͷ��WSASend�����ڷ�������
		bool PostRecv( PER_IO_CONTEXT* pIoContext );//Ͷ��WSARecv���ڽ�������

	protected:
		
		bool _InitializeIOCP();// ��ʼ��IOCP
		bool _InitializeListenSocket();// ��ʼ��Socket
		void _DeInitialize();// ����ͷ���Դ
		bool _PostAccept( PER_IO_CONTEXT* pAcceptIoContext ); //Ͷ��AcceptEx����
		bool _DoAccpet( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext );//���пͻ��������ʱ�򣬽��д���
		bool _DoFirstRecvWithData(PER_IO_CONTEXT* pIoContext );//���ӳɹ�ʱ�����ݵ�һ���Ƿ���յ����Կͻ��˵����ݽ��е���
		bool _DoFirstRecvWithoutData(PER_IO_CONTEXT* pIoContext );
		bool _DoRecv( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext );//���ݵ����������pIoContext������
		void _AddToContextList( PER_SOCKET_CONTEXT *pSocketContext );//���ͻ���socket�������Ϣ�洢��������
		void _RemoveContext( PER_SOCKET_CONTEXT *pSocketContext );//���ͻ���socket����Ϣ���������Ƴ�
		void _ClearContextList();// ��տͻ���socket��Ϣ
		bool _AssociateWithIOCP( PER_SOCKET_CONTEXT *pContext);// ������󶨵���ɶ˿���
		bool HandleError( PER_SOCKET_CONTEXT *pContext,const DWORD& dwErr );// ������ɶ˿��ϵĴ���
		bool _IsSocketAlive(SOCKET s);//�жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
		int _GetNoOfProcessors();//��ñ����Ĵ���������

		static DWORD WINAPI _WorkerThread(LPVOID lpParam);//�̺߳�����ΪIOCP�������Ĺ������߳�
	};
}
