#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__
#include "../Iocp/IocpServer.h"

class HttpServer : public IocpServer
{
	CRITICAL_SECTION m_csLog; // 用于Worker线程同步的互斥量
public:
    HttpServer(short listenPort, int maxConnectionCount = 10000);
    ~HttpServer();
	
protected:
    void notifyPackageReceived(ClientContext* pConnClient) override;
    //void notifyDisconnected(SOCKET s, Addr addr) override;
	void showMessage(const char* szFormat, ...);
};

#endif // !__HTTP_SERVER_H__
