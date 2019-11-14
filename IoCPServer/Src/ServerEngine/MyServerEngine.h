#pragma once

#include "..\iocp\IOCPModel.h"
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

using namespace std;


#define	IDC_hLB_Output				40002
#define	IDC_hEB_InputServerIP		40003
#define	IDC_hEB_InputServerPort		40004
#define	IDC_hST_TextServerIP		40006
#define	IDC_hST_TextServerPort		40007
#define	IDC_hBtn_Start				50000
#define	IDC_hBtn_Stop				50001
#define	IDC_hBtn_Exit				50002

/************************************************************************/
/* Desc : ���������� 
/* Author : thjie
/* Date : 2013-02-28
/************************************************************************/
namespace MyServer{

	class MyServerEngine{
	protected:
		HINSTANCE mhAppInst;		//ʵ��
		HWND      mhMainWnd;		//�����ھ��
		LPCTSTR lpszApplicationName;	//������
		LPCTSTR lpszTitle;	//����

		int mServerPort;	//����˿�

		CIOCPModel m_IOCP;     // ��Ҫ������ɶ˿�ģ��

		static HANDLE m_hMutexServerMsgs;	//�������
		vector<string> m_vtServerMsgs;	//Ҫ��ʾ����Ϣ


		HWND	hLB_Output;
		HWND	hST_TextServerIP;
		HWND	hEB_InputServerIP;
		HWND	hST_TextServerPort;
		HWND	hEB_InputServerPort;

		HWND	hBtnStart;		//��ʼ����
		HWND	hBtnStop;		//ֹͣ����
		HWND	hBtnExit;		//�˳���ť

	public:
		MyServerEngine(HINSTANCE hInstance);
		virtual ~MyServerEngine();

	public:
		HINSTANCE getAppInst();		//����ʵ��
		HWND      getMainWnd();		//���������ھ��

	public:
		virtual void Init();	//��ʼ��
		virtual void InitMainWindow();	//��ʼ��������
		virtual void InitControls(HWND hWnd);	//��ʼ���������еĿؼ�
		virtual int Run();	//����
		virtual void ShutDown();	//�ر�

		virtual void AddServerMsgs(string& msg);	//��ӷ�������Ϣ
		virtual void SettleServerMsgs();	//�����������Ϣ
		virtual LRESULT msgProc(UINT msg, WPARAM wParam, LPARAM lParam);	//�����ڴ�����

	protected:
		virtual void ShowText(string& msg);	//��ʾ��Ϣ

	};

	extern MyServerEngine* g_pServerEngine;	//�������

}

