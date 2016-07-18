#include "vod.h"

#include "http.h"
#include "json.h"
#include "url.h"
#include "path.h"

VOD::VOD(int id)
  : vod_id(id)
  , vod_width(0)
  , vod_height(0)
  , cache_dir(fmtstring("%d/cache", id))
{
  File token_file = HttpRequest::get(fmtstring("https://api.twitch.tv/api/vods/%d/access_token", id));
  if (!token_file) throw Exception("failed to load VOD %d", id);
  json::Value token;
  json::parse(token_file, token, json::mJSON, nullptr, true);
  token_file.release();

  File listing = HttpRequest::get(fmtstring("http://usher.twitch.tv/vod/%d?nauthsig=%s&nauth=%s", id, token["sig"].getString().c_str(), token["token"].getString().c_str()));
  if (!listing) throw Exception("failed to load VOD %d", id);
  File("listing.txt", "wb").copy(listing);
  listing.seek(0);
  std::string video_url_string;
  for (std::string const& line : listing) {
    if (line.empty()) continue;
    if (line[0] == '#') {
      char const* sub = strstr(line.c_str(), "RESOLUTION=");
      int w, h;
      if (sub && sscanf(sub, "RESOLUTION=\"%dx%d\"", &w, &h) == 2) {
        vod_width = w;
        vod_height = h;
      }
    } else {
      video_url_string = line;
      break;
    }
  }
  if (video_url_string.empty() || !parse_url(video_url_string.c_str(), &video_url)) throw Exception("failed to load VOD %d", id);
  listing.release();

  File video_header = HttpRequest::get(video_url_string);
  if (!video_header) throw Exception("failed to load VOD %d", id);
  double next_time = 0;
  double next_start = 0;
  for (std::string const& line : video_header) {
    if (line.empty()) continue;
    if (line[0] == '#') {
      double value;
      if (sscanf(line.c_str(), "#EXTINF:%lf,", &value) == 1) {
        next_time = value;
      }
    } else {
      Chunk p;
      p.index = chunks.size();
      p.start = next_start;
      p.duration = next_time;
      p.path = line;
      chunks.push_back(p);
      next_start += next_time;
      next_time = 0;
    }
  }
}

double VOD::duration(size_t pos) const {
  if (pos >= chunks.size()) {
    if (chunks.empty()) return 0;
    return chunks.back().start + chunks.back().duration;
  } else {
    return chunks[pos].start;
  }
}

size_t VOD::find(double time) const {
  size_t left = 0, right = chunks.size();
  while (right - left > 1) {
    size_t mid = (left + right) / 2;
    if (chunks[mid].start > time) {
      right = mid;
    } else {
      left = mid;
    }
  }
  return left;
}

bool VOD::load(size_t index, Chunk& chunk) const {
  if (index > chunks.size()) return false;

  chunk = chunks[index];

  url_t piece_url;
  if (!parse_url(chunk.path.c_str(), &piece_url, &video_url)) return false;
  chunk.path = cache_dir / fmtstring("chunk%06u.ts", index);

  if (!File::exists(chunk.path)) {
    File video_piece = HttpRequest::get(serialize_url(&piece_url));
    if (!video_piece) return false;
    File(chunk.path, "wb").copy(video_piece);
  }
  return true;
}
