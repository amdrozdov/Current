/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2016 Maxim Zhurovich <zhurovich@gmail.com>
          (c) 2016 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

// `Basic` is a boilerplate example of how to customize RESTful access to Current Storage.
// This basic implementation supports GET, POST, and DELETE, with rudimentary text-only messages on errors.

#ifndef CURRENT_STORAGE_REST_BASIC_H
#define CURRENT_STORAGE_REST_BASIC_H

#include "../storage.h"

#include "../../Blocks/HTTP/api.h"

namespace current {
namespace storage {
namespace rest {

struct Basic {
  template <class HTTP_VERB, typename ALL_FIELDS, typename PARTICULAR_FIELD, typename ENTRY, typename KEY>
  struct RESTful;

  static void RegisterTopLevel(HTTPRoutesScope& scope,
                               const std::vector<std::string>& fields,
                               int port,
                               const std::string& path_prefix,
                               const std::string& restful_url_prefix) {
    static_cast<void>(scope);
    static_cast<void>(fields);
    static_cast<void>(port);
    static_cast<void>(path_prefix);
    static_cast<void>(restful_url_prefix);
  }

  template <typename F>
  static void ExtractKeyFromURLAndNext(Request request, F&& next) {
    if (request.url.query.has("key")) {
      next(std::move(request), request.url.query["key"]);
    } else if (!request.url_path_args.empty()) {
      next(std::move(request), request.url_path_args[0]);
    } else {
      request("Need resource key in the URL.\n", HTTPResponseCode.BadRequest);
    }
  }

  template <typename ALL_FIELDS, typename PARTICULAR_FIELD, typename ENTRY, typename KEY>
  struct RESTful<GET, ALL_FIELDS, PARTICULAR_FIELD, ENTRY, KEY> {
    template <typename F>
    void Enter(Request request, F&& next) {
      ExtractKeyFromURLAndNext(std::move(request), std::forward<F>(next));
    }
    template <class INPUT>
    Response Run(const INPUT& input) const {
      const ImmutableOptional<ENTRY> result = input.field[input.key];
      if (Exists(result)) {
        return Value(result);
      } else {
        return Response("Nope.\n", HTTPResponseCode.NotFound);
      }
    }
  };

  template <typename ALL_FIELDS, typename PARTICULAR_FIELD, typename ENTRY, typename KEY>
  struct RESTful<POST, ALL_FIELDS, PARTICULAR_FIELD, ENTRY, KEY> {
    template <typename F>
    void Enter(Request request, F&& next) {
      if (!request.url_path_args.empty()) {
        request("Should not have resource key in the URL.\n", HTTPResponseCode.BadRequest);
      } else {
        next(std::move(request));
      }
    }
    template <class INPUT>
    Response Run(const INPUT& input) const {
      input.field.Add(input.entry);
      return Response("Created.\n", HTTPResponseCode.Created);
    }
    static Response ErrorBadJSON(const std::string&) {
      return Response("Bad JSON.\n", HTTPResponseCode.BadRequest);
    }
  };

  template <typename ALL_FIELDS, typename PARTICULAR_FIELD, typename ENTRY, typename KEY>
  struct RESTful<PUT, ALL_FIELDS, PARTICULAR_FIELD, ENTRY, KEY> {
    template <typename F>
    void Enter(Request request, F&& next) {
      ExtractKeyFromURLAndNext(std::move(request), std::forward<F>(next));
    }
    template <class INPUT>
    Response Run(const INPUT& input) const {
      if (input.entry_key == input.url_key) {
        const bool exists = Exists(input.field[input.entry_key]);
        input.field.Add(input.entry);
        if (exists) {
          return Response("Updated.\n", HTTPResponseCode.OK);
        } else {
          return Response("Created.\n", HTTPResponseCode.Created);
        }
      } else {
        return Response("Object key doesn't match URL key.\n", HTTPResponseCode.BadRequest);
      }
    }
    static Response ErrorBadJSON(const std::string&) {
      return Response("Bad JSON.\n", HTTPResponseCode.BadRequest);
    }
  };

  template <typename ALL_FIELDS, typename PARTICULAR_FIELD, typename ENTRY, typename KEY>
  struct RESTful<DELETE, ALL_FIELDS, PARTICULAR_FIELD, ENTRY, KEY> {
    template <typename F>
    void Enter(Request request, F&& next) {
      ExtractKeyFromURLAndNext(std::move(request), std::forward<F>(next));
    }
    template <class INPUT>
    Response Run(const INPUT& input) const {
      input.field.Erase(input.key);
      return Response("Deleted.\n", HTTPResponseCode.OK);
    }
  };

  static Response ErrorMethodNotAllowed() {
    return Response("Method not allowed.\n", HTTPResponseCode.MethodNotAllowed);
  }
};

}  // namespace rest
}  // namespace storage
}  // namespace current

#endif  // CURRENT_STORAGE_REST_BASIC_H
