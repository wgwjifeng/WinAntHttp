/*
 @ 0xCCCCCCCC
*/

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef WINANT_HTTP_WINANT_API_H_
#define WINANT_HTTP_WINANT_API_H_

#include "winant_http/winant_request.h"
#include "winant_http/winant_request_builder.h"
#include "winant_http/winant_response.h"

namespace {

using wat::HttpRequest;
using wat::HttpRequestBuilder;

template<typename Arg>
void SetRequestBuilder(HttpRequestBuilder& builder, Arg&& arg)
{
    builder.SetOption(std::forward<Arg>(arg));
}

template<typename Arg, typename ...Args>
void SetRequestBuilder(HttpRequestBuilder& builder, Arg&& arg, Args&&... args)
{
    SetRequestBuilder(builder, std::forward<Arg>(arg));
    SetRequestBuilder(builder, std::forward<Args>(args)...);
}

template<typename ...Args>
HttpRequest BuildRequest(HttpRequest::Method method, Args&&... args)
{
    HttpRequestBuilder builder(method);

    SetRequestBuilder(builder, std::forward<Args>(args)...);

    return builder.Build();
}

}   // namespace

namespace wat {

template<typename ...Args>
HttpResponse Get(Args&&... args)
{
    BuildRequest(HttpRequest::Method::Get, std::forward<Args>(args)...);
    // TODO: execute request and get the associated HttpResponse.
    return HttpResponse();
}

HttpResponse Post();

}   // namespace wat

#endif  // WINANT_HTTP_WINANT_API_H_
