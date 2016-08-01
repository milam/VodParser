#include "match.h"

#ifdef _MSC_VER
#ifdef _WIN64
#ifdef _DEBUG
#pragma comment(lib, "opencv_world310d.lib")
#else
#pragma comment(lib, "opencv_world310.lib") 
#endif
#else
#ifdef _DEBUG
#pragma comment(lib, "opencv_world300d.lib")
#else
#pragma comment(lib, "opencv_world300.lib") 
#endif
#endif
#endif

MatchFrame::MatchFrame(cv::Mat const& frame, MatchContext& ctx)
  : ctx(ctx)
{
  std::vector<cv::Mat> ch;
  cv::split(frame, ch);
  imageSpect.resize(ch.size());
  sqImageSpect.resize(ch.size());
  for (size_t i = 0; i < ch.size(); ++i) {
    ch[i].convertTo(tmp1, CV_32FC1, 1.0 / 255);
    ctx.forward(tmp1, imageSpect[i], tmp2);
    cv::multiply(tmp1, tmp1, tmp1);
    ctx.forward(tmp1, sqImageSpect[i], tmp2);
  }
}

Sprite::Sprite(std::string const& name, cv::Mat const& image, double threshold, MatchContext& ctx)
  : name(name)
  , threshold(threshold)
{
  cv::Mat img;
  double scale = ctx.frameSize.width / 1920.0;
  cv::resize(image, img, cv::Size(), scale, scale, cv::INTER_LANCZOS4);
  cv::flip(img, img, -1);

  std::vector<cv::Mat> ch;
  cv::split(img, ch);
  for (auto& im : ch) {
    im.convertTo(im, CV_32FC1, 1.0 / 255);
  }
  corrSize = ctx.frameSize - img.size() + cv::Size(1, 1);

  taNorm = 0;
  cv::Mat tmp, buf;
  for (size_t i = 0; i < ch.size() - 1; ++i) {
    cv::multiply(ch[i], ch.back(), tmp);
    taNorm += cv::norm(tmp, cv::NORM_L2SQR);
  }
  taNorm = sqrt(taNorm);

  cv::Mat alpha2;
  cv::multiply(ch.back(), ch.back(), alpha2);
  dfts.resize(ch.size());
  for (size_t i = 0; i < ch.size() - 1; ++i) {
    cv::multiply(ch[i], alpha2, tmp);
    ctx.forward(tmp, dfts[i], buf);
  }
  ctx.forward(alpha2, dfts.back(), buf);
}

void Sprite::match(std::vector<MatchInfo>& matches, MatchFrame& frame) const {
  for (size_t i = 0; i < frame.imageSpect.size(); ++i) {
    cv::mulSpectrums(frame.imageSpect[i], dfts[i], frame.tmp4, 0);
    frame.ctx.inverse(frame.tmp4, i == 0 ? frame.tmp1 : frame.tmp3, corrSize);
    if (i) cv::add(frame.tmp1, frame.tmp3, frame.tmp1);

    cv::mulSpectrums(frame.sqImageSpect[i], dfts.back(), frame.tmp4, 0);
    frame.ctx.inverse(frame.tmp4, i == 0 ? frame.tmp2 : frame.tmp3, corrSize);
    if (i) cv::add(frame.tmp2, frame.tmp3, frame.tmp2);
  }
  cv::sqrt(frame.tmp2, frame.tmp2);
  frame.tmp2 *= taNorm;

  cv::divide(frame.tmp1, frame.tmp2, frame.tmp1);
  cv::threshold(frame.tmp1, frame.tmp1, threshold, 1.0, cv::THRESH_TOZERO);

  cv::Rect corrRect(cv::Point(), corrSize);
  for (size_t i = 0; i < 12; ++i) {
    double maxVal;
    cv::Point maxLoc;
    cv::minMaxLoc(frame.tmp1, nullptr, &maxVal, nullptr, &maxLoc);
    if (maxVal < threshold) break;

    frame.tmp1(cv::Rect(maxLoc - cv::Point(8, 8), cv::Size(17, 17)) & corrRect) = cv::Scalar::all(0);

    MatchInfo info;
    info.point = maxLoc;
    info.value = maxVal;
    info.name = name;
    matches.push_back(info);
  }
}
