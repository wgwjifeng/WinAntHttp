/*
 @ 0xCCCCCCCC
*/

#include "winant_http/winant_request.h"

#include <string>
#include <utility>

#include <Windows.h>
#include <WinInet.h>

#include "kbase/basic_macros.h"
#include "kbase/error_exception_util.h"
#include "kbase/string_encoding_conversions.h"
#include "kbase/string_util.h"
#include "kbase/tokenizer.h"

#include "winant_http/winant_constants.h"

namespace {

using wat::Headers;
using wat::HttpRequest;
using wat::ReadResponseHandler;

constexpr std::pair<HttpRequest::Method, const wchar_t*> kVerbTable[] {
    {HttpRequest::Method::Get, L"GET"},
    {HttpRequest::Method::Post, L"POST"},
    {HttpRequest::Method::Head, L"HEAD"}
};

const wchar_t* MethodToVerb(HttpRequest::Method method)
{
    for (const auto& ele : kVerbTable) {
        if (ele.first == method) {
            return ele.second;
        }
    }

    ENSURE(CHECK, kbase::NotReached())(method).Require();

    return nullptr;
}

auto SplitHeaderLine(kbase::StringView header_line)
{
    constexpr kbase::StringView delim = ": ";
    auto pos = header_line.find(delim);
    ENSURE(CHECK, pos != kbase::StringView::npos)(header_line).Require();

    auto name = header_line.substr(0, pos);
    auto value = header_line.substr(pos + delim.size());

    return std::make_pair(name.ToString(), value.ToString());
}

bool ReadResponseHeaders(HINTERNET request, Headers& headers)
{
    DWORD header_size = 0;

    HttpQueryInfoA(request, HTTP_QUERY_RAW_HEADERS_CRLF, nullptr, &header_size, nullptr);
    kbase::LastError error;
    ENSURE(CHECK, error.error_code() == ERROR_INSUFFICIENT_BUFFER)(error).Require();

    std::string header_buf;
    auto buf = kbase::WriteInto(header_buf, header_size);
    BOOL success = HttpQueryInfoA(request, HTTP_QUERY_RAW_HEADERS_CRLF, buf, &header_size, nullptr);

    // Skip the status line.
    kbase::Tokenizer header_lines(header_buf, "\r\n");
    for (auto it = std::next(header_lines.begin()); it != header_lines.end(); ++it) {
        auto values = SplitHeaderLine(*it);
        headers.SetHeader(values.first, values.second);
    }

    return success == TRUE;
}

// `response_body` might be nullptr, if you decide not to save the response body.
bool ReadResponseBody(HINTERNET request, std::string* response_body,
                      const ReadResponseHandler& read_handler)
{
    constexpr DWORD kBufSize = 4 * 1024;
    char buf[kBufSize] {0};

    BOOL success = FALSE;
    while (true) {
        DWORD bytes_read = 0;
        success = InternetReadFile(request, buf, kBufSize, &bytes_read);
        if (!success || bytes_read == 0) {
            break;
        }

        if (response_body) {
            response_body->append(buf, bytes_read);
        }

        if (read_handler) {
            read_handler(buf, static_cast<int>(bytes_read));
        }
    }

    if (read_handler) {
        if (success) {
            read_handler(buf, 0);
        } else {
            read_handler(nullptr, -1);
        }
    }

    return success == TRUE;
}

void SetContentHeader(HINTERNET request, kbase::WStringView content_type)
{
#if defined(NDEBUG)
    DWORD flag = HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE;
#else
    DWORD flag = HTTP_ADDREQ_FLAG_ADD_IF_NEW;
#endif

    BOOL success = HttpAddRequestHeadersW(request,
                                          content_type.data(),
                                          static_cast<DWORD>(content_type.length()),
                                          flag);
    ENSURE(THROW, success == TRUE)(kbase::LastError())(content_type).Require();
}

}   // namespace

