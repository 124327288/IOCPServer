#ifndef __CODEC_H__
#define __CODEC_H__
#include "HttpStatus.h"
#include "HttpMessage.h"
#include <Windows.h>
#include <unordered_map>
#include <string>
using std::string;

struct HttpCodec
{
	HttpCodec(PBYTE pData, UINT size);

	int tryDecode();
	bool getHeader(Slice data, Slice& header);
	bool parseStartLine();
	bool parseHeader();
	bool parseBody();
	bool informUnimplemented();
	bool informUnsupported();
	string responseMessage(string s, HttpStatus status);
	string responseHeader(string header, long len);
	string responseChunkedHeader();
	string responseChunkedBegin(long len);
	string responseChunkedEnd();

	string getReqMethod();
	string getReqUrl();
	string getReqBody();
private:
	Slice m_inBuf;
	string m_outBuf;
	HttpRequest m_req;
	HttpResponse m_res;
};

#endif // !__CODEC_H__
