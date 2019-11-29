#ifndef __ADDR_H__
#define __ADDR_H__
#include <WinSock2.h>
#include <string>

class Addr
{
private:
	SOCKADDR_IN m_addr;

public:
	Addr() {}
	Addr(const SOCKADDR_IN& addr);
	std::string toString() const;
	operator std::string() const;
	LPSOCKADDR_IN GetAddr();
};

#endif // !__ADDR_H__
