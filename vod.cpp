#include "vod.h"

#include "http.h"
#include "url.h"
#include "path.h"
#include <mutex>

std::string format_time(double t, char const* fmt) {
  double m = floor(t / 60);
  t -= m * 60;
  double h = floor(m / 60);
  m -= h * 60;
  return fmtstring(fmt, static_cast<int>(h), static_cast<int>(m), static_cast<int>(t));
}

class VOD : public Video {
public:
  VOD(int id);

  std::string default_output() const override {
    return path::root() / fmtstring("%d", vod_id);
  }
  std::string title() const override {
    return fmtstring("[%d] %s", vod_id, vod_info["title"].getString().c_str());
  }
  void info(json::Value& config) const override {
    config["vod_id"] = vod_id;
  }
  int width() const override {
    return vod_width;
  }
  int height() const override {
    return vod_height;
  }

  size_t size() const override {
    return chunks.size();
  }

  double duration(size_t pos = -1) const override;
  size_t find(double time) const override;
  bool load(size_t index, Chunk& chunk, bool existing = false) override;
  void delete_cache(size_t index) override;

  int storyboard_index(double time) override;
  cv::Mat storyboard_image(int index, bool instant) override;

private:
  json::Value vod_info;
  int vod_id;
  int vod_width, vod_height;
  struct VodChunk {
    double start;
    double duration;
    std::string path;
  };
  std::vector<VodChunk> chunks;
  std::string cache_dir;
  url_t video_url;

  url_t sb_url;
  std::vector<cv::Mat> sb_images;
  json::Value sb_info;
};

static const double CHUNK_DURATION = 4.0;
static const double SB_DURATION = 120.0;

class VideoFile : public Video {
public:
  VideoFile(std::string const& path);

  std::string default_output() const override {
    return path::path(path);
  }
  std::string title() const override {
    return path::name(path);
  }
  void info(json::Value& config) const override {
    config["video_path"] = path;
  }
  int width() const override {
    return static_cast<int>(video.get(cv::CAP_PROP_FRAME_WIDTH));
  }
  int height() const override {
    return static_cast<int>(video.get(cv::CAP_PROP_FRAME_HEIGHT));
  }

  size_t size() const override {
    return chunks;
  }

  double duration(size_t pos = -1) const {
    if (pos >= chunks) return frames / fps;
    return pos * CHUNK_DURATION;
  }
  size_t find(double time) const {
    if (time < 0) return 0;
    size_t index = static_cast<size_t>(time / CHUNK_DURATION);
    if (index >= chunks) index = chunks - 1;
    return index;
  }
  bool load(size_t index, Chunk& chunk, bool existing = false) override {
    std::lock_guard<std::mutex> guard(mutex);
    chunk.frame.release();
    chunk.index = index;
    chunk.start = index * CHUNK_DURATION;
    chunk.duration = (index == chunks - 1 ? frames / fps - chunk.start : CHUNK_DURATION);
    size_t frame = static_cast<size_t>(index * CHUNK_DURATION * fps);
    video.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(std::min(frame, frames - 1)));
    video >> chunk.frame;
    return !chunk.frame.empty();
  }

  int storyboard_index(double time) override {
    int sb_length = static_cast<int>(ceil(frames / fps / SB_DURATION));
    int index = static_cast<size_t>(time / SB_DURATION);
    if (index < 0) index = 0;
    if (index >= sb_length) index = sb_length - 1;
    return index;
  }
  cv::Mat storyboard_image(int index, bool instant) override {
    int sb_length = static_cast<int>(ceil(frames / fps / SB_DURATION));
    {
      std::lock_guard<std::mutex> guard(sb_mutex);
      if (sb_images.size() < sb_length) sb_images.resize(sb_length);
      if (index >= static_cast<int>(sb_images.size())) return cv::Mat();
      if (!sb_images[index].empty() || instant) {
        return sb_images[index];
      }
    }
    std::lock_guard<std::mutex> guard(mutex);
    size_t frame = static_cast<size_t>(index * SB_DURATION * fps);
    video.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(std::min(frame, frames - 1)));
    cv::Mat image;
    video >> image;
    if (!image.empty()) {
      int fwidth = 220;
      int fheight = fwidth * height() / width();
      cv::resize(image, image, cv::Size(fwidth, fheight), 0, 0, cv::INTER_LANCZOS4);
      std::lock_guard<std::mutex> sb_guard(sb_mutex);
      sb_images[index] = image;
    }
    return image;
  }

private:
  std::string path;
  std::mutex mutex, sb_mutex;
  mutable cv::VideoCapture video;
  size_t chunks, frames;
  double fps;
  std::vector<cv::Mat> sb_images;
};

Video* Video::open(json::Value const& config) {
  if (config.has("vod_id")) {
    return new VOD(config["vod_id"].getInteger());
  }
  if (config.has("video_path")) {
    return new VideoFile(config["video_path"].getString());
  }
  throw Exception("unknown video type");
}

Video* Video::open_vod(int vod_id) {
  return new VOD(vod_id);
}

Video* Video::open_video(std::string const& path) {
  return new VideoFile(path);
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
      VodChunk p;
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

bool VOD::load(size_t index, Chunk& chunk, bool existing) {
  if (index > chunks.size()) return false;

  chunk.index = index;
  chunk.start = chunks[index].start;
  chunk.duration = chunks[index].duration;

  url_t piece_url;
  if (!parse_url(chunks[index].path.c_str(), &piece_url, &video_url)) return false;

  std::string path = cache_dir / fmtstring("chunk%06u.ts", index);

  if (!File::exists(path)) {
    if (existing) return false;
    File video_piece = HttpRequest::get(serialize_url(&piece_url));
    if (!video_piece) return false;
    File(path + ".tmp", "wb").copy(video_piece);
    rename_file((path + ".tmp").c_str(), path.c_str());
  }

  cv::VideoCapture cap(path);
  if (!cap.isOpened()) return false;
  cap >> chunk.frame;
  return !chunk.frame.empty();
}

void VOD::delete_cache(size_t index) {
  std::string path = cache_dir / fmtstring("chunk%06u.ts", index);
  delete_file(path.c_str());
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
cv::Mat VOD::storyboard_image(int index, bool instant) {
  int rows = sb_info["rows"].getInteger();
  int cols = sb_info["cols"].getInteger();
  int img_index = (index / (rows * cols));
  if (img_index >= static_cast<int>(sb_images.size())) return cv::Mat();
  if (sb_images[img_index].empty() && !instant) {
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

VideoFile::VideoFile(std::string const& path)
  : path(path)
  , video(path)
{
  if (!video.isOpened()) {
    throw Exception("failed to open video");
  }
  frames = static_cast<size_t>(video.get(cv::CAP_PROP_FRAME_COUNT));
  fps = video.get(cv::CAP_PROP_FPS);
  chunks = static_cast<size_t>(ceil(frames / fps / CHUNK_DURATION));
}
