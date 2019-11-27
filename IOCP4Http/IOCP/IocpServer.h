#ifndef __IOCP_SERVER_H__
#define __IOCP_SERVER_H__
#include "Addr.h"
#include "PerSocketContext.h"
#include <mswsock.h>
#include <vector>
#include <list>

//工作线程退出标志
constexpr int EXIT_THREAD = 0;
constexpr int MAX_POST_ACCEPT = 10;
constexpr int DEFAULT_PORT = 10240; //默认端口号

struct ListenContext;
struct ClientContext;
struct AcceptIoContext;

class IocpServer
{
private:
	bool m_bIsShutdown; //关闭时，退出工作线程
	short m_listenPort; //服务器开启的监听端口号
	LONG m_nMaxConnClientCnt; //最大客户端数量
	LONG m_nConnClientCnt; //已连接客户端数量
	LONG m_nWorkerCnt; //io工作线程数量
	HANDLE m_hIOCompletionPort; //完成端口
	HANDLE m_hExitEvent; //退出线程事件
	std::vector<HANDLE> m_hWorkerThreads; //工作线程句柄列表
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
	LPFN_ACCEPTEX m_lpfnAcceptEx; //acceptEx函数指针
	ListenContext* m_pListenCtx; //监听上下文
	CRITICAL_SECTION m_csClientList; //保护客户端链表std::list<ClientContext*>
	std::list<ClientContext*> m_connectedClientList; //已连接客户端链表
	std::list<ClientContext*> m_freeClientList; //空闲的ClientContext链表
	std::vector<AcceptIoContext*> m_acceptIoCtxList; //接收连接的IO上下文列表
	CRITICAL_SECTION m_csLog; // 用于Worker线程同步的互斥量

public:
	IocpServer(short listenPort = DEFAULT_PORT, int maxConnectionCount = 10000);
	IocpServer(const IocpServer&) = delete;
	IocpServer& operator=(const IocpServer&) = delete;
	virtual ~IocpServer();

	bool Start();
	bool Stop();
	bool Shutdown();
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
	bool initAcceptIoContext();

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

#endif // !__IOCP_SERVER_H__