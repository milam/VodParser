#pragma once

#include <memory>
#include "queue.h"
#include "vod.h"
#include "match.h"
#include "json.h"

static const int TEAM_SIZE = 6;

struct HeroLineup {
  int count = 0;
  int top;
  std::string blue[TEAM_SIZE];
  std::string red[TEAM_SIZE];
};

struct ChunkOutput {
  bool success = false;
  size_t index;
  bool prepare = false;
  VOD::Chunk chunk;
  HeroLineup lineup;
  cv::Mat frame;
};

class ChunkQueue : private JobQueue<size_t, ChunkOutput> {
  typedef JobQueue<size_t, ChunkOutput> Super;
public:
  ChunkQueue(json::Value const& config);
  ~ChunkQueue() {
    stop();
  }

  void stop();
  void join();
  void start();

protected:
  json::Value config_;
  VOD vod_;

  enum { REPORT_PROGRESS, REPORT_FINISHED, REPORT_STOPPED };
  virtual void report(int status, double time, cv::Mat const& frame) {}

private:
  void process(size_t const& index, ChunkOutput& output) override;

  bool is_preparation(cv::Mat const& frame, int top);
  void flush_match(cv::Mat const& screen);

  static void consume(ChunkQueue* queue);

  std::string path_;
  cv::Mat prepare_;
  cv::Mat assemble_;
  MatchContext ctx_;
  std::vector<Sprite> sprites_;
  size_t last_index_;

  json::Value result_;
  std::unique_ptr<std::thread> consumer_;
};
