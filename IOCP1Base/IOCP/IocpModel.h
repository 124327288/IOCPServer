/*==========================================================================
Purpose:
	* 这个类CIocpModel是本代码的核心类，用于说明WinSock服务器端编程模型中的
	 完成端口(IOCP)的使用方法，并使用MFC对话框程序来调用这个类实现了基本的
	 服务器网络通信的功能。
	* 其中的IoContext结构体是封装了用于每一个重叠操作的参数
	 SocketContext 是封装了用于每一个Socket的参数，也就是用于每一个完成端口的参数
	* 详细的文档说明请参考 http://blog.csdn.net/PiggyXP
Notes:
	* 具体讲明了服务器端建立完成端口、建立工作者线程、投递Recv请求、
	 投递Accept请求的方法，所有的客户端连入的Socket都需要绑定到IOCP上，
	 所有从客户端发来的数据，都会实时显示到主界面中去。
==========================================================================*/
#pragma once
#include "PerSocketContext.h"
#include <vector>
#include <list>

#define WORKER_THREADS_PER_PROCESSOR 2 // 每一个处理器上产生多少个线程
#define MAX_LISTEN_SOCKET SOMAXCONN // 同时监听的SOCKET数量//SOMAXCONN
#define MAX_POST_ACCEPT 10 // 同时投递的AcceptEx请求的数量
#define EXIT_THREAD NULL // 传递给Worker线程的退出信号
#define DEFAULT_IP "127.0.0.1" //默认IP地址
#define DEFAULT_PORT 10240 //默认端口号

#define RELEASE_ARRAY(x) {if(x != nullptr ){delete[] x;x=nullptr;}} 
#define RELEASE_POINTER(x) {if(x != nullptr ){delete x;x=nullptr;}} 
#define RELEASE_HANDLE(x) {if(x != nullptr && x!=INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x = nullptr;}} // 释放句柄宏
#define RELEASE_SOCKET(x) {if(x != NULL && x !=INVALID_SOCKET) \
	{ closesocket(x);x=INVALID_SOCKET;}} // 释放Socket宏

/****************************************************************
BOOL WINAPI GetQueuedCompletionStatus(
__in   HANDLE CompletionPort,
__out  LPDWORD lpNumberOfBytes,
__out  PULONG_PTR lpCompletionKey,
__out  LPOVERLAPPED *lpOverlapped,
__in   DWORD dwMilliseconds
);
lpCompletionKey [out] 对应于SocketContext结构，
调用CreateIoCompletionPort绑定套接字到完成端口时传入；
A pointer to a variable that receives the completion key value
associated with the file handle whose I/O operation has completed.
A completion key is a per-file key that is specified
in a call to CreateIoCompletionPort.

lpOverlapped [out] 对应于IoContext结构，
如：进行accept操作时，调用AcceptEx函数时传入；
A pointer to a variable that receives the address of
the OVERLAPPED structure that was specified
when the completed I/O operation was started.
****************************************************************/
//============================================================
//				CIocpModel类定义
//============================================================
typedef void (*LOG_FUNC)(const string& strInfo);
// 工作者线程的线程参数
class IocpServer;
struct WorkerThreadParam
{
	IocpServer* pIocpModel; //类指针，用于调用类中的函数
	int nThreadNo; //线程编号
	int nThreadId; //线程ID
};

