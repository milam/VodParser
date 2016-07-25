#include "vod.h"

#include "http.h"
#include "url.h"
#include "path.h"

std::string format_time(double t, char const* fmt) {
  double m = floor(t / 60);
  t -= m * 60;
  double h = floor(m / 60);
  m -= h * 60;
  return fmtstring(fmt, static_cast<int>(h), static_cast<int>(m), static_cast<int>(t));
}

VOD::VOD(int id)
  : vod_id(id)
  , vod_width(0)
  , vod_height(0)
  , cache_dir(path::root() / fmtstring("%d", id) / "cache")
{
  File info_file(cache_dir / "info.json");
  if (!info_file) {
    info_file = HttpRequest::get(fmtstring("https://api.twitch.tv/kraken/videos/v%d", id));
    if (!info_file) throw Exception("failed to load VOD %d", id);
    File(cache_dir / "info.json", "wb").copy(info_file);
    info_file.seek(0);
  }
  json::parse(info_file, vod_info, json::mJSON, nullptr, true);
  info_file.release();

  File listing(cache_dir / "listing.txt");
  if (!listing) {
    File token_file = HttpRequest::get(fmtstring("https://api.twitch.tv/api/vods/%d/access_token", id));
    if (!token_file) throw Exception("failed to load VOD %d", id);
    json::Value token;
    json::parse(token_file, token, json::mJSON, nullptr, true);
    token_file.release();

    listing = HttpRequest::get(fmtstring("http://usher.twitch.tv/vod/%d?nauthsig=%s&nauth=%s", id, token["sig"].getString().c_str(), token["token"].getString().c_str()));
    if (!listing) throw Exception("failed to load VOD %d", id);
    File(cache_dir / "listing.txt", "wb").copy(listing);
    listing.seek(0);
  }

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

  File video_header(cache_dir / "video.txt");
  if (!video_header) {
    video_header = HttpRequest::get(video_url_string);
    if (!video_header) throw Exception("failed to load VOD %d", id);
    File(cache_dir / "video.txt", "wb").copy(video_header);
    video_header.seek(0);
  }

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

bool VOD::load(size_t index, Chunk& chunk, bool existing) const {
  if (index > chunks.size()) return false;

  chunk = chunks[index];

  url_t piece_url;
  if (!parse_url(chunk.path.c_str(), &piece_url, &video_url)) return false;
  chunk.path = cache_dir / fmtstring("chunk%06u.ts", index);

  if (!File::exists(chunk.path)) {
    if (existing) return false;
    File video_piece = HttpRequest::get(serialize_url(&piece_url));
    if (!video_piece) return false;
    File(chunk.path + ".tmp", "wb").copy(video_piece);
    rename_file((chunk.path + ".tmp").c_str(), chunk.path.c_str());
  }
  return true;
}

int VOD::storyboard_index(double time) {
  if (sb_info.type() != json::Value::tObject) {
    if (sb_info.type() == json::Value::tBoolean) return -1;
    std::string local_path = cache_dir / "storyboard.json";
    sb_url = video_url;
    sb_url.path.pop_back();
    sb_url.path.pop_back();
    sb_url.path.push_back("storyboards");
    sb_url.path.push_back(fmtstring("%d-info.json", vod_id));
    sb_info = false;
    File sb_file(local_path);
    if (!sb_file) {
      sb_file = HttpRequest::get(serialize_url(&sb_url));
      if (!sb_file) return -1;
      File(local_path, "wb").copy(sb_file);
      sb_file.seek(0);
    }
    json::Value sbs;
    if (!json::parse(sb_file, sbs) || sbs.type() != json::Value::tArray) return -1;
    sb_info = sbs[sbs.length() - 1];
    sb_images.resize(sb_info["images"].length());
  }

  int index = static_cast<int>(time / sb_info["interval"].getNumber());
  int count = sb_info["count"].getInteger();
  if (index < 0) index = 0;
  if (index >= count) index = count - 1;
  return index;
}
cv::Mat VOD::storyboard_image(int index) {
  int rows = sb_info["rows"].getInteger();
  int cols = sb_info["cols"].getInteger();
  int img_index = (index / (rows * cols));
  if (img_index >= static_cast<int>(sb_images.size())) return cv::Mat();
  if (sb_images[img_index].empty()) {
    std::string image_name = sb_info["images"][img_index].getString();
    std::string local_path = cache_dir / image_name;
    if (!File::exists(local_path)) {
      url_t image_url;
      parse_url(image_name.c_str(), &image_url, &sb_url);
      File image_file = HttpRequest::get(serialize_url(&image_url));
      if (!image_file) return cv::Mat();
      File(local_path, "wb").copy(image_file);
    }
    sb_images[img_index] = cv::imread(local_path);
  }
  if (sb_images[img_index].empty()) return cv::Mat();
  int width = sb_info["width"].getInteger();
  int height = sb_info["height"].getInteger();

  index -= img_index * (rows * cols);
  int row = index / cols;
  int col = index % cols;
  cv::Rect size(cv::Point(0, 0), sb_images[img_index].size());
  return sb_images[img_index](cv::Rect(col * width, row * height, width, height) & size);
}
