#include <stdio.h>
#include <memory>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <math.h>

#include "path.h"
#include "common.h"
#include "vod.h"
#include "json.h"
#include "match.h"
#include "chunkqueue.h"

class PrintChunkQueue : public ChunkQueue {
public:
  PrintChunkQueue(json::Value const& config)
    : ChunkQueue(config)
  {}

  void report(int status, double time, cv::Mat const& frame) override {
    if (status == REPORT_PROGRESS) {
      printf("\r%s / %s", format_time(time).c_str(), format_time(config_["end_time"].getNumber() - config_["start_time"].getNumber()).c_str());
    } else {
      printf("\rDONE               \n");
    }
  }
};

int do_main(int vod_id) {
  printf("initializing...");
  fclose(stderr);

  VOD vod(vod_id);
  {
    VOD::Chunk chunk;
    vod.load(0, chunk);
    cv::VideoCapture vid;
    vid.open(chunk.path);
  }
  json::Value status, config;
  if (json::parse(File(fmtstring("%d/status.json", vod_id)), status)) {
    config = status["config"];
  } else {
    config["vod_id"] = vod_id;
    config["start_time"] = 0;
    config["end_time"] = vod.duration();
    config["clean_output"] = true;
    config["delete_chunks"] = false;
    config["max_threads"] = 2;
  }

  PrintChunkQueue queue(config);

  queue.start();
  queue.join();

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
