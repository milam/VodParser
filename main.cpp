#include <stdio.h>
#include <memory>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <math.h>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <algorithm>
using namespace std;

#include "path.h"
#include "common.h"
#include "vod.h"
#include "json.h"
#include "match.h"

template<class Data>
class JobQueue {
public:
  JobQueue(size_t max_size)
    : max_size(max_size)
    , done(false)
    , n_pushed(0)
    , n_popped(0)
  {}

  size_t pushed() const {
    return n_pushed;
  }
  size_t popped() const {
    return n_popped;
  }

  bool wait_progress(size_t pushed, size_t popped) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this, pushed, popped]{
      return n_pushed > pushed || n_popped > popped || done;
    });
    return !done || queue.size();
  }

  void push(Data const& data) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this]{
      return queue.size() < max_size;
    });

    queue.push_back(data);
    ++n_pushed;
    cv.notify_all();
  }
  void push_nil() {
    std::lock_guard<std::mutex> guard(mutex);
    ++n_pushed;
    ++n_popped;
    cv.notify_all();
  }
  void finish() {
    std::lock_guard<std::mutex> guard(mutex);
    done = true;
    cv.notify_all();
  }

  bool pop(Data& data) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this]{
      return queue.size() || done;
    });

    if (queue.size()) {
      data = queue.front();
      queue.pop_front();
      ++n_popped;
      cv.notify_all();
      return true;
    } else {
      return false;
    }
  }

private:
  std::deque<Data> queue;
  std::mutex mutex;
  std::condition_variable_any cv;
  size_t max_size;
  bool done;
  size_t n_pushed, n_popped;
};

std::string format_time(double t, char const* fmt = "%02d:%02d:%02d") {
  double m = floor(t / 60);
  t -= m * 60;
  double h = floor(m / 60);
  m -= h * 60;
  return fmtstring(fmt, static_cast<int>(h), static_cast<int>(m), static_cast<int>(t));
}

void worker_downloader(JobQueue<VOD::Chunk>& queue, VOD const& vod) {
  VOD::Chunk chunk;
  size_t index = 0;
  json::Value status;
  if (json::parse(File(fmtstring("%d/status.json", vod.id())), status)) {
    index = status["chunks"].getInteger();
  }

  for (size_t i = 0; i < vod.size(); ++i) {
    if (i < index) {
      queue.push_nil();
    } else if (vod.load(i, chunk)) {
      queue.push(chunk);
    } else {
      queue.push_nil();
    }
  }
  queue.finish();
}

static const int TEAM_SIZE = 6;

struct HeroLineup {
  int count = 0;
  int top;
  std::string blue[TEAM_SIZE];
  std::string red[TEAM_SIZE];
};
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

