#include <WinSock2.h>
#include <Windows.h>
#include "CommonLib.h"
#include "HttpCodec.h"
#include "HttpServer.h"
#include <iostream>
using namespace std;

HttpServer::HttpServer(short listenPort, int maxConnCount)
	: IocpServer(listenPort, maxConnCount)
{
	showMessage("HttpServer() listenPort=%d", listenPort);
}

HttpServer::~HttpServer()
{
	showMessage("~HttpServer()");
}

void HttpServer::OnRecvCompleted(ClientContext* pClientCtx)
{
	showMessage("OnRecvCompleted() pClientCtx=%p", pClientCtx);
	HttpCodec codec(pClientCtx->m_inBuf.getBuffer(),
		pClientCtx->m_inBuf.getBufferLen());
	while (true)
	{
		int ret = codec.tryDecode();
		if (ret > 0)
		{//解析完成
			showMessage("tryDecode ok");
			showMessage(codec.getReqMethod().c_str());
			showMessage(codec.getReqUrl().c_str());
			showMessage(codec.getReqBody().c_str());
			if (codec.getReqUrl() == "/")
			{
				string rspMsg = codec.responseMessage("hello", HttpStatus::ok);
				SendData(pClientCtx, (PBYTE)rspMsg.c_str(), rspMsg.length());
			}
			else
			{
				char* pBuf = NULL;
				string dirFile = "./files" + codec.getReqUrl();
				int len = readFile(dirFile, pBuf);
				if (len > 0 && pBuf)
				{
					/*string rspMsg = codec.responseChunkedHeader();
					Send(pClientCtx, (PBYTE)rspMsg.c_str(), rspMsg.length());
					rspMsg = codec.responseChunkedBegin(len); //开始
					Send(pClientCtx, (PBYTE)rspMsg.c_str(), rspMsg.length());
					Send(pClientCtx, (PBYTE)pBuf, len); //实际数据
					rspMsg = codec.responseChunkedEnd(); //结束
					Send(pClientCtx, (PBYTE)rspMsg.c_str(), rspMsg.length());*/
					string contentType = "application/x-zip-compressed";
					if (codec.getReqUrl() == "/favicon.ico")
					{
						contentType = "image/x-icon";
					}
					string rspMsg = codec.responseHeader(contentType, len);
					SendData(pClientCtx, (PBYTE)rspMsg.c_str(), rspMsg.length());
					SendData(pClientCtx, (PBYTE)pBuf, len); //实际数据
					delete[]pBuf;
				}
				else
				{
					string rspMsg = codec.responseMessage("", HttpStatus::not_found);
					SendData(pClientCtx, (PBYTE)rspMsg.c_str(), rspMsg.length());
				}
			}
			pClientCtx->m_inBuf.remove(pClientCtx->m_inBuf.getBufferLen());
		}
		else if (ret < 0)
		{//解析失败
			showMessage("tryDecode failed");
			handleClose(pClientCtx);
			break;
		}
		else
		{//解析完了或者数据不完整
			//showMessage("tryDecode unfinished");
			break;
		}
	}
}
