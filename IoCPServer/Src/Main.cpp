//#include <vld.h>
#include "ServerEngine/MyServerEngine.h"

using namespace MyServer;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,PSTR cmdLine, int showCmd){
	//��ʼ�������
	srand(timeGetTime());

	//����һ������ʵ����ִ��
	MyServerEngine app(hInstance);
	g_pServerEngine = &app;
	g_pServerEngine->Init();
	g_pServerEngine->Run();
	g_pServerEngine->ShutDown();

	return 0;

}