void worker_parser(JobQueue<VOD::Chunk>& queue, VOD const& vod) {
  VOD::Chunk chunk;

  MatchContext ctx(cv::Size(vod.width(), vod.height() / 5));

  json::Value hero_list;
  json::parse(File("heroes/list.js"), hero_list, json::mJS, nullptr, true);
  std::vector<Sprite> sprites;

  for (auto const& kv : hero_list.getMap()) {
    cv::Mat icon = cv::imread(fmtstring("heroes/%s.png", kv.first.c_str()), -1);
    if (icon.empty()) continue;
    sprites.emplace_back(kv.first, icon, kv.second.getNumber(), ctx);
  }

  double factor = vod.width() / 1920.0;
  cv::Mat sp_assemble = cv::imread("heroes/assemble.png");
  cv::Mat sp_prepare = cv::imread("heroes/prepare.png");
  cv::cvtColor(sp_assemble, sp_assemble, cv::COLOR_BGR2GRAY);
  cv::cvtColor(sp_prepare, sp_prepare, cv::COLOR_BGR2GRAY);
  cv::resize(sp_assemble, sp_assemble, cv::Size(), factor, factor, cv::INTER_LANCZOS4);
  cv::resize(sp_prepare, sp_prepare, cv::Size(), factor, factor, cv::INTER_LANCZOS4);

  cv::Mat frame, full_frame;
  HeroLineup prev_lineup;
  double empty_time = 60;
  cv::VideoCapture video;
  int in_game = 0;

  json::Value status;
  if (json::parse(File(fmtstring("%d/status.json", vod.id())), status)) {
    auto& arr = status["lineup"];
    for (size_t i = 0; i < TEAM_SIZE; ++i) {
      prev_lineup.blue[i] = arr[i].getString();
      prev_lineup.red[i] = arr[i + TEAM_SIZE].getString();
    }
    in_game = status["ingame"].getInteger();
    empty_time = status["empty"].getNumber();
  }

  while (queue.pop(chunk)) {
    if (!video.open(chunk.path)) continue;
    video >> full_frame;
    video.release();
    if (full_frame.empty()) continue;
    frame = full_frame(cv::Rect(0, 0, full_frame.cols, full_frame.rows / 5));
    ctx.load(frame);

    std::vector<MatchInfo> matches;
    for (Sprite const& sprite : sprites) {
      sprite.match(matches, ctx);
    }
    //auto tm = format_time(chunk.start, "%02d-%02d-%02d");
    HeroLineup lineup = parse_lineup(matches, frame.cols);
    if (lineup.count) {
      int unit = 10 * frame.cols / 1280;
      double mv1 = 0, mv2 = 0;
      if (lineup.top - unit >= 0 && lineup.top + 3 * unit <= frame.rows) {
        cv::Mat text;
        cv::cvtColor(frame(cv::Rect(55 * unit, lineup.top - unit, 15 * unit, 3 * unit)), text, cv::COLOR_BGR2GRAY);
        threshold_image(text);
        cv::Mat m1, m2;
        cv::matchTemplate(text, sp_prepare, m1, cv::TM_SQDIFF);
        cv::matchTemplate(text, sp_assemble, m2, cv::TM_SQDIFF);
        cv::minMaxLoc(m1, &mv1);
        cv::minMaxLoc(m2, &mv2);
      }
      double mt1 = sp_prepare.size().area() * 255 * 255;
      double mt2 = sp_assemble.size().area() * 255 * 255;
      if (mv1 / mt1 < 0.16 || mv2 / mt2 < 0.12) {
        in_game -= 1;
        if (in_game < 0) in_game = 0;
      } else {
        in_game += 1;
        if (in_game > 3) in_game = 3;
      }
    }
    if (in_game < 2 || !lineup.count) {
      empty_time += chunk.duration;
    } else {
      File log(fmtstring("%d/picks.txt", vod.id()), "at");
      if (empty_time > 30.0) {
        cv::imwrite(fmtstring("%d/%s.png", vod.id(), format_time(chunk.start, "%02d-%02d-%02d").c_str()), full_frame);
        log.printf("\n");
      } else {
        for (size_t i = 0; i < TEAM_SIZE; ++i) {
          if (lineup.blue[i].empty()) lineup.blue[i] = prev_lineup.blue[i];
          if (lineup.red[i].empty()) lineup.red[i] = prev_lineup.red[i];
        }
      }
      empty_time = 0;
      log.printf("%s\t%g\t", format_time(chunk.start).c_str(), chunk.duration);
      for (size_t i = 0; i < TEAM_SIZE; ++i) {
        log.printf("\t%s", lineup.blue[i].c_str());
      }
      log.printf("\t");
      for (size_t i = 0; i < TEAM_SIZE; ++i) {
        log.printf("\t%s", lineup.red[i].c_str());
      }
      log.printf("\n");

      prev_lineup = lineup;
    }

    json::Value status;
    status["chunks"] = chunk.index + 1;
    auto& arr = status["lineup"];
    for (size_t i = 0; i < TEAM_SIZE; ++i) {
      arr.append(prev_lineup.blue[i]);
    }
    for (size_t i = 0; i < TEAM_SIZE; ++i) {
      arr.append(prev_lineup.red[i]);
    }
    status["ingame"] = in_game;
    status["empty"] = empty_time;
    json::write(File(fmtstring("%d/status.json", vod.id()), "wb"), status);
  }
}

void worker_progress(JobQueue<VOD::Chunk>& queue, VOD const& vod) {
  size_t pushed = 0, popped = 0;
  do {
    pushed = queue.pushed();
    popped = queue.popped();
    printf("\r[done] %-8s [loaded] %-8s [total] %-8s",
      format_time(vod.duration(popped)).c_str(),
      format_time(vod.duration(pushed)).c_str(),
      format_time(vod.duration()).c_str());
  } while (queue.wait_progress(pushed, popped));
  printf("\rDONE                                              \n");
}

int do_main(int vod_id) {
  printf("initializing...");
  fclose(stderr);

  VOD vod(vod_id);
  {
    VOD::Chunk chunk;
    //vod.load(vod.find(23 * 60 + 30), chunk);
    vod.load(0, chunk);
    cv::VideoCapture vid;
    vid.open(chunk.path);
    //cv::Mat img;
    //vid >> img;
    //cv::imwrite("frame.png", img);
  }
  JobQueue<VOD::Chunk> queue(8);

  std::vector<std::thread> threads;
  threads.emplace_back(worker_downloader, std::ref(queue), std::ref(vod));
  threads.emplace_back(worker_parser, std::ref(queue), std::ref(vod));
  threads.emplace_back(worker_progress, std::ref(queue), std::ref(vod));\
  for (std::thread& thread : threads) {
    thread.join();
  }


  return 0;
}

int main(int argc, char const** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: vodscanner <vod-id>\n");
    return 1;
  }
  try {
    return do_main(std::atoi(argv[1]));
  } catch (cv::Exception& e) {
    std::cout << e.what() << std::endl;
  } catch (Exception& e) {
    std::cout << e.what() << std::endl;
  }
  return 0;
}
