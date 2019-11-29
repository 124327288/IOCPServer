#include "Iocp/IocpServer.h"
#include "Http/HttpServer.h"
#include <cstdio>

int main()
{
	{
		printf("main()\n");
		//IocpServer server(10240);
		HttpServer server(8000);
		bool ret = server.Start();
		if (!ret)
		{
			printf("main() start failed\n");
			return 0;
		}
		while (1)
		{
			Sleep(1000);
		}
	}
	system("pause");
	return 0;
}
