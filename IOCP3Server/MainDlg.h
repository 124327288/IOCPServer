// MainDlg.h : ͷ�ļ�
#pragma once
#include "IOCPModel.h"

#define WM_ADD_LIST_ITEM (WM_USER + 100)  

// CMainDlg �Ի���
class CMainDlg : public CDialog
{
public:
	CMainDlg(CWnd* pParent = NULL);	// ��׼���캯��

	enum { IDD = IDD_SERVER_DIALOG };

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
	// "�˳�"��ť
	afx_msg void OnBnClickedCancel();
	// ϵͳ�˳���ʱ��Ϊȷ����Դ�ͷţ�ֹͣ���������Socket���
	afx_msg void OnDestroy();
	// �б�����ݵ�ˢ�£�����б��
	afx_msg LRESULT OnAddListItem(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	// ��ʼ��Socket���Լ�������Ϣ
	void Init();
	// ��ʼ��List�ؼ�
	void InitListCtrl();

public:
	// ��ǰ�ͻ���������Ϣ������ʱ��������������ʾ
	//		�µ�������Ϣ(����CIocpModel�е���)
	// Ϊ�˼��ٽ�������Ч�ʵ�Ӱ�죬�˴�ʹ��������
	inline void AddInformation(const CString strInfo)
	{
		CString* pStr = new CString(strInfo);
		PostMessage(WM_ADD_LIST_ITEM, 0, (LPARAM)pStr);
	}

private:
	CIocpModel m_IOCP;// ��Ҫ������ɶ˿�ģ��
};