namespace wat {

HttpRequest::HttpRequest(Method method, const Url& url)
    : method_(method), canonicalized_url_(url)
{
    ENSURE(CHECK, !canonicalized_url_.empty()).Require();

    auto request_url = kbase::ASCIIToWide(canonicalized_url_.spec());

    constexpr DWORD kSchemeLength = 16;
    size_t max_url_length = request_url.length();
    std::vector<wchar_t> host(max_url_length);
    std::vector<wchar_t> path(max_url_length);

    URL_COMPONENTS components;
    memset(&components, 0, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = kSchemeLength;
    components.lpszHostName = host.data();
    components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data();
    components.dwUrlPathLength = static_cast<DWORD>(path.size());

    BOOL success = InternetCrackUrlW(request_url.data(), static_cast<DWORD>(request_url.size()), 0,
                                     &components);
    ENSURE(THROW, success == TRUE)(kbase::LastError()).Require();

    // Initialize environment for WinINet.
    inet_env_.reset(InternetOpenW(kWinAntUserAgent,
                                  INTERNET_OPEN_TYPE_DIRECT,
                                  nullptr,
                                  nullptr,
                                  0));
    ENSURE(THROW, !!inet_env_)(kbase::LastError()).Require();

    // Open a HTTP session.
    conn_session_.reset(InternetConnectW(inet_env_.get(),
                                         components.lpszHostName,
                                         components.nPort,
                                         nullptr,
                                         nullptr,
                                         INTERNET_SERVICE_HTTP,
                                         0,
                                         0));
    ENSURE(THROW, !!conn_session_)(kbase::LastError()).Require();

    // We finally can create a HTTP request now.
    DWORD http_open_flag = components.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0;
    request_.reset(HttpOpenRequestW(conn_session_.get(),
                                    MethodToVerb(method),
                                    components.lpszUrlPath,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    http_open_flag,
                                    0));
    ENSURE(THROW, !!request_)(kbase::LastError()).Require();
}

void HttpRequest::SetLoadFlags(LoadFlags flags)
{
    load_flags_ = flags;
}

void HttpRequest::SetHeaders(const Headers& headers)
{
    FORCE_AS_NON_CONST_FUNCTION();

    auto headers_content = kbase::ASCIIToWide(headers.ToString());
    BOOL success = HttpAddRequestHeadersW(request_.get(),
                                          headers_content.data(),
                                          static_cast<DWORD>(headers_content.size()),
                                          HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
    ENSURE(THROW, success == TRUE)(kbase::LastError())(headers_content).Require();
}

void HttpRequest::SetPayload(const Payload& payload)
{
    SetContent(payload.ToString());
}

void HttpRequest::SetJSON(const JSONContent& json)
{
    SetContent(json.ToString());
}

void HttpRequest::SetMultipart(const Multipart& multipart)
{
    SetContent(multipart.ToString());
}

void HttpRequest::SetReadResponseHandler(ReadResponseHandler handler)
{
    read_response_handler_ = std::move(handler);
}

HttpResponse HttpRequest::Start()
{
    FORCE_AS_NON_CONST_FUNCTION();

    void* body_data = nullptr;
    DWORD body_size = 0;

    if (!body_.empty()) {
        ENSURE(CHECK, body_.size() <= std::numeric_limits<DWORD>::max())(body_.size()).Require();
        body_data = const_cast<char*>(body_.data());
        body_size = static_cast<DWORD>(body_.size());
    }

    BOOL success = HttpSendRequestW(request_.get(), nullptr, 0, body_data, body_size);
    ENSURE(THROW, success == TRUE)(kbase::LastError()).Require();

    // Read response then.

    int response_status_code = 0;
    DWORD status_code_size = sizeof(response_status_code);
    success = HttpQueryInfoW(request_.get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                             &response_status_code, &status_code_size, nullptr);
    ENSURE(THROW, success == TRUE)(kbase::LastError()).Require();

    Headers response_headers;
    bool complete = ReadResponseHeaders(request_.get(), response_headers);
    ENSURE(CHECK, complete)(kbase::LastError()).Require();

    std::string response_body;
    std::string* body_ptr = (load_flags_.flags & LoadFlags::DoNotSaveResponseBody) ?
                                nullptr : &response_body;
    complete = ReadResponseBody(request_.get(), body_ptr, read_response_handler_);
    ENSURE(CHECK, complete)(kbase::LastError()).Require();

    return HttpResponse(response_status_code,
                        std::move(response_headers),
                        std::move(response_body));
}

void HttpRequest::SetContent(RequestContent&& content)
{
    std::wstring content_type;
    std::string content_data;
    std::tie(content_type, content_data) = std::move(content);

    SetContentHeader(request_.get(), content_type);

    body_ = std::move(content_data);
}

}   // namespace wat