class IocpServer
{
private:
	bool m_bIsShutdown; //关闭时，退出工作线程
	short m_listenPort; //服务器开启的监听端口号
	LONG m_nMaxConnClientCnt; //最大客户端数量
	LONG m_nConnClientCnt; // 当前的连接数量
	LONG m_nWorkerCnt; // 生成的线程数量
	HANDLE m_hIOCompletionPort; // 完成端口的句柄
	HANDLE m_hExitEvent; // 用来通知线程，为了能够更好的退出
	std::vector<HANDLE> m_hWorkerThreads; //工作线程句柄列表
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
	LPFN_ACCEPTEX m_lpfnAcceptEx;
	SocketContext* m_pListenCtx; // 用于监听的Socket的Context信息
	CRITICAL_SECTION m_csClientList; // 用于Worker线程同步的互斥量
	//std::list<ClientContext*> m_connectedClientList; //已连接客户端链表
	//std::list<ClientContext*> m_freeClientList; //空闲的ClientContext链表
	vector<SocketContext*> m_arrayClientContext; // 客户端Socket的Context信息 
	LONG acceptPostCount; // 当前投递的的Accept数量
	LONG errorCount; // 当前的错误数量
	string m_strIP; // 服务器端的IP地址


public:
	IocpServer(short listenPort = DEFAULT_PORT, int maxConnectionCount = 10000);
	~IocpServer(void);

	// 加载Socket库
	bool LoadSocketLib();
	// 卸载Socket库，彻底完事
	void UnloadSocketLib() noexcept
	{
		WSACleanup();
	}
	// 启动服务器
	bool Start();
	//	停止服务器
	void Stop();
	// 获得本机的IP地址
	string GetLocalIP();

	// 向指定客户端发送数据
	bool SendData(SocketContext* pSoContext, char* data, int size);
	bool SendData(SocketContext* pSoContext, IoContext* pIoContext);
	// 继续接收指定客户端的数据
	bool RecvData(SocketContext* pSoContext, IoContext* pIoContext);

	// 获取当前连接数
	int GetConnectCount() { return m_nConnClientCnt; }
	// 获取当前监听端口
	unsigned int GetPort() { return m_listenPort; }

	// 事件通知函数(派生类重载此族函数)
	virtual void OnConnectionAccepted(SocketContext* pSoContext){};
	virtual void OnConnectionClosed(SocketContext* pSoContext) {};
	virtual void OnConnectionError(SocketContext* pSoContext, int error) {};
	virtual void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext) 
	{
		SendData(pSoContext, pIoContext); // 接收数据完成，原封不动发回去
	};
	virtual void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext) 
	{
		RecvData(pSoContext, pIoContext); // 发送数据完成，继续接收数据
	};

protected:
	// 初始化IOCP
	bool _InitializeIOCP();
	// 初始化Socket
	bool _InitializeListenSocket();
	// 最后释放资源
	void _DeInitialize();
	//投递AcceptEx请求
	bool _PostAccept(IoContext* pIoContext);
	//在有客户端连入的时候，进行处理
	bool _DoAccept(SocketContext* pSoContext, IoContext* pIoContext);
	//连接成功时，根据第一次是否接收到来自客户端的数据进行调用
	bool _DoFirstRecvWithData(IoContext* pIoContext);
	bool _DoFirstRecvWithoutData(IoContext* pIoContext);
	//投递WSARecv用于接收数据
	bool _PostRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//数据到达，数组存放在pIoContext参数中
	bool _DoRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//投递WSASend，用于发送数据
	bool _PostSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoClose(SocketContext* pSoContext);
	//将客户端socket的相关信息存储到数组中
	void _AddToContextList(SocketContext* pSoContext);
	//将客户端socket的信息从数组中移除
	void _RemoveContext(SocketContext* pSoContext);
	// 清空客户端socket信息
	void _ClearContextList();
	// 将句柄绑定到完成端口中
	bool _AssociateWithIOCP(SocketContext* pSoContext);
	// 处理完成端口上的错误
	bool HandleError(SocketContext* pSoContext, const DWORD& dwErr);
	//获得本机的处理器数量
	int _GetNumOfProcessors() noexcept;
	//判断客户端Socket是否已经断开
	bool _IsSocketAlive(SOCKET s) noexcept;
	//线程函数，为IOCP请求服务的工作者线程
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);
	//在主界面中显示信息
	virtual void _ShowMessage(const char* szFormat, ...);

public:
	void SetLogFunc(LOG_FUNC fn) { m_LogFunc = fn; }
protected:
	LOG_FUNC m_LogFunc;
};
