#include "chunkqueue.h"
#include "path.h"

HeroLineup parse_lineup(std::vector<MatchInfo>& matches, int width) {
  HeroLineup lineup;
  if (matches.empty()) return lineup;

  std::sort(matches.begin(), matches.end(), [](MatchInfo const& lhs, MatchInfo const& rhs) {
    return lhs.point.y < rhs.point.y;
  });
  size_t right = 0, best_count = 0;
  int ysum = 0, avgy = 0;
  for (size_t left = 0; left < matches.size(); ++left) {
    while (right < matches.size() && matches[right].point.y < matches[left].point.y + 12) {
      ysum += matches[right++].point.y;
    }
    if (right - left > best_count) {
      best_count = right - left;
      avgy = ysum / best_count;
    }
    ysum -= matches[left].point.y;
  }
  std::vector<MatchInfo> tmp_matches;
  for (MatchInfo const& m : matches) {
    if (m.point.y < avgy - 8 || m.point.y >= avgy + 8) continue;
    tmp_matches.push_back(m);
  }
  matches.swap(tmp_matches);
  lineup.top = avgy;

  const int max_dist = width / 50;
  const int base_coords[TEAM_SIZE * 2] = {29, 103, 180, 253, 328, 403, 809, 882, 957, 1031, 1107, 1182};

  for (size_t i = 0; i < TEAM_SIZE * 2; ++i) {
    int best = -1;
    int coord = base_coords[i] * width / 1280;
    for (size_t j = 0; j < matches.size(); ++j) {
      if (matches[j].point.x > coord - max_dist && matches[j].point.x < coord + max_dist) {
        if (best < 0 || matches[j].value > matches[best].value) {
          best = j;
        }
      }
    }

    if (best >= 0) {
      if (i < TEAM_SIZE) {
        lineup.blue[i] = matches[best].name;
        ++lineup.count;
      } else {
        lineup.red[i - TEAM_SIZE] = matches[best].name;
        ++lineup.count;
      }
    }
  }

  return lineup;
}

void threshold_image(cv::Mat& image) {
  std::vector<uchar> mv;
  mv.reserve(image.rows * image.cols);
  for (int y = 0; y < image.rows; ++y) {
    auto const* ptr = image.data + y * image.step;
    for (int x = 0; x < image.cols; ++x) {
      mv.push_back(ptr[x]);
    }
  }
  std::sort(mv.begin(), mv.end());
  double minv = mv[0], maxv = mv[mv.size() * 95 / 100];
  cv::threshold(image, image, std::max(maxv - 40.0, minv * 0.2 + maxv * 0.8), 255, cv::THRESH_BINARY);
}

ChunkQueue::ChunkQueue(json::Value const& config)
  : config_(config)
  , vod_(config["vod_id"].getInteger())
  , path_(config["path"].getString())
  , ctx_(cv::Size(vod_.width(), vod_.height() / 5))
  , last_index_(0)
{
  double factor = vod_.width() / 1920.0;
  assemble_ = cv::imread(path::root() / "heroes/assemble.png");
  prepare_ = cv::imread(path::root() / "heroes/prepare.png");
  cv::cvtColor(assemble_, assemble_, cv::COLOR_BGR2GRAY);
  cv::cvtColor(prepare_, prepare_, cv::COLOR_BGR2GRAY);
  cv::resize(assemble_, assemble_, cv::Size(), factor, factor, cv::INTER_LANCZOS4);
  cv::resize(prepare_, prepare_, cv::Size(), factor, factor, cv::INTER_LANCZOS4);

  json::Value hero_list;
  json::parse(File(path::root() / "heroes/list.js"), hero_list, json::mJS, nullptr, true);
  std::vector<Sprite> sprites;

  for (auto const& kv : hero_list.getMap()) {
    cv::Mat icon = cv::imread(path::root() / fmtstring("heroes/%s.png", kv.first.c_str()), -1);
    if (icon.empty()) continue;
    sprites_.emplace_back(kv.first, icon, kv.second.getNumber(), ctx_);
  }

  size_t current = 0;
  if (!json::parse(File(path_ / "status.json"), result_)) {
    result_.clear();
    result_["config"] = config;
    json::write(File(path_ / "status.json", "wb"), result_);
  } else {
    if (result_.has("current")) {
      current = result_["current"].getInteger();
    }
  }

  double start_time = config["start_time"].getNumber();
  double end_time = config["end_time"].getNumber();
  for (size_t index = current; index < vod_.size(); ++index) {
    if (vod_.duration(index + 1) <= start_time) continue;
    if (vod_.duration(index) >= end_time) break;

    push(index);
    last_index_ = index + 1;
  }
  finish();

  consumer_.reset(new std::thread(consume, this));
}

void ChunkQueue::stop() {
  Super::stop();
  if (consumer_) {
    consumer_->join();
    consumer_.reset();
  }
}
void ChunkQueue::join() {
  Super::join();
  if (consumer_) {
    consumer_->join();
    consumer_.reset();
  }
}
void ChunkQueue::start() {
  Super::start(config_["max_threads"].getInteger());
}

