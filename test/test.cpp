#include <iostream>
using namespace std;

void noMemoryToAlloc()
{
	cerr << "unable to satisfy request for memory\n";
	abort();
}

int main1()
{
	try
	{
		//set过后，新的异常处理函数会被调用到
		set_new_handler(noMemoryToAlloc);
		size_t size = 0;//0x7FFFFFFFFFFFFFFF;
		int* p = new int[size];
		if (p == 0) // 检查 p 是否空指针
		{
			return -1;
		}
	}
	catch (const bad_alloc & e)
	{
		//否则，会调用到这里
		cerr << e.what();
		return -1;
	}
	return 0;
}

#include "Cow.h"
int main2()
{
	Cow cow1;
	cow1.showCow();
	Cow cow2("yellow", "grass", 120);
	cow2.showCow();
	Cow cow3(cow2);
	cow3.showCow();
	cow1 = cow2;
	cow1.showCow();
	system("pause");
	return 0;
}


int main3()
{
	char s[10] = { 0 };
	//字符串会被截断，很安全
	//snprintf(s, sizeof(s), "hello world");
	//将一个无效参数传递给了将无效参数视为严重错误的函数。
	//strncpy_s(s, "hello world", sizeof(s));
	cout << (void*)s << endl; //跟函数一样
	cout << &s;
	return 0;
}

#include <queue>
#include <string>
class A
{

public:
	string name;

	A(string nam)
	{
		cout << nam + "  constructor is called !" << endl;
		name = nam;
	}

	A(const A& a)
	{
		this->name = "A copy of " + a.name;
		cout << name + "  copy constructor is called !" << endl;
	}

	~A()
	{
		cout << name + "  destructor is called " << endl;
	}
};

int main()
{
	queue<A> qu;

	A a1("a1");
	A a2("a2");
	A a3(a1);

	//qu.push(a1);
	qu.emplace(a1);
	//qu.push(A("a3"));
	//qu.emplace(A("a3"));
	qu.emplace("a3");
	cout << qu.front().name << endl;
	qu.pop();
	cout << qu.front().name << endl;
	qu.pop();

	cout << "end of main()" << endl;
}