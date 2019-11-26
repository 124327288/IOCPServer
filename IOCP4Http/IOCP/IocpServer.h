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
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExAddr;
	LPFN_ACCEPTEX m_lpfnAcceptEx; //acceptEx函数指针
	ListenContext* m_pListenCtx; //监听上下文
	CRITICAL_SECTION m_csClientList; //保护客户端链表std::list<ClientContext*>
	std::list<ClientContext*> m_connectedClientList; //已连接客户端链表
	std::list<ClientContext*> m_freeClientList; //空闲的ClientContext链表
	std::vector<AcceptIoContext*> m_acceptIoCtxList; //接收连接的IO上下文列表
	//postSend对应的写操作已完成，可以进行下一个投递
	HANDLE m_hWriteCompletedEvent;

public:
	IocpServer(short listenPort = DEFAULT_PORT, int maxConnectionCount = 10000);
	IocpServer(const IocpServer&) = delete;
	IocpServer& operator=(const IocpServer&) = delete;
	virtual ~IocpServer();

	bool Start();
	bool Stop();
	bool Shutdown();
	bool Send(ClientContext* pConnClient, PBYTE pData, UINT len);

	// 获取当前连接数
	int GetConnectCount() { return m_nConnClientCnt; }
	// 获取当前监听端口
	unsigned int GetPort() { return m_listenPort; }

protected:
	//必须要static _beginthreadex才能访问
	static unsigned WINAPI IocpWorkerThread(LPVOID arg);

	HANDLE associateWithCompletionPort(SOCKET s, ULONG_PTR completionKey);
	bool getAcceptExPtr();
	bool getAcceptExSockaddrs();
	bool setKeepAlive(ClientContext* pConnClient, LPOVERLAPPED lpOverlapped,
		int time = 1, int interval = 1);

	bool createListenClient(short listenPort);
	bool createIocpWorker();
	bool exitIocpWorker();
	bool initAcceptIoContext();

	bool postAccept(AcceptIoContext* pIoCtx);
	PostResult postRecv(ClientContext* pConnClient);
	PostResult postSend(ClientContext* pConnClient);

	bool handleAccept(LPOVERLAPPED lpOverlapped, DWORD dwBytesTransferred);
	bool handleRecv(ULONG_PTR lpCompletionKey, LPOVERLAPPED lpOverlapped,
		DWORD dwBytesTransferred);
	bool handleSend(ULONG_PTR lpCompletionKey, LPOVERLAPPED lpOverlapped,
		DWORD dwBytesTransferred);
	bool handleClose(ULONG_PTR lpCompletionKey);

	// Used to avoid access violation.
	void enterIoLoop(ClientContext* pClientCtx);
	int exitIoLoop(ClientContext* pClientCtx);

	void CloseClient(ClientContext* pConnClient);

	//管理已连接客户端链表，线程安全
	void addClient(ClientContext* pConnClient);
	void removeClient(ClientContext* pConnClient);
	void removeAllClients();

	ClientContext* allocateClientContext(SOCKET s);
	void releaseClientContext(ClientContext* pConnClient);

	void echo(ClientContext* pConnClient);

	//回调函数
	virtual void notifyNewConnection(ClientContext* pConnClient);
	//virtual void notifyDisconnected(ClientContext* pConnClient);
	virtual void notifyDisconnected(SOCKET s, Addr addr);
	virtual void notifyPackageReceived(ClientContext* pConnClient);
	virtual void notifyWritePackage();
	virtual void notifyWriteCompleted();
};

#endif // !__IOCP_SERVER_H__