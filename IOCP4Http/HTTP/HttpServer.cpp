#include <WinSock2.h>
#include <Windows.h>
#include "CommonLib.h"
#include "HttpCodec.h"
#include "HttpServer.h"
#include <iostream>
using namespace std;

HttpServer::HttpServer(short listenPort, int maxConnectionCount)
	: IocpServer(listenPort, maxConnectionCount)
{
	showMessage("HttpServer() listenPort=%d", listenPort);
}

HttpServer::~HttpServer()
{
	showMessage("~HttpServer()");
}

void HttpServer::notifyPackageReceived(ClientContext* pClientCtx)
{
	showMessage("notifyPackageReceived() pClientCtx=%p", pClientCtx);
	HttpCodec codec(pClientCtx->m_inBuf.getBuffer(),
		pClientCtx->m_inBuf.getBufferLen());
	while (true)
	{
		int ret = codec.tryDecode();
		if (ret > 0)
		{//解析完成
			showMessage("tryDecode ok");
			showMessage(codec.m_req.m_method.c_str());
			showMessage(codec.m_req.m_url.c_str());
			showMessage(codec.m_req.m_body.toString().c_str());
			if (codec.m_req.m_url == "/")
			{
				string resMsg = codec.responseMessage();
				Send(pClientCtx, (PBYTE)resMsg.c_str(), resMsg.length());
			}
			else //if (codec.m_req.m_url == "/favicon.ico")
			{
				char* pBuf = NULL;
				string dirFile = "./files" + codec.m_req.m_url;
				int len = readFile(dirFile, pBuf);
				if (len > 0 && pBuf)
				{
					Send(pClientCtx, (PBYTE)pBuf, len);
					delete[]pBuf;
				}
			}
			pClientCtx->m_inBuf.remove(pClientCtx->m_inBuf.getBufferLen());
		}
		else if (ret < 0)
		{//解析失败
			showMessage("tryDecode failed");
			CloseClient(pClientCtx);
			releaseClientCtx(pClientCtx);
			break;
		}
		else
		{//数据不完整
			showMessage("tryDecode unfinished");
			break;
		}
	}
}
