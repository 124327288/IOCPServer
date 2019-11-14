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
#include <MSWSock.h>
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")

// ���������� (1024*8) ֮����Ϊʲô����8K��Ҳ��һ�������ϵľ���ֵ
// ���ȷʵ�ͻ��˷�����ÿ�����ݶ��Ƚ��٣���ô�����õ�СһЩ��ʡ�ڴ�
#define MAX_BUFFER_LEN 8192 
// Ĭ��IP��ַ
#define DEFAULT_IP _T("127.0.0.1")
// Ĭ�϶˿�
#define DEFAULT_PORT 10240 

/****************************************************************
BOOL WINAPI GetQueuedCompletionStatus(
__in HANDLE CompletionPort,
__out LPDWORD lpNumberOfBytes,
__out PULONG_PTR lpCompletionKey,
__out LPOVERLAPPED *lpOverlapped,
__in DWORD dwMilliseconds
);
lpCompletionKey [out] ��Ӧ��SocketContext�ṹ��
����CreateIoCompletionPort���׽��ֵ���ɶ˿�ʱ���룻
A pointer to a variable that receives the completion key value
associated with the file handle whose I/O operation has completed.
A completion key is a per-file key that is specified in a call
to CreateIoCompletionPort.

lpOverlapped [out] ��Ӧ��IoContext�ṹ��
�磺����accept����ʱ������AcceptEx����ʱ���룻
A pointer to a variable that receives the address of the OVERLAPPED structure
that was specified when the completed I/O operation was started.

****************************************************************/

// ����ɶ˿���Ͷ�ݵ�I/O����������
typedef enum _OPERATION_TYPE
{
	UNKNOWN, // ���ڳ�ʼ����������
	ACCEPT, // ��־Ͷ�ݵ�Accept����
	SEND, // ��־Ͷ�ݵ��Ƿ��Ͳ���
	RECV, // ��־Ͷ�ݵ��ǽ��ղ���
}OPERATION_TYPE;

//===============================================================================
//
//				��IO���ݽṹ�嶨��(����ÿһ���ص������Ĳ���)
//
//===============================================================================
//ÿ���׽��ֲ���(�磺AcceptEx, WSARecv, WSASend��)��Ӧ�����ݽṹ��
//OVERLAPPED�ṹ(��ʶ���β���)���������׽��֣����������������ͣ�
struct IoContext
{
	// ÿһ���ص�����������ص��ṹ
	OVERLAPPED m_Overlapped; //(���ÿһ��Socket��ÿһ����������Ҫ��һ��) 
	SOCKET m_sockAccept; // ������������ʹ�õ�Socket
	WSABUF m_wsaBuf; // WSA���͵Ļ����������ڸ��ص�������������
	char m_szBuffer[MAX_BUFFER_LEN]; // �����WSABUF�������ַ��Ļ�����
	OPERATION_TYPE m_OpType; // ��ʶ�������������(��Ӧ�����ö��)

	DWORD m_nTotalBytes; //�����ܵ��ֽ���
	DWORD m_nSendBytes;	//�Ѿ����͵��ֽ�������δ��������������Ϊ0

	//���캯��
	IoContext()
	{
		ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
		ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
		m_sockAccept = INVALID_SOCKET;
		m_wsaBuf.buf = m_szBuffer;
		m_wsaBuf.len = MAX_BUFFER_LEN;
		m_OpType = UNKNOWN;

		m_nTotalBytes = 0;
		m_nSendBytes = 0;
	}
	//��������
	~IoContext()
	{
		if (m_sockAccept != INVALID_SOCKET)
		{
			closesocket(m_sockAccept);
			m_sockAccept = INVALID_SOCKET;
		}
	}
	//���û���������
	void ResetBuffer()
	{
		ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
		m_wsaBuf.buf = m_szBuffer;
		m_wsaBuf.len = MAX_BUFFER_LEN;
	}
};

