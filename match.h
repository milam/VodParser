#pragma once

#include <opencv2/opencv.hpp>

class MatchContext {
public:
  MatchContext(cv::Size frameSize)
    : frameSize(frameSize)
    , dftSize(cv::getOptimalDFTSize(frameSize.width), cv::getOptimalDFTSize(frameSize.height))
  {}

  const cv::Size frameSize;
  const cv::Size dftSize;

  void forward(cv::Mat const& src, cv::Mat& dst, cv::Mat& buf) {
    cv::copyMakeBorder(src, buf, 0, dftSize.height - src.rows, 0, dftSize.width - src.cols, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::dft(buf, dst, cv::DFT_COMPLEX_OUTPUT);
  }

  void inverse(cv::Mat const& src, cv::Mat& dst, cv::Size dstSize) {
    cv::dft(src, dst, cv::DFT_INVERSE | cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);
    dst = dst(cv::Rect(dst.size() - dstSize, dstSize));
  }
};

class MatchFrame {
public:
  MatchFrame(cv::Mat const& src, MatchContext& ctx);

  MatchContext& ctx;
  std::vector<cv::Mat> imageSpect;
  std::vector<cv::Mat> sqImageSpect;
  cv::Mat tmp1, tmp2, tmp3, tmp4;
};

struct MatchInfo {
  cv::Point point;
  double value;
  std::string name;
};

class Sprite {
public:
  Sprite(std::string const& name, cv::Mat const& image, double threshold, MatchContext& ctx);

  void match(std::vector<MatchInfo>& matches, MatchFrame& frame) const;

private:
  std::string name;
  double threshold;
  cv::Size corrSize;
  std::vector<cv::Mat> dfts;
  double taNorm;
};
