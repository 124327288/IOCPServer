// MainDlg.h : ͷ�ļ�
#pragma once
#include "Client.h"

#define WM_ADD_LIST_ITEM (WM_USER + 100)  

// CMainDlg �Ի���
class CMainDlg : public CDialog
{
public:
	CMainDlg(CWnd* pParent = NULL);	// ��׼���캯��

	// �Ի�������
	enum { IDD = IDD_CLIENT_DIALOG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

protected:
	HICON m_hIcon;
	// ���ɵ���Ϣӳ�亯��
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	// ��ʼ����
	afx_msg void OnBnClickedOk();
	// ֹͣ����
	afx_msg void OnBnClickedStop();
	// �˳�
	afx_msg void OnBnClickedCancel();
	// �Ի�������
	afx_msg void OnDestroy();
	// �б�����ݵ�ˢ�£�����б��
	afx_msg LRESULT OnAddListItem(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

public:
	// Ϊ�����������Ϣ��Ϣ(����CIocpModel�е���)
	// Ϊ�˼��ٽ�������Ч�ʵ�Ӱ�죬�˴�ʹ��������
	inline void AddInformation(const CString strInfo)
	{
		try {
			CString* pStr = new CString(strInfo);
			PostMessage(WM_ADD_LIST_ITEM, 0, (LPARAM)pStr);
		}
		catch (...)
		{
		}
	}
private:
	// ��ʼ��������Ϣ
	void InitGUI();
	// ��ʼ��List�ؼ�
	void InitListCtrl();

private:
	CClient m_Client; // �ͻ������Ӷ���
};
