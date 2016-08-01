#pragma once

#include <string>
#include <vector>
#include "url.h"
#include "path.h"
#include "json.h"
#include <opencv2/opencv.hpp>

std::string format_time(double t, char const* fmt = "%02d:%02d:%02d");

class Video {
public:
  virtual std::string default_output() const = 0;
  virtual std::string title() const = 0;
  virtual void info(json::Value& config) const = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;

  struct Chunk {
    size_t index;
    double start;
    double duration;
    cv::Mat frame;
  };
  virtual bool load(size_t index, Chunk& chunk, bool existing = false) = 0;
  virtual void delete_cache(size_t index) {}

  virtual size_t size() const = 0;
  virtual double duration(size_t pos = -1) const = 0;
  virtual size_t find(double time) const = 0;

  virtual int storyboard_index(double time) = 0;
  virtual cv::Mat storyboard_image(int index, bool instant = false) = 0;

  static Video* open(json::Value const& config);
  static Video* open_vod(int vod_id);
  static Video* open_video(std::string const& path);
};
