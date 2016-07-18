#include "url.h"
#include "common.h"
#include <cctype>

bool url_code_point(int chr) {
  switch (chr) {
  case '!': case '$': case '&': case '\'': case '(': case ')': case '*':
  case '+': case ',': case '-': case '.': case '/': case ';': case '=':
  case '?': case '@': case '_': case '~': // added space here
    return true;
  }
  // unicode not supported
  return std::isalnum(chr);
}

struct encode_simple {
  static bool contains(int chr) {
    return chr <= 0x1F || chr > 0x7E;
  }
};
struct encode_default {
  static bool contains(int chr) {
    switch (chr) {
    case ' ': case '"': case '#': case '<': case '>':
    case '?': case '`': case '{': case '}':
      return true;
    }
    return encode_simple::contains(chr);
  }
};
struct encode_userinfo {
  static bool contains(int chr) {
    switch (chr) {
    case '/': case ':': case ';': case '=': case '@':
    case '[': case '\\': case ']': case '^': case '|':
      return true;
    }
    return encode_default::contains(chr);
  }
};
struct encode_query {
  static bool contains(int chr) {
    switch (chr) {
    case ' ': case '"': case '#': case '<': case '>':
      return true;
    }
    return encode_simple::contains(chr);
  }
};

template<class encode_set>
void percent_encode(std::string& dst, int chr) {
  if (!encode_set::contains(chr)) {
    dst.push_back(chr);
  } else {
    dst.resize(dst.size() + 3);
    sprintf(&dst[dst.size() - 3], "%%%02X", chr);
  }
}

std::string percent_decode(char const* src) {
  std::string result;
  while (*src) {
    if (*src != '%' || !std::isxdigit(src[1]) || !std::isxdigit(src[2])) {
      result.push_back(*src++);
    } else {
      int bytePoint;
      sscanf(src, "%%%02x", &bytePoint);
      result.push_back(bytePoint);
      src += 3;
    }
  }
  return result;
}

