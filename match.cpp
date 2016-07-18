#include "match.h"

#ifdef _MSC_VER
#ifdef _DEBUG
#pragma comment(lib, "opencv_world300d.lib")
#else
#pragma comment(lib, "opencv_world300.lib")
#endif
#endif

void MatchContext::load(cv::Mat const& frame) {
  std::vector<cv::Mat> ch;
  cv::split(frame, ch);
  imageSpect.resize(ch.size());
  sqImageSpect.resize(ch.size());
  for (size_t i = 0; i < ch.size(); ++i) {
    ch[i].convertTo(tmp1, CV_32FC1, 1.0 / 255);
    forward(tmp1, imageSpect[i]);
    cv::multiply(tmp1, tmp1, tmp1);
    forward(tmp1, sqImageSpect[i]);
  }
}

Sprite::Sprite(std::string const& name, cv::Mat const& image, double threshold, MatchContext& ctx)
  : name(name)
  , threshold(threshold)
{
  double scale = ctx.frameSize.width / 1920.0;
  cv::resize(image, ctx.tmp2, cv::Size(), scale, scale, cv::INTER_LANCZOS4);
  cv::flip(ctx.tmp2, ctx.tmp2, -1);

  cv::Mat tmp;
  std::vector<cv::Mat> ch;
  cv::split(ctx.tmp2, ch);
  for (auto& img : ch) {
    img.convertTo(img, CV_32FC1, 1.0 / 255);
  }
  corrSize = ctx.frameSize - ctx.tmp2.size() + cv::Size(1, 1);

  taNorm = 0;
  for (size_t i = 0; i < ch.size() - 1; ++i) {
    cv::multiply(ch[i], ch.back(), ctx.tmp1);
    taNorm += cv::norm(ctx.tmp1, cv::NORM_L2SQR);
  }
  taNorm = sqrt(taNorm);

  cv::Mat alpha2;
  cv::multiply(ch.back(), ch.back(), alpha2);
  dfts.resize(ch.size());
  for (size_t i = 0; i < ch.size() - 1; ++i) {
    cv::multiply(ch[i], alpha2, ctx.tmp1);
    ctx.forward(ctx.tmp1, dfts[i]);
  }
  ctx.forward(alpha2, dfts.back());
}

void Sprite::match(std::vector<MatchInfo>& matches, MatchContext& ctx) const {
  for (size_t i = 0; i < ctx.imageSpect.size(); ++i) {
    cv::mulSpectrums(ctx.imageSpect[i], dfts[i], ctx.tmp4, 0);
    ctx.inverse(ctx.tmp4, i == 0 ? ctx.tmp1 : ctx.tmp3, corrSize);
    if (i) cv::add(ctx.tmp1, ctx.tmp3, ctx.tmp1);

    cv::mulSpectrums(ctx.sqImageSpect[i], dfts.back(), ctx.tmp4, 0);
    ctx.inverse(ctx.tmp4, i == 0 ? ctx.tmp2 : ctx.tmp3, corrSize);
    if (i) cv::add(ctx.tmp2, ctx.tmp3, ctx.tmp2);
  }
  cv::sqrt(ctx.tmp2, ctx.tmp2);
  ctx.tmp2 *= taNorm;

  cv::divide(ctx.tmp1, ctx.tmp2, ctx.tmp1);
  cv::threshold(ctx.tmp1, ctx.tmp1, threshold, 1.0, cv::THRESH_TOZERO);

  cv::Rect corrRect(cv::Point(), corrSize);
  for (size_t i = 0; i < 12; ++i) {
    double maxVal;
    cv::Point maxLoc;
    cv::minMaxLoc(ctx.tmp1, nullptr, &maxVal, nullptr, &maxLoc);
    if (maxVal < threshold) break;

    ctx.tmp1(cv::Rect(maxLoc - cv::Point(8, 8), cv::Size(17, 17)) & corrRect) = cv::Scalar::all(0);

    MatchInfo info;
    info.point = maxLoc;
    info.value = maxVal;
    info.name = name;
    matches.push_back(info);
  }
}
