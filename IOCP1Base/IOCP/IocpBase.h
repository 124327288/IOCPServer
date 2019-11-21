/*==========================================================================
* 这个类CIocpBase是本代码的核心类，用于说明WinSock服务器端编程模型中的
	完成端口(IOCP)的使用方法, 其中的IoContext类是封装了用于每一个重叠操作的参数，
	具体说明了服务器端建立完成端口、建立工作者线程、投递Recv请求、投递Accept请求的方法，
	所有的客户端连入的Socket都需要绑定到IOCP上，所有从客户端发来的数据，都会调用回调函数。
*用法：派生一个子类，重载回调函数
Created by TTGuoying at 2018/02
Revised by GaoJS at 2019/11
==========================================================================*/
#pragma once
#include <WinSock2.h>
#include <MSWSock.h>
#include <Windows.h>
#include <vector>
#include <list>
#include <string>

using std::list;
using std::vector;
using std::wstring;

#define BUFF_SIZE (1024*4) // I/O 请求的缓冲区大小
#define WORKER_THREADS_PER_PROCESSOR (1) // 每个处理器上的线程数
#define INIT_IOCONTEXT_NUM (3) // IOContextPool中的初始数量
#define MAX_POST_ACCEPT (2) // 同时投递的Accept数量
#define EXIT_CODE	(-1) // 传递给Worker线程的退出信号
#define DEFAULT_PORT	(10240) // 默认端口

// 释放指针的宏
#define RELEASE_POINTER(x) {if(x != NULL) {delete x; x = NULL;}}
// 释放句柄的宏
#define RELEASE_HANDLE(x)	{if(x != NULL && x != INVALID_HANDLE_VALUE)\
	 { CloseHandle(x); x = INVALID_HANDLE_VALUE; }}
// 释放Socket的宏
#define RELEASE_SOCKET(x)	{if(x != INVALID_SOCKET)\
	 { closesocket(x); x = INVALID_SOCKET; }}

#ifndef TRACE
#define TRACE
//#define TRACE wprintf
//#include <atltrace.h>
//#define TRACE AtlTrace
#endif

enum class IOTYPE
{
	UNKNOWN, // 用于初始化，无意义
	ACCEPT, // 投递Accept操作
	SEND, // 投递Send操作
	RECV, // 投递Recv操作
};

//每投递网络IO请求，都要求提供一个WSABuf和WSAOVERLAPPED的参数，
//所以，我们自定义一个IOContext类，每次投递附带这个类的变量，
//但要注意这个变量的生命周期，防止内存泄漏。
class IoContext
{
public:
	// 每个socket的每一个IO操作都需要一个重叠结构
	WSAOVERLAPPED overLapped;
	SOCKET hSocket; // 此IO操作对应的socket
	WSABUF wsaBuf; // 数据缓冲
	IOTYPE ioType; // IO操作类型

	IoContext()
	{
		TRACE(L"IoContext()\n");
		hSocket = INVALID_SOCKET;
		ZeroMemory(&overLapped, sizeof(overLapped));
		wsaBuf.buf = (char*)HeapAlloc(GetProcessHeap(),
			HEAP_ZERO_MEMORY, BUFF_SIZE);
		wsaBuf.len = BUFF_SIZE;
		ioType = IOTYPE::UNKNOWN;
	}

	~IoContext()
	{
		TRACE(L"~IoContext()\n");
		RELEASE_SOCKET(hSocket);
		if (wsaBuf.buf != NULL)
		{
			HeapFree(GetProcessHeap(), 0, wsaBuf.buf);
		}
	}

	void Reset()
	{
		TRACE(L"Reset()\n");
		if (wsaBuf.buf != NULL)
		{
			ZeroMemory(wsaBuf.buf, BUFF_SIZE);
		}
		else
		{
			wsaBuf.buf = (char*)HeapAlloc(GetProcessHeap(),
				HEAP_ZERO_MEMORY, BUFF_SIZE);
		}
		ZeroMemory(&overLapped, sizeof(overLapped));
		ioType = IOTYPE::UNKNOWN;
	}
};

// 对于每一个socket也定义了一个IOContextPool的缓冲池类
// 空闲的IoContext管理类(IoContext池)
class IoContextPool
{
private:
	list<IoContext*> contextList;
	CRITICAL_SECTION csLock;

public:
	IoContextPool()
	{
		TRACE(L"IoContextPool()\n");
		InitializeCriticalSection(&csLock);
		EnterCriticalSection(&csLock);
		contextList.clear();
		for (size_t i = 0; i < INIT_IOCONTEXT_NUM; i++)
		{
			IoContext* context = new IoContext;
			contextList.push_back(context);
		}
		LeaveCriticalSection(&csLock);
	}

	~IoContextPool()
	{
		TRACE(L"~IoContextPool()\n");
		EnterCriticalSection(&csLock);
		for (list<IoContext*>::iterator it = contextList.begin();
			it != contextList.end(); it++)
		{
			delete (*it);
		}
		contextList.clear();
		LeaveCriticalSection(&csLock);
		DeleteCriticalSection(&csLock);
	}

	// 分配一个IOContxt
	IoContext* AllocateIoContext()
	{
		TRACE(L"AllocateIoContext()\n");
		IoContext* context = NULL;
		EnterCriticalSection(&csLock);
		if (contextList.size() > 0)
		{//list不为空，从list中取一个
			context = contextList.back();
			contextList.pop_back();
		}
		else
		{//list为空，新建一个
			context = new IoContext;
		}
		LeaveCriticalSection(&csLock);
		return context;
	}

