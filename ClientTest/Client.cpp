#include "StdAfx.h"
#include "Client.h"
#include "MainDlg.h"
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")

#define RELEASE_HANDLE(x) {if(x != NULL && x!=INVALID_HANDLE_VALUE)\
		{ CloseHandle(x);x = NULL;}}
#define RELEASE_POINTER(x) {if(x != NULL ){delete x;x=NULL;}}
#define RELEASE_ARRAY(x) {if(x != NULL ){delete[] x;x=NULL;}}

CClient::CClient(void) :
	m_strServerIP(DEFAULT_IP),
	m_strLocalIP(DEFAULT_IP),
	m_nThreads(DEFAULT_THREADS),
	m_pMain(NULL),
	m_nPort(DEFAULT_PORT),
	m_strMessage(DEFAULT_MESSAGE),
	m_phWorkerThreads(NULL),
	m_hConnectionThread(NULL),
	m_hShutdownEvent(NULL)
{
}

CClient::~CClient(void)
{
	this->Stop();
}

//////////////////////////////////////////////////////////////////////////////////
//	�������ӵ��߳�
DWORD WINAPI CClient::_ConnectionThread(LPVOID lpParam)
{
	ConnectionThreadParam* pParams = (ConnectionThreadParam*)lpParam;
	CClient* pClient = (CClient*)pParams->pClient;
	TRACE("_AccpetThread������ϵͳ������...\n");
	pClient->EstablishConnections();
	TRACE(_T("_ConnectionThread�߳̽���.\n"));
	RELEASE_POINTER(pParams);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////
// ���ڷ�����Ϣ���߳�
DWORD WINAPI CClient::_WorkerThread(LPVOID lpParam)
{
	WorkerThreadParam* pParams = (WorkerThreadParam*)lpParam;
	CClient* pClient = (CClient*)pParams->pClient;
	char szTemp[MAX_BUFFER_LEN] = { 0 };
	char szRecv[MAX_BUFFER_LEN] = { 0 };
	int nBytesSent = 0;
	int nBytesRecv = 0;

	for (int i = 1; i <= pParams->nSendTimes; i++)
	{
		memset(szRecv, 0, MAX_BUFFER_LEN);
		memset(szTemp, 0, sizeof(szTemp));
		// �������������Ϣ
		sprintf(szTemp, ("Msg:[%d] Thread:[%d], Data:[%s]"),
			i, pParams->nThreadNo, pParams->szSendBuffer);
		nBytesSent = send(pParams->sock, szTemp, strlen(szTemp), 0);
		if (SOCKET_ERROR == nBytesSent)
		{
			TRACE("send ERROR: ErrCode=[%ld]\n", WSAGetLastError());
			return 1;
		}
		pClient->ShowMessage("SENT: %s", szTemp);
		TRACE("SENT: %s\n", szTemp);

		memset(pParams->szRecvBuffer, 0, MAX_BUFFER_LEN);
		memset(szTemp, 0, sizeof(szTemp));
		nBytesRecv = recv(pParams->sock, pParams->szRecvBuffer,
			MAX_BUFFER_LEN, 0);
		if (SOCKET_ERROR == nBytesRecv)
		{
			TRACE("recv ERROR: ErrCode=[%ld]\n", WSAGetLastError());
			return 1;
		}
		pParams->szRecvBuffer[nBytesRecv] = 0;
		sprintf(szTemp, ("RECV: Msg:[%d] Thread[%d], Data[%s]"),
			i, pParams->nThreadNo, pParams->szRecvBuffer);
		pClient->ShowMessage(szTemp);
		Sleep(1000);
	}

	if (pParams->nThreadNo == pClient->m_nThreads)
	{
		pClient->ShowMessage(_T("���Բ��� %d ���߳����."),
			pClient->m_nThreads);
	}
	return 0;
}
///////////////////////////////////////////////////////////////////////////////////
// ��������
bool  CClient::EstablishConnections()
{
	DWORD nThreadID = 0;
	PCSTR pData = m_strMessage.GetString();
	m_phWorkerThreads = new HANDLE[m_nThreads];
	m_pWorkerParams = new WorkerThreadParam[m_nThreads];
	// �����û����õ��߳�����������ÿһ���߳����������������������̷߳�������
	for (int i = 0; i < m_nThreads; i++)
	{
		// �����û���ֹͣ�¼�
		if (WAIT_OBJECT_0 == WaitForSingleObject(m_hShutdownEvent, 0))
		{
			TRACE(_T("���յ��û�ֹͣ����.\n"));
			return true;
		}
		// ���������������
		if (!this->ConnetToServer(&m_pWorkerParams[i].sock,
			m_strServerIP, m_nPort))
		{
			ShowMessage(_T("���ӷ�����ʧ�ܣ�"));
			//CleanUp(); //����������̻߳����ã��ͱ�����
			//return false;
			continue;
		}
		m_pWorkerParams[i].nThreadNo = i + 1;
		m_pWorkerParams[i].nSendTimes = m_nTimes;
		sprintf(m_pWorkerParams[i].szSendBuffer, "%s", pData);
		Sleep(10);
		// ������ӷ������ɹ����Ϳ�ʼ�����������̣߳������������ָ������
		m_pWorkerParams[i].pClient = this;
		m_phWorkerThreads[i] = CreateThread(0, 0, _WorkerThread,
			(void*)(&m_pWorkerParams[i]), 0, &nThreadID);
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////////
//	�����������Socket����
bool CClient::ConnetToServer(SOCKET* pSocket,
	CString strServer, int nPort)
{
	struct sockaddr_in ServerAddress;
	struct hostent* Server;
	// ����SOCKET
	*pSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == *pSocket)
	{
		TRACE("���󣺳�ʼ��Socketʧ�ܣ�������Ϣ��%d\n",
			WSAGetLastError());
		return false;
	}
	// ���ɵ�ַ��Ϣ
	Server = gethostbyname(strServer.GetString());
	if (Server == NULL)
	{
		closesocket(*pSocket);
		TRACE("������Ч�ķ�������ַ.\n");
		return false;
	}
	ZeroMemory((char*)&ServerAddress, sizeof(ServerAddress));
	ServerAddress.sin_family = AF_INET;
	CopyMemory((char*)&ServerAddress.sin_addr.s_addr,
		(char*)Server->h_addr, Server->h_length);
	ServerAddress.sin_port = htons(m_nPort);
	// ��ʼ���ӷ�����
	if (SOCKET_ERROR == connect(*pSocket,
		reinterpret_cast<const struct sockaddr*>(&ServerAddress),
		sizeof(ServerAddress)))
	{
		closesocket(*pSocket);
		TRACE("����������������ʧ�ܣ�\n");
		return false;
	}
	return true;
}

////////////////////////////////////////////////////////////////////
// ��ʼ��WinSock 2.2
bool CClient::LoadSocketLib()
{
	WSADATA wsaData;
	int nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (NO_ERROR != nResult)
	{
		ShowMessage(_T("��ʼ��WinSock 2.2ʧ�ܣ�\n"));
		return false; // ����
	}
	return true;
}

///////////////////////////////////////////////////////////////////
// ��ʼ����
bool CClient::Start()
{
	// ����ϵͳ�˳����¼�֪ͨ
	m_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	// ���������߳�
	DWORD nThreadID = 0;
	ConnectionThreadParam* pThreadParams = new ConnectionThreadParam;
	pThreadParams->pClient = this;
	m_hConnectionThread = ::CreateThread(0, 0, _ConnectionThread,
		(void*)pThreadParams, 0, &nThreadID);
	return true;
}

///////////////////////////////////////////////////////////////////////
//	ֹͣ����
void CClient::Stop()
{
	if (m_hShutdownEvent == NULL) return;
	SetEvent(m_hShutdownEvent);
	// �ȴ�Connection�߳��˳�
	WaitForSingleObject(m_hConnectionThread, INFINITE);
	// �ر����е�Socket
	for (int i = 0; i < m_nThreads; i++)
	{
		closesocket(m_pWorkerParams[i].sock);
	}
	// �ȴ����еĹ������߳��˳�
	WaitForMultipleObjects(m_nThreads, m_phWorkerThreads, TRUE, INFINITE);
	// �����Դ
	CleanUp();
	TRACE("����ֹͣ.\n");
}

//////////////////////////////////////////////////////////////////////
//	�����Դ
void CClient::CleanUp()
{
	if (m_hShutdownEvent == NULL) return;
	RELEASE_ARRAY(m_phWorkerThreads);
	RELEASE_HANDLE(m_hConnectionThread);
	RELEASE_ARRAY(m_pWorkerParams);
	RELEASE_HANDLE(m_hShutdownEvent);
}

////////////////////////////////////////////////////////////////////
// ��ñ�����IP��ַ
CString CClient::GetLocalIP()
{
	char hostname[MAX_PATH];
	gethostname(hostname, MAX_PATH); // ��ñ���������
	struct hostent FAR* lpHostEnt = gethostbyname(hostname);
	if (lpHostEnt == NULL)
	{
		return DEFAULT_IP;
	}
	// ȡ��IP��ַ�б��еĵ�һ��Ϊ���ص�IP
	LPSTR lpAddr = lpHostEnt->h_addr_list[0];
	struct in_addr inAddr;
	memmove(&inAddr, lpAddr, 4);
	// ת���ɱ�׼��IP��ַ��ʽ
	m_strLocalIP = CString(inet_ntoa(inAddr));
	return m_strLocalIP;
}

/////////////////////////////////////////////////////////////////////
// ������������ʾ��Ϣ
void CClient::ShowMessage(const CString strInfo, ...)
{
	// ���ݴ���Ĳ�����ʽ���ַ���
	CString   strMessage;
	va_list   arglist;

	va_start(arglist, strInfo);
	strMessage.FormatV(strInfo, arglist);
	va_end(arglist);

	// ������������ʾ
	CMainDlg* pMain = (CMainDlg*)m_pMain;
	if (m_pMain != NULL)
	{
		pMain->AddInformation(strMessage);
	}
}