//=================================================================================
//
//				��������ݽṹ�嶨��(����ÿһ����ɶ˿ڣ�Ҳ����ÿһ��Socket�Ĳ���)
//
//=================================================================================
//ÿ��SOCKET��Ӧ�����ݽṹ(����GetQueuedCompletionStatus����)��-
//SOCKET����SOCKET��Ӧ�Ŀͻ��˵�ַ�������ڸ�SOCKET��������(��Ӧ�ṹIoContext)��
struct SocketContext
{
	SOCKET m_Socket; // ÿһ���ͻ������ӵ�Socket
	SOCKADDR_IN m_ClientAddr; // �ͻ��˵ĵ�ַ
	// �ͻ���������������������ݣ�
	// Ҳ����˵����ÿһ���ͻ���Socket���ǿ���������ͬʱͶ�ݶ��IO�����
	//�׽��ֲ�����������WSARecv��WSASend����һ��IoContext
	CArray<IoContext*> m_arrayIoContext;

	//���캯��
	SocketContext()
	{
		m_Socket = INVALID_SOCKET;
		memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
	}

	//��������
	~SocketContext()
	{
		if (m_Socket != INVALID_SOCKET)
		{
			closesocket(m_Socket);
			m_Socket = INVALID_SOCKET;
		}
		// �ͷŵ����е�IO����������
		for (int i = 0; i < m_arrayIoContext.GetCount(); i++)
		{
			delete m_arrayIoContext.GetAt(i);
		}
		m_arrayIoContext.RemoveAll();
	}

	//�����׽��ֲ���ʱ�����ô˺�������PER_IO_CONTEX�ṹ
	IoContext* GetNewIoContext()
	{
		IoContext* p = new IoContext;
		m_arrayIoContext.Add(p);
		return p;
	}

	// ���������Ƴ�һ��ָ����IoContext
	void RemoveContext(IoContext* pContext)
	{
		ASSERT(pContext != NULL);
		for (int i = 0; i < m_arrayIoContext.GetCount(); i++)
		{
			if (pContext == m_arrayIoContext.GetAt(i))
			{
				delete pContext;
				pContext = NULL;
				m_arrayIoContext.RemoveAt(i);
				break;
			}
		}
	}
};

//============================================================
//				CIocpModel�ඨ��
//============================================================
// �������̵߳��̲߳���
class CIocpModel;
struct WorkerThreadParam
{
	CIocpModel* pIocpModel; //��ָ�룬���ڵ������еĺ���
	int nThreadNo; //�̱߳��
} ;

class CIocpModel
{
public:
	CIocpModel(void);
	~CIocpModel(void);

public:
	// ����Socket��
	bool LoadSocketLib();
	// ж��Socket�⣬��������
	void UnloadSocketLib() { WSACleanup(); }
	// ����������
	bool Start();
	//	ֹͣ������
	void Stop();
	// ��ñ�����IP��ַ
	CString GetLocalIP();
	// ���ü����˿�
	void SetPort(const int& nPort) { m_nPort = nPort; }
	// �����������ָ�룬���ڵ�����ʾ��Ϣ��������
	void SetMainDlg(CDialog* p) { m_pMain = p; }
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
	//�̺߳�����ΪIOCP�������Ĺ������߳�
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);
	//��ñ����Ĵ���������
	int _GetNoOfProcessors();
	//�жϿͻ���Socket�Ƿ��Ѿ��Ͽ�
	bool _IsSocketAlive(SOCKET s);
	//������������ʾ��Ϣ
	void _ShowMessage(const CString szFormat, ...) const;

private:
	HANDLE m_hShutdownEvent; // ����֪ͨ�߳�ϵͳ�˳����¼���Ϊ���ܹ����õ��˳��߳�
	HANDLE m_hIOCompletionPort; // ��ɶ˿ڵľ��
	HANDLE* m_phWorkerThreads; // �������̵߳ľ��ָ��
	int m_nThreads; // ���ɵ��߳�����
	CString m_strIP; // �������˵�IP��ַ
	int m_nPort; // �������˵ļ����˿�
	CDialog* m_pMain; // ������Ľ���ָ�룬����������������ʾ��Ϣ
	CRITICAL_SECTION m_csContextList; // ����Worker�߳�ͬ���Ļ�����
	CArray<SocketContext*> m_arrayClientContext; // �ͻ���Socket��Context��Ϣ 
	SocketContext* m_pListenContext; // ���ڼ�����Socket��Context��Ϣ
	// AcceptEx �� GetAcceptExSockaddrs �ĺ���ָ�룬���ڵ�����������չ����
	LPFN_ACCEPTEX m_lpfnAcceptEx; 
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
};
