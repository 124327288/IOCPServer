#pragma once

#include <string>

using namespace std;

/************************************************************************/
/* Desc : ������ 
/* Author : thjie
/* Date : 2013-02-28
/************************************************************************/
namespace MyServer{
	class pub{
	public:
		//string ת��Ϊ float
		static float str2float(string str);

		//string ת��Ϊ int
		static int str2int( string str);

		//intת�ַ���
		static string Int2Str(int num);

		//floatת�ַ���
		static string Float2Str(float num,int round = 0);
	};

}