	// 回收一个IOContxt
	void ReleaseIoContext(IoContext* pContext)
	{
		TRACE(L"ReleaseIoContext()\n");
		pContext->Reset();
		EnterCriticalSection(&csLock);
		contextList.push_front(pContext);
		LeaveCriticalSection(&csLock);
	}
};

// 对于每一个socket也定义了一个SocketContext的类
class SocketContext
{
public:
	SOCKET connSocket; // 连接的socket
	SOCKADDR_IN clientAddr; // 连接的远程地址

private:
	static IoContextPool ioContextPool; // 空闲的IOContext池
	vector<IoContext*> arrIoContext; // 同一个socket上的多个IO请求
	CRITICAL_SECTION csLock;

public:
	SocketContext()
	{
		TRACE(L"SocketContext()\n");
		InitializeCriticalSection(&csLock);
		EnterCriticalSection(&csLock);
		arrIoContext.clear(); //没用
		LeaveCriticalSection(&csLock);
		ZeroMemory(&clientAddr, sizeof(clientAddr));
		connSocket = INVALID_SOCKET;
	}

	~SocketContext()
	{
		TRACE(L"~SocketContext()\n");
		RELEASE_SOCKET(connSocket);
		// 回收所有的IOContext
		EnterCriticalSection(&csLock);
		if (arrIoContext.size() > 0)
		{
			for (vector<IoContext*>::iterator it = arrIoContext.begin();
				it != arrIoContext.end(); it++)
			{
				ioContextPool.ReleaseIoContext(*it);
			}
			arrIoContext.clear();
		}
		LeaveCriticalSection(&csLock);
		DeleteCriticalSection(&csLock);
	}

	// 获取一个新的IoContext
	IoContext* GetNewIoContext()
	{
		TRACE(L"GetNewIoContext()\n");
		IoContext* context = ioContextPool.AllocateIoContext();
		if (context != NULL)
		{
			EnterCriticalSection(&csLock);
			arrIoContext.push_back(context);
			LeaveCriticalSection(&csLock);
		}
		return context;
	}

	// 从数组中移除一个指定的IoContext
	void RemoveContext(IoContext* pContext)
	{
		TRACE(L"RemoveContext()\n");
		for (vector<IoContext*>::iterator it = arrIoContext.begin();
			it != arrIoContext.end(); it++)
		{
			if (pContext == *it)
			{
				ioContextPool.ReleaseIoContext(*it);
				EnterCriticalSection(&csLock);
				arrIoContext.erase(it);
				LeaveCriticalSection(&csLock);
				break;
			}
		}
	}
};

// IOCP基类
class CIocpBase
{
public:
	CIocpBase();
	~CIocpBase();

	// 开始服务
	BOOL Start(int port = DEFAULT_PORT);

	// 停止服务
	void Stop();

	// 向指定客户端发送数据
	BOOL SendData(SocketContext* socketContext, char* data, int size);

	// 获取当前连接数
	ULONG GetConnectCount() { return connectCount; }

	// 获取当前连接数
	UINT GetPort() { return port; }

	// 事件通知函数(派生类重载此族函数)
	// 新连接
	virtual void OnConnectionAccepted(SocketContext* sockContext) = 0;
	// 连接关闭
	virtual void OnConnectionClosed(SocketContext* sockContext) = 0;
	// 连接上发生错误
	virtual void OnConnectionError(SocketContext* sockContext, int error) = 0;
	// 读操作完成
	virtual void OnRecvCompleted(SocketContext* sockContext, IoContext* ioContext) = 0;
	// 写操作完成
	virtual void OnSendCompleted(SocketContext* sockContext, IoContext* ioContext) = 0;

protected:
	HANDLE stopEvent; // 通知线程退出的时间
	HANDLE completionPort; // 完成端口
	HANDLE* workerThreads; // 工作者线程的句柄指针
	int workerThreadNum; // 工作者线程的数量
	int port; // 监听端口
	SocketContext* listenSockContext; // 监听socket的Context
	LONG connectCount; // 当前的连接数量
	LONG acceptPostCount; // 当前投递的的Accept数量

	//AcceptEx函数指针,win8.1以后才支持
	LPFN_ACCEPTEX fnAcceptEx; 
	//GetAcceptExSockAddrs函数指针,win8.1以后才支持
	LPFN_GETACCEPTEXSOCKADDRS fnGetAcceptExSockAddrs;

private:
	// 工作线程函数
	static DWORD WINAPI WorkerThreadProc(LPVOID pThiz);

	// 初始化IOCP
	BOOL InitializeIocp();
	// 初始化Socket
	BOOL InitializeListenSocket();
	// 释放资源
	void DeInitialize();
	// socket是否存活
	BOOL IsSocketAlive(SOCKET sock);
	// 获取本机CPU核心数
	int GetNumOfProcessors();
	// 将句柄(Socket)绑定到完成端口中
	BOOL AssociateWithIocp(SocketContext* sockContext);
	// 投递IO请求
	BOOL PostAccept(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL PostRecv(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL PostSend(SocketContext*& sockContext, IoContext*& ioContext);
	// IO处理函数
	BOOL DoAccept(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL DoRecv(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL DoSend(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL DoClose(SocketContext*& sockContext);
};
