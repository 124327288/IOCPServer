#include "PerIoContext.h"
#include <iostream>
using namespace std;

IoContext::IoContext(PostType type) : m_PostType(type)
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
	m_wsaBuf.buf = nullptr;
	m_wsaBuf.len = 0;
}

IoContext::~IoContext()
{
}

void IoContext::ResetBuffer()
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
	m_wsaBuf.buf = nullptr;
	m_wsaBuf.len = 0;
}

AcceptIoContext::AcceptIoContext(SOCKET acceptSocket)
    : IoContext(PostType::ACCEPT), m_acceptSocket(acceptSocket)
{
    SecureZeroMemory(m_acceptBuf, MAX_BUFFER_LEN);
    m_wsaBuf.buf = (PCHAR)m_acceptBuf;
    m_wsaBuf.len = MAX_BUFFER_LEN;
}

AcceptIoContext::~AcceptIoContext()
{
}

void AcceptIoContext::ResetBuffer()
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
    SecureZeroMemory(m_acceptBuf, MAX_BUFFER_LEN);
}

RecvIoContext::RecvIoContext()
    : IoContext(PostType::RECV)
{
    SecureZeroMemory(m_recvBuf, MAX_BUFFER_LEN);
    m_wsaBuf.buf = (PCHAR)m_recvBuf;
    m_wsaBuf.len = MAX_BUFFER_LEN;
}

RecvIoContext::~RecvIoContext()
{
}

void RecvIoContext::ResetBuffer()
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
    SecureZeroMemory(m_recvBuf, MAX_BUFFER_LEN);
}

SendIoContext::SendIoContext()
	: IoContext(PostType::SEND)
{
}

SendIoContext::~SendIoContext()
{
}