void ChunkQueue::process(size_t const& index, ChunkOutput& output) {
  output.index = index;

  VOD::Chunk& chunk = output.chunk;
  if (!vod_.load(index, chunk)) return;
  cv::VideoCapture video(chunk.path);
  if (!video.isOpened()) return;
  video >> output.frame;
  video.release();
  if (output.frame.empty()) return;
  cv::Mat frame = output.frame(cv::Rect(0, 0, output.frame.cols, output.frame.rows / 5));
  MatchFrame mf(frame, ctx_);

  std::vector<MatchInfo> matches;
  for (Sprite const& sprite : sprites_) {
    sprite.match(matches, mf);
  }

  output.lineup = parse_lineup(matches, frame.cols);
  if (output.lineup.count && is_preparation(frame, output.lineup.top)) {
    output.prepare = true;
  }

  output.success = true;
}

bool ChunkQueue::is_preparation(cv::Mat const& frame, int top) {
  int unit = 10 * frame.cols / 1280;
  if (top - unit < 0 || top + 3 * unit > frame.rows) return true;
  double mv1 = 0, mv2 = 0;
  cv::Mat text;
  cv::cvtColor(frame(cv::Rect(55 * unit, top - unit, 15 * unit, 3 * unit)), text, cv::COLOR_BGR2GRAY);
  threshold_image(text);
  cv::Mat m1, m2;
  cv::matchTemplate(text, prepare_, m1, cv::TM_SQDIFF);
  cv::matchTemplate(text, assemble_, m2, cv::TM_SQDIFF);
  cv::minMaxLoc(m1, &mv1);
  cv::minMaxLoc(m2, &mv2);
  double mt1 = prepare_.size().area() * 255 * 255;
  double mt2 = assemble_.size().area() * 255 * 255;
  return (mv1 / mt1 < 0.16 || mv2 / mt2 < 0.12);
}
void ChunkQueue::flush_match(cv::Mat const& screen) {
  if (result_["frames"].length() && (!config_["clean_output"].getBoolean() || result_["frames"].length() >= 16)) {
    cv::imwrite(path_ / format_time(result_["frames"][0]["start"].getNumber(), "%02d-%02d-%02d.png"), screen);

    File picks(path_ / "picks.txt", "at");
    picks.printf("\n");
    for (auto const& frame : result_["frames"]) {
      picks.printf("%s\t%g\t", format_time(frame["start"].getNumber()).c_str(), frame["duration"].getNumber());
      for (auto const& hero : frame["blue"]) {
        picks.printf("\t%s", hero.getString().c_str());
      }
      picks.printf("\t");
      for (auto const& hero : frame["red"]) {
        picks.printf("\t%s", hero.getString().c_str());
      }
      picks.printf("\n");
    }
  }
  result_["frames"].clear();
}

void ChunkQueue::consume(ChunkQueue* queue) {
  ChunkOutput output;
  auto& frames = queue->result_["frames"];
  auto& gap = queue->result_["gap"];
  auto& start = queue->result_["match_start"];
  if (gap.type() != json::Value::tInteger) gap = 0;
  if (frames.type() != json::Value::tArray) frames.setType(json::Value::tArray);
  if (start.type() != json::Value::tNumber) start = 0;
  size_t last_flush = 0;
  double last_time = 0;

  cv::Mat match_frame = cv::imread(queue->path_ / "temp_frame.png");
  while (queue->pop(output)) {
    if (!output.success) {
      if (output.chunk.path.size()) {
        delete_file(output.chunk.path.c_str());
      }
      continue;
    }

    if (output.prepare || output.lineup.count < 5) {
      gap.setInteger(gap.getInteger() + 1);
      if (frames.length() && gap.getInteger() > std::min<int>(4, frames.length() / 4)) {
        queue->flush_match(match_frame);
        match_frame.release();
      }
    } else {
      gap.setInteger(0);
      if (frames.length()) {
        json::Value const& last = frames.getArray().back();
        for (size_t i = 0; i < TEAM_SIZE; ++i) {
          if (output.lineup.blue[i].empty()) output.lineup.blue[i] = last["blue"][i].getString();
          if (output.lineup.red[i].empty()) output.lineup.red[i] = last["red"][i].getString();
        }
      } else {
        match_frame = output.frame;
      }
      json::Value frame;
      frame["start"] = output.chunk.start;
      frame["duration"] = output.chunk.duration;
      for (size_t i = 0; i < TEAM_SIZE; ++i) {
        frame["blue"].append(output.lineup.blue[i]);
        frame["red"].append(output.lineup.red[i]);
      }
      frames.append(frame);
    }

    if (queue->config_["delete_chunks"].getBoolean()) {
      delete_file(output.chunk.path.c_str());
    }
    queue->result_["current"] = output.index + 1;
    if (output.index >= last_flush + 16) {
      last_flush = output.index;
      if (!match_frame.empty()) cv::imwrite(queue->path_ / "temp_frame.png", match_frame);
      json::write(File(queue->path_ / "status.json", "wb"), queue->result_);
    }

    last_time = output.chunk.start + output.chunk.duration;
    queue->report(REPORT_PROGRESS, last_time, output.frame);
  }

  if (queue->result_["current"].getInteger() >= queue->last_index_) {
    if (frames.length()) queue->flush_match(match_frame);
    queue->report(REPORT_FINISHED, queue->config_["end_time"].getNumber(), output.frame);
  } else {
    queue->report(REPORT_STOPPED, last_time, output.frame);
  }
  json::write(File(queue->path_ / "status.json", "wb"), queue->result_);
}
