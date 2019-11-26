#include "HttpStatus.h"
#include "HttpCodec.h"
#include <iostream>
#include <sstream>
using namespace std;

HttpCodec::HttpCodec(PBYTE pData, UINT size)
    : m_inBuf((PCHAR)pData, size)
{
}

int HttpCodec::tryDecode()
{
    Slice header;
    getHeader(m_inBuf, header);
    if (header.empty())
    {
        return 0;
    }
    Slice startLine = header.eatLine();
    Slice method = startLine.eatWord();
    Slice url = startLine.eatWord();
    Slice version = startLine.eatWord();
    if (version.empty())
    {
        m_res.m_status = HttpStatus::bad_request;
        return -1;
    }
    m_req.m_method = method;
    m_req.m_url = url;
    m_req.m_version = version;
    while (!header.empty())
    {
        //删除\r\n
        header.eat(2);
        Slice line = header.eatLine();
        Slice key = line.eatWord();
        Slice value = line.eatWord();
        if(!value.empty() && !key.empty() && key.back() == ':')
        {
            m_req.m_headers.insert(make_pair(key.sub(0, -1), value));
        }
        else
        {
            m_res.m_status = HttpStatus::bad_request;
            return -1;
        }
    }
    if (!parseStartLine())
    {
        return -1;
    }
    if (!parseHeader())
    {
        return -1;
    }
    string contentLength = m_req.getHeaderField("content-length");
    //body不完整，继续recv
    if (m_inBuf.size() < m_req.m_nHeaderLength + atoi(contentLength.c_str()))
    {
        return 0;
    }
    m_req.m_body = Slice(m_inBuf.data() + m_req.m_nHeaderLength,
		atoi(contentLength.c_str()));
    if ("GET" == m_req.m_method)
    {
        return m_inBuf.size();
    }
    else if ("POST" == m_req.m_method)
    {
        if (!parseBody())
		{
            return -1;
		}
    }
    return -1;
}

string HttpCodec::responseMessage(string s, HttpStatus status)
{
	ostringstream os;
	os << "HTTP/1.1" << " " << status << "\r\n";
	os << "Content-Type: text/plain\r\n";
	os << "Content-Length: " << s.length() << "\r\n";	   
    //非法连接则通知对端关闭http连接，然后server再关闭tcp连接
    if (HttpStatus::ok != m_res.m_status)
    {
        os << "Connection: close\r\n";
    }
    os << "\r\n";
    os << s;
	return os.str();
}

string HttpCodec::responseChunkedHeader()
{
	return string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
}

string HttpCodec::responseChunkedBegin(long len)
{
	char chunk_size[65] = { 0 };
	snprintf(chunk_size, sizeof(chunk_size), "%lX\r\n", len);
	return string(chunk_size);
}

string HttpCodec::responseChunkedEnd()
{
	char chunk_size[65] = { 0 };
	snprintf(chunk_size, sizeof(chunk_size), "%lX\r\n\r\n", 0);
	return string(chunk_size);
}

bool HttpCodec::getHeader(Slice data, Slice& header)
{
    size_t sz = data.size();
    if (sz < 4)
	{
        return false;
	}
    for (size_t i = 0; i <= sz - 4; ++i)
    {
        const char* pb = data.data();
        if ('\r' == data[i] && 0 == memcmp("\r\n\r\n", pb + i, 4))
        {
            header = Slice(pb, i);
            m_req.m_nHeaderLength = i + 4;
            return true;
        }
    }
    return false;
}

bool HttpCodec::parseStartLine()
{
    try
    {
        if ("HTTP/1.0" != m_req.m_version && "HTTP/1.1" != m_req.m_version)
        {
            informUnsupported();
            return false;
        }
        if ("GET" != m_req.m_method)// && "POST" != m_req.m_method)
        {
            informUnimplemented();
            return false;
        }
        if ("" == m_req.m_url)
        {
            m_res.m_status = HttpStatus::bad_request;
            return false;
        }
        if ('/' != m_req.m_url.at(0))
        {
            m_res.m_status = HttpStatus::bad_request;
            return false;
        }
        return true;
    }
    catch (std::exception& e)
    {
        cout << "exception: " << e.what() << endl;
        m_res.m_status = HttpStatus::internal_server_error;
        return false;
    }
}

bool HttpCodec::parseHeader()
{
    if ("POST" == m_req.m_method)
    {
        auto it = m_req.m_headers.find("Content-Length");
        if (it == m_req.m_headers.end())
        {
            m_res.m_status = HttpStatus::bad_request;
            return false;
        }
    }
    auto it = m_req.m_headers.find("Host");
    if (it == m_req.m_headers.end())
    {
        m_res.m_status = HttpStatus::bad_request;
        return false;
    }

    auto it2 = m_req.m_headers.find("Connection");
    if (it2 != m_req.m_headers.end())
    {
        cout << "keep-alive" << endl;
    }
    return true;
}

bool HttpCodec::parseBody()
{
    return false;
}

bool HttpCodec::informUnimplemented()
{
    m_res.m_status = HttpStatus::not_implemented;
    return true;
}

bool HttpCodec::informUnsupported()
{
    // 505 (HTTP Version Not Supported)
    m_res.m_status = HttpStatus::http_version_not_supported;
    return true;
}