bool ipv6_parse_host(std::string const& input, url_t* url, bool* errors) {
  uint16 pieces[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  uint16* piece = pieces;
  uint16* compress = nullptr;
  char const* ptr = input.c_str();
  if (*ptr == ':') {
    if (ptr[1] != ':') return false;
    ptr += 2;
    compress = ++piece;
  }
  while (*ptr) {
    if (piece == pieces + 8) return false;
    if (*ptr == ':') {
      if (compress) return false;
      ++ptr;
      compress = ++piece;
      continue;
    }
    int value = 0, length = 0;
    if (*ptr != '-' && *ptr != '+') {
      sscanf(ptr, "%4x%n", &value, &length);
    }
    ptr += length;
    if (*ptr == '.') {
      if (!length) return false;
      ptr -= length;
      break;
    } else if (*ptr == ':') {
      if (!*++ptr) return false;
    } else if (*ptr) {
      return false;
    }
    *piece++ = value;
  }
  if (*ptr) {
    if (piece > pieces + 6) return false;
    int dots = 0;
    while (*ptr) {
      int value, length;
      if (!std::isdigit(*ptr) || (*ptr == '0' && std::isdigit(ptr[1]))) {
        return false;
      }
      if (!sscanf(ptr, "%3d%n", &value, &length) || value > 255) {
        return false;
      }
      ptr += length;
      if (dots < 3 && *ptr != '.') return false;
      *piece = (*piece << 8) + value;
      if (dots & 1) {
        ++piece;
      }
      if (*ptr) ++ptr;
      if (dots == 3 && *ptr) return false;
      ++dots;
    }
  }
  if (compress) {
    int swaps = piece - compress;
    piece = pieces + 7;
    while (piece != pieces && swaps) {
      std::swap(*piece--, compress[--swaps]);
    }
  } else if (piece != pieces + 8) {
    return false;
  }
  memcpy(url->ipv6, pieces, sizeof pieces);
  // serialize
  url->host = "[";
  int longest = 0, current = 0;
  compress = nullptr;
  for (piece = pieces; piece < pieces + 8; ++piece) {
    if (*piece == 0) {
      if (++current > longest) {
        compress = piece - current + 1;
      }
    }
  }
  for (piece = pieces; piece < pieces + 8; ++piece) {
    if (compress == piece) {
      url->host.append(piece == pieces ? "::" : ":");
      uint16* next = piece;
      while (next < pieces + 8 && !*next) {
        ++next;
      }
      if (next < pieces + 8) {
        piece = next;
        continue;
      }
    }
    char buffer[8];
    sprintf(buffer, "%x", *piece);
    url->host.append(buffer);
    if (piece != pieces + 7) {
      url->host.push_back(':');
    }
  }
  url->host.push_back(']');
  return true;
}

bool ipv4_parse_number(char const* input, uint32* number, bool* errors) {
  int end;
  if (input[0] == '0' && input[1]) {
    *errors = true;
  }
  if (input[0] == '-' || input[0] == '+') {
    return false;
  }
  return sscanf(input, "%i%n", number, &end) == 1 && !input[end];
}

bool ipv4_parse_host(std::string const& input, url_t* url, bool* errors) {
  url->host = input;
  auto parts = split(input, '.');
  if (parts.back().empty()) {
    *errors = true;
    parts.pop_back();
  }
  if (parts.size() > 4) return true;
  uint32 result = 0;
  for (size_t i = 0; i < parts.size(); ++i) {
    uint32 number;
    if (!ipv4_parse_number(parts[i].c_str(), &number, errors)) {
      return true;
    }
    if (number >= 256) {
      *errors = true;
      if (i < parts.size() - 1 || number >= (1U << (40 - 8 * parts.size()))) {
        return false;
      }
    }
    result += number << (24 - 8 * i);
  }
  url->ipv4 = result;
  char buffer[32];
  sprintf(buffer, "%d.%d.%d.%d", (result >> 24) & 0xFF, (result >> 16) & 0xFF,
    (result >> 8) & 0xFF, result & 0xFF);
  url->host = buffer;
  return true;
}

bool parse_host(std::string const& input, url_t* url, bool* errors) {
  if (input[0] == '[') {
    if (input.back() != ']') return false;
    return ipv6_parse_host(input.substr(1, input.size() - 2), url, errors);
  }

  std::string domain = percent_decode(input.c_str());
  for (char chr : domain) {
    switch (chr) {
    case 0x00: case 0x09: case 0x0A: case 0x0D: case 0x20:
    case '#': case '%': case '/': case ':': case '?':
    case '@': case '[': case '\\': case ']':
      return false;
    }
  }

  return ipv4_parse_host(domain, url, errors);
}

void reset_url(url_t* url) {
  url->non_relative = false;
  url->is_special = false;
  url->scheme.clear();
  url->username.clear();
  url->password.clear();
  url->host.clear();
  url->port = 0;
  url->path.clear();
  url->query.clear();
  url->fragment.clear();
  url->ipv4 = 0;
  memset(url->ipv6, 0, sizeof url->ipv6);
}

enum copy_flags{
  c_scheme      = 0x01,
  c_username    = 0x02,
  c_password    = 0x04,
  c_host        = 0x08,
  c_port        = 0x10,
  c_path        = 0x20,
  c_query       = 0x40,
  c_fragment    = 0x80,
};
void copy_url(url_t* src, url_t const* base, int flags) {
  if (flags & c_scheme) {
    src->scheme = base->scheme;
    src->is_special = base->is_special;
  }
  if (flags & c_username) src->username = base->username;
  if (flags & c_password) src->password = base->password;
  if (flags & c_host) {
    src->host = base->host;
    src->ipv4 = base->ipv4;
    memcpy(src->ipv6, base->ipv6, sizeof base->ipv6);
  }
  if (flags & c_port) src->port = base->port;
  if (flags & c_path) src->path = base->path;
  if (flags & c_query) src->query = base->query;
  if (flags & c_fragment) src->fragment = base->fragment;
}

uint16 scheme_port(char const* scheme) {
  if (!strcmp(scheme, "ftp")) return 21;
  if (!strcmp(scheme, "gopher")) return 70;
  if (!strcmp(scheme, "http")) return 80;
  if (!strcmp(scheme, "https")) return 443;
  if (!strcmp(scheme, "ws")) return 80;
  if (!strcmp(scheme, "wss")) return 443;
  return 0;
}

bool is_drive(std::string const& str) {
  return str.size() == 2 && std::isalpha(str[0]) && (str[1] == ':' || str[1] == '|');
}
bool is_normalized_drive(std::string const& str) {
  return str.size() == 2 && std::isalpha(str[0]) && str[1] == ':';
}

void pop_path(std::vector<std::string>& path) {
  if (path.empty()) return;
  if (path.size() == 1 && is_normalized_drive(path[0])) {
    return;
  }
  path.pop_back();
}

bool parse_url(char const* input, url_t* url, url_t const* base, bool* errors) {
  enum state_t {
    state_scheme_start,
    state_scheme,
    state_no_scheme,
    state_relative_or_authority,
    state_path_or_authority,
    state_relative,
    state_relative_slash,
    state_authority_slashes,
    state_authority_ignore_slashes,
    state_authority,
    state_host,
    state_port,
    state_file,
    state_file_slash,
    state_file_host,
    state_path_start,
    state_path,
    state_non_relative_path,
    state_query,
    state_fragment,
  };

  size_t length = strlen(input);
  if (!length) return false;
  for (size_t i = 0; i < length; ++i) {
    if (input[i] < 0x00 || input[i] > 0x7E) {
      // no unicode support
      return false;
    }
    if (input[i] == 0x0D || input[i] == 0x0A || input[i] == 0x09) {
      // no tab or newline
      return false;
    }
  }

  bool s_errors = false;
  if (errors) {
    *errors = false;
  } else {
    errors = &s_errors;
  }

  if (input[0] <= 0x20 || input[length - 1] <= 0x20) {
    // leading or trailing C0-controls and space
    *errors = true;
    while (*input <= 0x20) {
      ++input;
      --length;
    }
    while (length && input[length - 1] <= 0x20) {
      --length;
    }
  }

  state_t state = state_scheme_start;
  reset_url(url);

  bool at_flag = false;
  bool bracket_flag = false;

  std::string buffer;
  char const* ptr = input;
  while (true) {
    switch (state) {
    case state_scheme_start:
      if (std::isalpha(*ptr)) {
        buffer.push_back(std::tolower(*ptr));
        state = state_scheme;
      } else {
        state = state_no_scheme;
        --ptr;
      }
      break;
    case state_scheme:
      if (std::isalnum(*ptr) || *ptr == '+' || *ptr == '-' || *ptr == '.') {
        buffer.push_back(std::tolower(*ptr));
      } else if (*ptr == ':') {
        url->scheme = buffer;
        url->is_special = (url->scheme == "ftp" || url->scheme == "gopher" || url->scheme == "http" ||
          url->scheme == "https" || url->scheme == "ws" || url->scheme == "wss" || url->scheme == "file");
        buffer.clear();
        if (url->scheme == "file") {
          if (ptr[1] != '/' || ptr[2] != '/') {
            *errors = true;
          }
          state = state_file;
        } else if (url->is_special) {
          if (base && base->scheme == url->scheme) {
            state = state_relative_or_authority;
          } else {
            state = state_authority_slashes;
          }
        } else if (ptr[1] == '/') {
          state = state_path_or_authority;
          ++ptr;
        } else {
          url->non_relative = true;
          url->path.push_back("");
          state = state_non_relative_path;
        }
      } else {
        buffer.clear();
        ptr = input - 1;
        state = state_no_scheme;
      }
      break;
    case state_no_scheme:
      if (!base || (base->non_relative && *ptr != '#')) {
        return false;
      } else if (base->non_relative && *ptr == '#') {
        copy_url(url, base, c_scheme | c_path | c_query);
        url->non_relative = true;
        state = state_fragment;
      } else if (base->scheme != "file") {
        state = state_relative;
        --ptr;
      } else {
        state = state_file;
        --ptr;
      }
      break;
    case state_relative_or_authority:
      if (ptr[0] == '/' && ptr[1] == '/') {
        state = state_authority_ignore_slashes;
        ++ptr;
      } else {
        *errors = true;
        state = state_relative;
        --ptr;
      }
      break;
    case state_path_or_authority:
      if (*ptr == '/') {
        state = state_authority;
      } else {
        state = state_path;
        --ptr;
      }
      break;
    case state_relative:
      copy_url(url, base, c_scheme);
      switch (*ptr) {
      case 0:
        copy_url(url, base, c_username | c_password | c_host | c_port | c_path | c_query);
        break;
      case '/':
        state = state_relative_slash;
        break;
      case '?':
        copy_url(url, base, c_username | c_password | c_host | c_port | c_path);
        state = state_query;
        break;
      case '#':
        copy_url(url, base, c_username | c_password | c_host | c_port | c_path | c_query);
        state = state_fragment;
        break;
      default:
        if (url->is_special && *ptr == '\\') {
          *errors = true;
          state = state_relative_slash;
        } else {
          copy_url(url, base, c_username | c_password | c_host | c_port | c_path);
          if (url->path.size()) {
            url->path.pop_back();
          }
          state = state_path;
          --ptr;
        }
      }
      break;
    case state_relative_slash:
      if (*ptr == '/' || (url->is_special && *ptr == '\\')) {
        if (*ptr == '\\') {
          *errors = true;
        }
        state = state_authority_ignore_slashes;
      } else {
        copy_url(url, base, c_username | c_password | c_host | c_port);
        state = state_path;
        --ptr;
      }
      break;
    case state_authority_slashes:
      if (ptr[0] == '/' && ptr[1] == '/') {
        state = state_authority_ignore_slashes;
        ++ptr;
      } else {
        *errors = true;
        state = state_authority_ignore_slashes;
        --ptr;
      }
      break;
    case state_authority_ignore_slashes:
      if (*ptr != '/' && *ptr != '\\') {
        state = state_authority;
        --ptr;
      } else {
        *errors = true;
      }
      break;
    case state_authority:
      if (*ptr == '@') {
        *errors = true;
        if (at_flag) {
          buffer = "%40" + buffer;
        }
        at_flag = true;
        bool password = false;
        for (char chr : buffer) {
          if (chr == ':') {
            password = true;
          } else if (password) {
            percent_encode<encode_userinfo>(url->password, chr);
          } else {
            percent_encode<encode_userinfo>(url->username, chr);
          }
        }
        buffer.clear();
      } else if (*ptr == 0 || *ptr == '/' || *ptr == '?' || *ptr == '#' || (url->is_special && *ptr == '\\')) {
        ptr -= buffer.size() + 1;
        buffer.clear();
        state = state_host;
      } else {
        buffer.push_back(*ptr);
      }
      break;
    case state_host:
      if (*ptr == ':' && !bracket_flag) {
        if (url->is_special && buffer.empty()) {
          return false;
        }
        if (!parse_host(buffer, url, errors)) {
          return false;
        }
        buffer.clear();
        state = state_port;
      } else if (*ptr == 0 || *ptr == '/' || *ptr == '?' || *ptr == '#' || (url->is_special && *ptr == '\\')) {
        --ptr;
        if (url->is_special && buffer.empty()) {
          return false;
        }
        if (!parse_host(buffer, url, errors)) {
          return false;
        }
        buffer.clear();
        state = state_path_start;
      } else {
        if (*ptr == '[') {
          bracket_flag = true;
        } else if (*ptr == ']') {
          bracket_flag = false;
        }
        buffer.push_back(*ptr);
      }
      break;
    case state_port:
      if (std::isdigit(*ptr)) {
        buffer.push_back(*ptr);
      } else if (*ptr == 0 || *ptr == '/' || *ptr == '?' || *ptr == '#' || (url->is_special && *ptr == '\\')) {
        if (buffer.size() > 5) return false;
        int length, port;
        if (sscanf(buffer.c_str(), "%d%n", &port, &length) != 1 || static_cast<size_t>(length) != buffer.size() || port > 65535) {
          return false;
        }
        url->port = (port == scheme_port(url->scheme.c_str()) ? 0 : port);
        buffer.clear();
        state = state_path_start;
        --ptr;
      } else {
        return false;
      }
      break;
    case state_file:
      url->scheme = "file";
      url->is_special = true;
      switch (*ptr) {
      case 0:
        if (base && base->scheme == "file") {
          copy_url(url, base, c_host | c_path | c_query);
        }
        break;
      case '\\':
        *errors = true;
        // fall through
      case '/':
        state = state_file_slash;
        break;
      case '?':
        if (base && base->scheme == "file") {
          copy_url(url, base, c_host | c_path);
        }
        state = state_query;
        break;
      case '#':
        if (base && base->scheme == "file") {
          copy_url(url, base, c_host | c_path | c_query);
        }
        state = state_fragment;
        break;
      default:
        if (base && base->scheme == "file" && (!std::isalpha(ptr[0]) || (ptr[1] != ':' && ptr[1] != '|') ||
          ptr[2] == 0 || (ptr[2] != '/' && ptr[2] != '\\' && ptr[2] != '?' && ptr[2] != '#')))
        {
          copy_url(url, base, c_host | c_path);
          pop_path(url->path);
        } else if (base && base->scheme == "file") {
          return false;
        }
        state = state_path;
        --ptr;
      }
      break;
    case state_file_slash:
      if (*ptr == '/' || *ptr == '\\') {
        if (*ptr == '\\') *errors = true;
        state = state_file_host;
      } else {
        if (base && base->scheme == "file" && base->path.size() && is_normalized_drive(base->path[0])) {
          url->path.push_back(base->path[0]);
        }
        state = state_path;
        --ptr;
      }
      break;
    case state_file_host:
      if (*ptr == 0 || *ptr == '/' || *ptr == '\\' || *ptr == '?' || *ptr == '#') {
        --ptr;
        if (is_drive(buffer)) {
          *errors = true;
          state = state_path;
        } else if (buffer.empty()) {
          state = state_path_start;
        } else {
          if (!parse_host(buffer, url, errors)) {
            return false;
          }
          if (url->host == "localhost") {
            url->host.clear();
          }
          buffer.clear();
          state = state_path_start;
        }
      } else {
        buffer.push_back(*ptr);
      }
      break;
    case state_path_start:
      if (url->is_special && *ptr == '\\') {
        *errors = true;
      }
      state = state_path;
      if (*ptr != '/' && (!url->is_special || *ptr != '\\')) {
        --ptr;
      }
      break;
    case state_path:
      if (*ptr == 0 || *ptr == '/' || (url->is_special && *ptr == '\\') || *ptr == '?' || *ptr == '#') {
        if (url->is_special && *ptr == '\\') {
          *errors = true;
        }
        if (buffer == "..") {
          pop_path(url->path);
          if (*ptr != '/' && (!url->is_special || *ptr != '\\')) {
            url->path.push_back("");
          }
        } else if (buffer == "." && *ptr != '/' && (!url->is_special || *ptr != '\\')) {
          url->path.push_back("");
        } else if (buffer != ".") {
          if (url->scheme == "file" && url->path.empty() && is_drive(buffer)) {
            if (url->host.size()) *errors = true;
            url->host.clear();
            buffer[1] = ':';
          }
          url->path.push_back(buffer);
        }
        buffer.clear();
        if (*ptr == '?') {
          state = state_query;
        } else if (*ptr == '#') {
          state = state_fragment;
        }
      } else {
        if (!url_code_point(*ptr) && *ptr != '%') {
          *errors = true;
        } else if (*ptr == '%' && (!std::isxdigit(ptr[1]) || !std::isxdigit(ptr[2]))) {
          *errors = true;
        }
        if (*ptr == '%' && ptr[1] == '2' && (ptr[2] == 'e' || ptr[2] == 'E')) {
          buffer.push_back('.');
          ptr += 2;
        } else {
          percent_encode<encode_default>(buffer, *ptr);
        }
      }
      break;
    case state_non_relative_path:
      if (*ptr == '?') {
        state = state_query;
      } else if (*ptr == '#') {
        state = state_fragment;
      } else {
        if (*ptr != 0 && !url_code_point(*ptr) && *ptr != '%') {
          *errors = true;
        } else if (*ptr == '%' && (!std::isxdigit(ptr[1]) || !std::isxdigit(ptr[2]))) {
          *errors = true;
        }
        if (*ptr) {
          percent_encode<encode_simple>(url->path[0], *ptr);
        }
      }
      break;
    case state_query:
      if (*ptr == '#') {
        state = state_fragment;
      } else {
        if (*ptr != 0 && !url_code_point(*ptr) && *ptr != '%') {
          *errors = true;
        } else if (*ptr == '%' && (!std::isxdigit(ptr[1]) || !std::isxdigit(ptr[2]))) {
          *errors = true;
        }
        if (*ptr) {
          percent_encode<encode_query>(url->query, *ptr);
        }
      }
      break;
    case state_fragment:
      if (*ptr != 0 && !url_code_point(*ptr) && *ptr != '%') {
        *errors = true;
      } else if (*ptr == '%' && (!std::isxdigit(ptr[1]) || !std::isxdigit(ptr[2]))) {
        *errors = true;
      }
      if (*ptr) {
        url->query.push_back(*ptr);
      }
      break;
    }

    if (ptr < input + length) {
      ++ptr;
    } else {
      break;
    }
  }

  return true;
}

void set_url_username(url_t* url, char const* username) {
  url->username.clear();
  while (*username) {
    percent_encode<encode_userinfo>(url->username, *username++);
  }
}
void set_url_password(url_t* url, char const* password) {
  url->password.clear();
  while (*password) {
    percent_encode<encode_userinfo>(url->password, *password++);
  }
}

std::string serialize_url(url_t const* url, bool no_fragment, bool no_query) {
  std::string output(url->scheme);
  output.push_back(':');
  if (url->host.size()) {
    output.append("//");
    if (url->username.size() || url->password.size()) {
      output.append(url->username);
      if (url->password.size()) {
        output.push_back(':');
        output.append(url->password);
      }
      output.push_back('@');
    }
    output.append(url->host);
    if (url->port) {
      output.push_back(':');
      char buffer[8];
      sprintf(buffer, "%d", url->port);
      output.append(buffer);
    }
  } else if (url->scheme == "file") {
    output.append("//");
  }
  if (url->non_relative) {
    output.append(url->path[0]);
  } else {
    for (auto const& part : url->path) {
      output.push_back('/');
      output.append(part);
    }
  }
  if (!no_query && url->query.size()) {
    output.push_back('?');
    output.append(url->query);
  }
  if (!no_fragment && url->fragment.size()) {
    output.push_back('#');
    output.append(url->fragment);
  }
  return output;
}
