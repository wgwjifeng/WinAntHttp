/*
 @ 0xCCCCCCCC
*/

#include "gtest/gtest.h"

#include "winant_http/winant_api.h"
#include "winant_http/winant_common_types.h"

#include "kbase/string_format.h"
#include "kbase/string_util.h"

namespace wat {

TEST(TypeUrl, GeneralUsage)
{
    Url empty_url;
    EXPECT_TRUE(empty_url.empty());

    Url url("http://foobar.com");
    EXPECT_FALSE(url.empty());

    const std::string test_url = "http://foo.bar.com";
    url = Url(test_url);
    EXPECT_EQ(test_url, url.spec());
}

TEST(TypeHeaders, GeneralUsage)
{
    Headers empty_headers;
    EXPECT_TRUE(empty_headers.empty());

    Headers headers {
        {"key1", "value1"},
        {"key2", "value2"}
    };
    EXPECT_FALSE(headers.empty());

    headers = Headers {
        {"cookie", "blah"},
        {"range", "12345"}
    };
    EXPECT_FALSE(headers.HasHeader("key1"));
    EXPECT_FALSE(headers.HasHeader("key2"));

    headers.SetHeader("etag", "0xdeadbeef");
    EXPECT_TRUE(headers.HasHeader("etag"));

    std::string header_value;
    EXPECT_TRUE(headers.GetHeader("cookie", header_value));
    EXPECT_EQ(header_value, "blah");
    EXPECT_TRUE(headers.GetHeader("range", header_value));
    EXPECT_EQ(header_value, "12345");
    EXPECT_TRUE(headers.GetHeader("etag", header_value));
    EXPECT_EQ(header_value, "0xdeadbeef");
    EXPECT_FALSE(headers.GetHeader("non-exist", header_value));

    // Won't cause any trouble
    headers.RemoveHeader("non-exist");

    headers.RemoveHeader("cookie");
    EXPECT_FALSE(headers.HasHeader("cookie"));

    headers.clear();
    EXPECT_TRUE(headers.empty());
}

TEST(TypeHeaders, Iteration)
{
    Headers headers {
        {"key1", "value1"},
        {"key2", "value2"},
        {"key3", "value3"}
    };

    for (const auto& header : headers) {
        EXPECT_TRUE(!header.first.empty() && !header.second.empty());
        // Watch out if you changed literal content in headers above.
        EXPECT_EQ(header.first.back(), header.second.back());
    }
}

TEST(TypeHeaders, ToString)
{
    Headers headers {
        {"key1", "value1"},
        {"key2", ""},
        {"key3", "value3"}
    };

    const char kExpectedHeaderStr[] = "key1: value1\r\nkey2:\r\nkey3: value3\r\n\r\n";

    auto header_string = headers.ToString();
    ASSERT_TRUE(!header_string.empty());
    EXPECT_TRUE(kbase::EndsWith(header_string, "\r\n\r\n"));
    EXPECT_EQ(kExpectedHeaderStr, header_string);
}

TEST(TypeParameters, GeneralUsage)
{
    Parameters empty_params;
    EXPECT_TRUE(empty_params.empty());

    Parameters params { {"access_key", "token123"} };
    EXPECT_FALSE(params.empty());

    params.Add({"uid", "789"}).Add({"appkey", "winant http"});

    // Test multiple pairs with the same key.
    params.Add({"appkey", "backup&winant"});

    const char query_string[] =
        "access_key=token123&uid=789&appkey=winant%20http&appkey=backup%26winant";
    EXPECT_EQ(query_string, params.ToString());
}

TEST(TypeParameters, Empty)
{
    Parameters empty_params;
    EXPECT_TRUE(empty_params.empty());

    // Bypass internal empty detection.
    Parameters empty_content {{"", ""}};
    EXPECT_FALSE(empty_content.empty());
    EXPECT_TRUE(empty_content.ToString().empty());
}

TEST(TypePayload, GeneralUsage)
{
    Payload empty_payload;
    EXPECT_TRUE(empty_payload.empty());

    Payload payload {{"token", "token123"}};
    EXPECT_FALSE(payload.empty());
    payload.Add({"uid", "kcno.1"}).Add({"app", "winant http"});

    const wchar_t type[] = L"Content-Type: application/x-www-form-urlencoded\r\n";
    const char data[] = "token=token123&uid=kcno.1&app=winant%20http";
    auto content = payload.ToString();
    EXPECT_EQ(type, content.first);
    EXPECT_EQ(data, content.second);
}

TEST(TypeJSONContent, GeneralUsage)
{
    JSONContent json_data;
    EXPECT_TRUE(json_data.empty());

    const wchar_t type[] = L"Content-Type: application/json\r\n";
    const char json_str[] = R"({"code": 0, "msg": "success"})";
    json_data.data = json_str;
    EXPECT_FALSE(json_data.empty());
    auto content = json_data.ToString();
    EXPECT_EQ(type, content.first);
    EXPECT_EQ(json_str, content.second);
}

TEST(TypeMultipart, Empty)
{
    Multipart part;
    EXPECT_TRUE(part.empty());

    part.AddPart(Multipart::Value {"key", "value"});
    EXPECT_FALSE(part.empty());
}

TEST(TypeMultipart, Generation)
{
    Multipart upload;
    Multipart::File file {"file", "test.txt", Multipart::File::kDefaultMimeType, "hello, world!"};
    upload.AddPart(std::move(file)).AddPart(Multipart::Value {"file_size", "unknown"});

    auto content = upload.ToString();
    kbase::WStringView type = L"Content-Type: multipart/form-data; boundary=";
    EXPECT_TRUE(kbase::StartsWith(content.first, type));
    EXPECT_TRUE(kbase::EndsWith(content.first, L"\r\n"));

    std::cout << content.second << std::endl;

    auto boundary = kbase::WideToASCII(content.first.substr(content.first.find('=') + 1));
    kbase::EraseChars(boundary, "\r\n");
    constexpr const char* data_template =
        "--{0}\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"test.txt\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n"
        "hello, world!\r\n"
        "--{0}\r\n"
        "Content-Disposition: form-data; name=\"file_size\"\r\n\r\n"
        "unknown\r\n"
        "--{0}--\r\n";
    auto expected = kbase::StringFormat(data_template, boundary);
    EXPECT_EQ(expected, content.second);
}

TEST(TypeLoadFlags, DoNotSaveResponseBody)
{
    constexpr char kHost[] = "https://httpbin.org/get";

    auto response = Get(Url(kHost), LoadFlags(LoadFlags::DoNotSaveResponseBody));
    EXPECT_EQ(200, response.status_code());
    EXPECT_TRUE(response.text().empty());
    std::string content_type;
    EXPECT_TRUE(response.headers().GetHeader("Content-Type", content_type));
    EXPECT_EQ("application/json", content_type);
}

TEST(TypeReadResponseHandler, UseAsDownloader)
{
    constexpr char kHost[] = "https://httpbin.org/get";

    std::string data;
    auto response_saver = [&data](const char* buf, int bytes_read) {
        if (bytes_read > 0) {
            data.append(buf, bytes_read);
        } else if (bytes_read == 0) {
            data.append("\n--data end--\n");
        }
    };

    auto response = Get(Url(kHost), LoadFlags(LoadFlags::DoNotSaveResponseBody),
                        ReadResponseHandler(response_saver));
    EXPECT_EQ(200, response.status_code());
    EXPECT_TRUE(response.text().empty());
    EXPECT_FALSE(data.empty());
    EXPECT_TRUE(kbase::EndsWith(data, "--data end--\n"));
    std::cout << data;
}

}   // namespace wat
