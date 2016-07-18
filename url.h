#pragma once

#include "types.h"
#include <string>
#include <vector>

// url parser/serializer
// based on specification at
// https://url.spec.whatwg.org/
// only supports ansi strings

struct url_t {
  bool non_relative;
  bool is_special;
  std::string scheme;
  std::string username;
  std::string password;
  std::string host;
  uint16 port;
  std::vector<std::string> path;
  std::string query;
  std::string fragment;
  uint32 ipv4;
  uint16 ipv6[8];
};

bool parse_url(char const* input, url_t* url, url_t const* base = nullptr, bool* errors = nullptr);
std::string serialize_url(url_t const* url, bool no_fragment = false, bool no_query = false);
