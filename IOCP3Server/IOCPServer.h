//IOCPServer.h : Ӧ�ó������ͷ�ļ�
#pragma once
//#include "resource.h" // ������
// CMyServerApp:
// �йش����ʵ�֣������ IOCPServer.cpp
//
class CMyServerApp : public CWinApp
{
public:
	CMyServerApp();

public:
	virtual BOOL InitInstance();

	// ʵ��
	DECLARE_MESSAGE_MAP()
};

extern CMyServerApp theApp;