/*==========================================================================
Purpose:
	* 这个类是本代码的核心类，用于说明WinSock服务器端编程模型中的
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
#include "Addr.h"
#include "PerSocketContext.h"
#include <mswsock.h>
#include <vector>
#include <list>

constexpr int EXIT_THREAD = 0; //工作线程退出标志
constexpr int MAX_POST_ACCEPT = 10; //最大投递AcceptEx请求的数量
constexpr int MAX_LISTEN_SOCKET = SOMAXCONN; // 同时监听的SOCKET数量
constexpr int WORKER_THREADS_PER_PROCESSOR= 2; // CPU每核的线程数
constexpr int MAX_CONN_COUNT = 100000; //最大并发连接数
constexpr int DEFAULT_PORT = 10240; //默认端口号

#define RELEASE_ARRAY(x) {if(x != nullptr ){delete[] x;x=nullptr;}} 
#define RELEASE_POINTER(x) {if(x != nullptr ){delete x;x=nullptr;}} 
#define RELEASE_HANDLE(x) {if(x != nullptr && x!=INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x = nullptr;}} // 释放句柄宏
#define RELEASE_SOCKET(x) {if(x != NULL && x !=INVALID_SOCKET) \
	{ closesocket(x);x=INVALID_SOCKET;}} // 释放Socket宏

//============================================================
//				IocpServer类定义
//============================================================
class IocpServer
{
private:
	CRITICAL_SECTION m_csLog; // 用于Worker线程同步的互斥量
	bool m_bIsShutdown; //关闭时，退出工作线程
	short m_listenPort; //服务器开启的监听端口号
	LONG m_nMaxConnClientCnt; //最大客户端数量
	LONG m_nConnClientCnt; //已连接客户端数量
	LONG m_nWorkerCnt; //IO工作线程数量
	HANDLE m_hIOCompletionPort; //完成端口的句柄
	HANDLE m_hExitEvent; //退出线程事件，为了能够更好的退出
	std::vector<HANDLE> m_hWorkerThreads; //工作线程句柄列表
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
	LPFN_ACCEPTEX m_lpfnAcceptEx; //acceptEx函数指针
	ListenContext* m_pListenCtx; //监听上下文
	CRITICAL_SECTION m_csClientList; //保护客户端链表std::list<ClientContext*>
	std::list<ClientContext*> m_connectedClientList; //已连接客户端链表
	std::list<ClientContext*> m_freeClientList; //空闲的ClientContext链表

public:
	IocpServer(short listenPort = DEFAULT_PORT, int maxConnCount = MAX_CONN_COUNT);
	IocpServer& operator=(const IocpServer&) = delete;
	IocpServer(const IocpServer&) = delete;
	virtual ~IocpServer();
	
	bool Start(); // 启动服务器	
	bool Stop(); //	停止服务器
	bool Send(ClientContext* pClientCtx, PBYTE pData, UINT len);

	// 获取当前连接数
	int GetConnectCount() { return m_nConnClientCnt; }
	// 获取当前监听端口
	unsigned int GetPort() { return m_listenPort; }

protected:
	//必须要static _beginthreadex才能访问
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

	//管理已连接客户端链表，线程安全
	void addClientCtx(ClientContext* pClientCtx);
	void removeClientCtx(ClientContext* pClientCtx);
	void removeAllClientCtxs();

	ClientContext* allocateClientCtx(SOCKET s);
	void releaseClientCtx(ClientContext* pClientCtx);

	void echo(ClientContext* pClientCtx);

	//回调函数
	virtual void notifyNewConnection(ClientContext* pClientCtx);
	//virtual void notifyDisconnected(ClientContext* pClientCtx);
	virtual void notifyDisconnected(SOCKET s, Addr addr);
	virtual void notifyPackageReceived(ClientContext* pClientCtx);
	virtual void notifyWritePackage();
	virtual void notifyWriteCompleted();
	virtual void showMessage(const char* szFormat, ...);
};