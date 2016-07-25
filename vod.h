#pragma once

#include <string>
#include <vector>
#include "url.h"
#include "json.h"
#include <opencv2/opencv.hpp>

std::string format_time(double t, char const* fmt = "%02d:%02d:%02d");

class VOD {
public:
  VOD(int id);

  int id() const {
    return vod_id;
  }
  int width() const {
    return vod_width;
  }
  int height() const {
    return vod_height;
  }

  struct Chunk {
    size_t index;
    double start;
    double duration;
    std::string path;
  };

  size_t size() const {
    return chunks.size();
  }
  double duration(size_t pos = -1) const;
  size_t find(double time) const;
  bool load(size_t index, Chunk& chunk, bool existing = false) const;

  int storyboard_index(double time);
  cv::Mat storyboard_image(int index);

  json::Value const& info() const {
    return vod_info;
  }

private:
  json::Value vod_info;
  int vod_id;
  int vod_width, vod_height;
  std::vector<Chunk> chunks;
  std::string cache_dir;
  url_t video_url;

  url_t sb_url;
  std::vector<cv::Mat> sb_images;
  json::Value sb_info;
};
