#pragma once

#include <string>
#include <vector>
#include "url.h"

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
  bool load(size_t index, Chunk& chunk) const;

private:
  int vod_id;
  int vod_width, vod_height;
  std::vector<Chunk> chunks;
  std::string cache_dir;
  url_t video_url;
};
