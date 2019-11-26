#include <WinSock2.h>
#include <Windows.h>
#include "HttpCodec.h"
#include "HttpServer.h"
#include <iostream>
using namespace std;

HttpServer::HttpServer(short listenPort, int maxConnectionCount)
    : IocpServer(listenPort, maxConnectionCount)
{
	InitializeCriticalSection(&m_csLog);
	showMessage("HttpServer() listenPort=%d", listenPort);
}

HttpServer::~HttpServer()
{
	showMessage("~HttpServer()");
	DeleteCriticalSection(&m_csLog);
}

void HttpServer::notifyPackageReceived(ClientContext* pConnClient)
{
	showMessage("notifyPackageReceived() pConnClient=%p", pConnClient);
    HttpCodec codec(pConnClient->m_inBuf.getBuffer(),
		pConnClient->m_inBuf.getBufferLen());
    while (true)
    { 
		int ret = codec.tryDecode();
        if (ret > 0)
        {//解析完成
			showMessage("tryDecode ok");
			showMessage(codec.m_req.m_method.c_str());
			showMessage(codec.m_req.m_url.c_str());
			showMessage(codec.m_req.m_body.toString().c_str());
            string resMsg = codec.responseMessage();
            Send(pConnClient, (PBYTE)resMsg.c_str(), resMsg.length());
            pConnClient->m_inBuf.remove(pConnClient->m_inBuf.getBufferLen());
        }
        else if (ret < 0)
        {//解析失败
            showMessage("tryDecode failed");
            CloseClient(pConnClient);
            releaseClientCtx(pConnClient);
			break;
        }
		else
		{//数据不完整
			showMessage("tryDecode unfinished");
			break;
		}
    }
}

void print_time()
{
	SYSTEMTIME sysTime = { 0 };
	GetLocalTime(&sysTime);
	printf("%4d-%02d-%02d %02d:%02d:%02d.%03d：",
		sysTime.wYear, sysTime.wMonth, sysTime.wDay,
		sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
		sysTime.wMilliseconds);
}

void HttpServer::showMessage(const char* szFormat, ...)
{
	//printf(".");
	//return;
	__try
	{
		EnterCriticalSection(&m_csLog);
		print_time();
		// 处理变长参数
		va_list arglist;
		va_start(arglist, szFormat);
		vprintf(szFormat, arglist);
		va_end(arglist);
		printf("\n");
		return;
	}
	__finally
	{
		::LeaveCriticalSection(&m_csLog);
	}
}