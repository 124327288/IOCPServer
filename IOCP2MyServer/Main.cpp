#include "MyServer.h"
using namespace MyServer;

int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE prevInstance, PSTR cmdLine, int showCmd) 
{
	//��ʼ�������
	srand(timeGetTime());
	//����һ������ʵ����ִ��
	CMyServer app(hInstance);
	g_pServer = &app;
	g_pServer->Init();
	g_pServer->Run();
	g_pServer->ShutDown();
	return 0;
}