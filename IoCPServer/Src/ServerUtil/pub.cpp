#include "pub.h"

namespace MyServer{

	//string ת��Ϊ float
	float pub::str2float(string str){
		float f = (float)atof(str.c_str());
		return f;
	}

	//string ת��Ϊ int
	int pub::str2int( string str){
		int rr = atoi(str.c_str());
		return rr;
	}

	//intת�ַ���
	string pub::Int2Str(int num){
		char aa[20];
		sprintf_s(aa,"%d",num);
		string ss = aa;
		return ss;
	}

	//floatת�ַ���
	string pub::Float2Str(float num,int round){
		char aa[20];
		string format = "%."+Int2Str(round)+"f";
		sprintf_s(aa,format.c_str(),num);
		string ss = aa;
		return ss;
	}

}

