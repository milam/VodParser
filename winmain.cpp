#include <windows.h>
#include <commctrl.h>
#include <memory>
#include <functional>
#include "common.h"
#include "chunkqueue.h"
#include "frameui/fontsys.h"
#include "vod.h"
#include "path.h"
#include "frameui/framewnd.h"
#include "frameui/controlframes.h"
#include "resource.h"
#include "http.h"
#include <time.h>

#include <shlobj.h>
bool browseForFolder(wchar_t const* prompt, std::string& result) {
  IFileDialog* pfd;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
    return false;
  }
  DWORD dwOptions;
  if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
    pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
  }
  pfd->SetTitle(prompt);
  if (FAILED(pfd->Show(NULL))) {
    pfd->Release();
    return false;
  }
  IShellItem* psi;
  if (FAILED(pfd->GetResult(&psi))) {
    pfd->Release();
    return false;
  }
  wchar_t* str;
  if (FAILED(psi->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &str)) || !str) {
    psi->Release();
    pfd->Release();
    return false;
  }
  std::wstring ws(str);
  result = std::string(ws.begin(), ws.end());
  psi->Release();
  pfd->Release();
  return true;
}

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

class DialogWindow : public RootWindow {
protected:
  void create(int width, int height, std::string const& text, int style, int exStyle, Window* parent = nullptr) {
    if (WNDCLASSEX* wcx = createclass("MainDlgClass")) {
      wcx->hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
      wcx->hCursor = LoadCursor(NULL, IDC_ARROW);
      wcx->hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_OVERWATCH));
      RegisterClassEx(wcx);
    }
    RECT rc = {0, 0, width, height};
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    RootWindow::create((sw - (rc.right - rc.left)) / 2, (sh - (rc.bottom - rc.top)) / 2, rc.right - rc.left, rc.bottom - rc.top,
      text, style, exStyle, parent ? parent->getHandle() : NULL);
  }
  void move(int width, int height) {
    RECT wrc;
    GetWindowRect(hWnd, &wrc);
    RECT rc = {0, 0, width, height};
    AdjustWindowRectEx(&rc, GetWindowLongPtr(hWnd, GWL_STYLE), FALSE, GetWindowLongPtr(hWnd, GWL_EXSTYLE));
    SetWindowPos(hWnd, NULL,
      (wrc.left + wrc.right + rc.left - rc.right) / 2,
      (wrc.top + wrc.bottom + rc.top - rc.bottom) / 2,
      rc.right - rc.left, rc.bottom - rc.top,
      SWP_NOZORDER | SWP_NOACTIVATE);
  }
};

#define ID_BUTTON_0     131
class LaunchDialog : public DialogWindow {
public:
  LaunchDialog(std::vector<std::string> const& options, std::function<bool(DialogWindow*, int)>&& callback)
    : callback_(std::move(callback))
  {
    create(200, 31 * options.size() + 10, "VOD Scanner", WS_OVERLAPPED | WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT);

    for (size_t i = 0; i < options.size(); ++i) {
      buttons_.push_back(new ButtonFrame(options[i], this, ID_BUTTON_0 + i));
      buttons_[i]->setPoint(PT_RIGHT, -10, 0);
      buttons_[i]->setHeight(21);
      if (i) {
        buttons_[i]->setPoint(PT_TOPLEFT, buttons_[i - 1], PT_BOTTOMLEFT, 0, 10);
      } else {
        buttons_[i]->setPoint(PT_TOPLEFT, 10, 10);
      }
    }
  }

private:
  std::vector<ButtonFrame*> buttons_;
  std::function<bool(DialogWindow*, int)> callback_;

  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
    if (message == WM_COMMAND) {
      int id = LOWORD(wParam);
      if (id >= ID_BUTTON_0 && id < ID_BUTTON_0 + buttons_.size()) {
        if (callback_(this, id - ID_BUTTON_0)) {
          endModal();
        }
      }
      return 0;
    }
    return M_UNHANDLED;
  }
};

#define ID_BUTTON_LOAD      131
#define ID_BUTTON_BROWSE    130
#define ID_RANGE_SLIDER     132
#define ID_TIME_START       133
#define ID_TIME_END         134
#define ID_BROWSE_PATH      135
#define ID_START_DOWNLOAD   136

HBITMAP CVBitmap(cv::Mat const& frame, HDC hDC = NULL) {
  if (!frame.rows || !frame.cols) return NULL;

  bool nullDC = (!hDC);
  if (nullDC) hDC = GetDC(NULL);

  HBITMAP hBitmap = CreateCompatibleBitmap(hDC, frame.cols, frame.rows);

  BITMAPINFOHEADER hdr;
  memset(&hdr, 0, sizeof hdr);
  hdr.biSize = sizeof hdr;
  hdr.biWidth = frame.cols;
  hdr.biHeight = -frame.rows;
  hdr.biPlanes = 1;
  hdr.biCompression = BI_RGB;
  switch (frame.depth()) {
  case CV_8U:
  case CV_8S:
    hdr.biBitCount = 8;
    break;
  case CV_16U:
  case CV_16S:
    hdr.biBitCount = 16;
    break;
  case CV_32S:
  case CV_32F:
    hdr.biBitCount = 32;
    break;
  case CV_64F:
    hdr.biBitCount = 64;
    break;
  }
  hdr.biBitCount *= frame.channels();

  cv::Mat src;
  if ((hdr.biBitCount / 8) * frame.cols != frame.step) {
    frame.copyTo(src);
  } else {
    src = frame;
  }
  SetDIBits(hDC, hBitmap, 0, frame.rows, src.data, (BITMAPINFO*) &hdr, DIB_RGB_COLORS);
  if (nullDC) ReleaseDC(NULL, hDC);

  return hBitmap;
}

void addTip(Frame* frame, std::string const& name) {
  StaticFrame* tip = new StaticFrame(name, frame->getParent());
  tip->setPoint(PT_RIGHT, frame, PT_LEFT, -8, 0);
}

#define ID_BUTTON_LIST        139
#define ID_BUTTON_PREV        140
#define ID_BUTTON_NEXT        141
#define ID_BUTTON_BROADCASTS  142
#define ID_BUTTON_HIGHLIGHTS  143
#define ID_EDIT_CHANNEL       144
#define ID_VIDEO_LIST         145

class VodBrowser : public DialogWindow {
public:
  VodBrowser(std::function<bool(int)>&& callback, Window* parent = nullptr)
    : callback(std::move(callback))
    , hBitmap(NULL)
  {
    create(650, 350, "VOD Browser", WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT, parent);

    btn_list = new ButtonFrame("List", this, ID_BUTTON_LIST);
    btn_list->setPointEx(PT_TOPRIGHT, 0.64, 0, 0, 10);
    btn_list->setSize(60, 21);
    channel = new EditFrame(this, ID_EDIT_CHANNEL);
    channel->setPoint(PT_TOPLEFT, 10, 10);
    channel->setPoint(PT_BOTTOMRIGHT, btn_list, PT_BOTTOMLEFT, -6, 0);
    Edit_SetCueBannerText(channel->getHandle(), L"Enter channel name");
    btn_prev = new ButtonFrame("<", this, ID_BUTTON_PREV);
    btn_prev->setPoint(PT_BOTTOMLEFT, 10, -10);
    btn_prev->setSize(40, 21);
    list = new ListFrame(this, ID_VIDEO_LIST, LVS_ALIGNLEFT | LVS_REPORT |
      LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
    ListView_SetExtendedListViewStyle(list->getHandle(), LVS_EX_FULLROWSELECT);
    list->setPoint(PT_TOPLEFT, channel, PT_BOTTOMLEFT, 0, 8);
    list->setPoint(PT_TOPRIGHT, btn_list, PT_BOTTOMRIGHT, 0, 8);
    list->setPoint(PT_BOTTOMLEFT, btn_prev, PT_TOPLEFT, 0, -8);
    btn_next = new ButtonFrame(">", this, ID_BUTTON_NEXT);
    btn_next->setPoint(PT_TOPRIGHT, list, PT_BOTTOMRIGHT, 0, 8);
    btn_next->setSize(40, 21);
    btn_broadcasts = new ButtonFrame("Broadcasts", this, ID_BUTTON_BROADCASTS, BS_CHECKBOX | BS_PUSHLIKE);
    btn_broadcasts->setPointEx(PT_BOTTOMRIGHT, 0.32, 1, 2, -10);
    btn_broadcasts->setSize(80, 21);
    btn_highlights = new ButtonFrame("Highlights", this, ID_BUTTON_HIGHLIGHTS, BS_CHECKBOX | BS_PUSHLIKE);
    btn_highlights->setPointEx(PT_BOTTOMLEFT, 0.32, 1, 8, -10);
    btn_highlights->setSize(80, 21);
    btn_load = new ButtonFrame("Load", this, ID_BUTTON_LOAD);
    btn_load->setPoint(PT_TOPLEFT, btn_next, PT_TOPRIGHT, 10, 0);
    btn_load->setPoint(PT_BOTTOMRIGHT, -10, -10);

    list->setColumns(3);
    list->setColumn(0, 40, LVCFMT_LEFT);
    list->setColumn(1, LVSCW_AUTOSIZE, LVCFMT_LEFT);
    list->setColumn(2, 80, LVCFMT_LEFT);

    sel_image = new StaticFrame(this, 0, SS_BITMAP | WS_BORDER);
    sel_image->setPoint(PT_TOPLEFT, btn_list, PT_TOPRIGHT, 10, 0);
    sel_image->setPoint(PT_TOPRIGHT, -10, 10);
    sel_image->setHeight(100);
    sel_info = new StaticFrame(this);
    sel_info->setPoint(PT_TOPLEFT, sel_image, PT_BOTTOMLEFT, 0, 8);
    sel_info->setPoint(PT_BOTTOMRIGHT, btn_load, PT_TOPRIGHT, 0, -8);

    btn_prev->hide();
    btn_next->hide();
    sel_image->hide();

    if (vod_data.has("channel")) {
      channel->setText(vod_data["channel"].getString());
    }
    load_data(vod_data);
  }
  ~VodBrowser() {
    if (hBitmap) DeleteObject(hBitmap);
  }
private:
  std::function<bool(int)> callback;

  EditFrame* channel;
  ListFrame* list;
  ButtonFrame* btn_list;
  ButtonFrame* btn_prev;
  ButtonFrame* btn_next;
  ButtonFrame* btn_broadcasts;
  ButtonFrame* btn_highlights;
  ButtonFrame* btn_load;
  StaticFrame* sel_image;
  StaticFrame* sel_info;

  static json::Value vod_data;
  json::Value sel_data;
  cv::Mat sel_mat;
  HBITMAP hBitmap;

  static time_t parse_time(char const* time) {
    struct tm data;
    memset(&data, 0, sizeof data);
    if (sscanf(time, "%d-%d-%dT%d:%d:%dZ", &data.tm_year, &data.tm_mon, &data.tm_mday, &data.tm_hour, &data.tm_min, &data.tm_sec) != 6) {
      return 0;
    }
    data.tm_year -= 1900;
    data.tm_mon -= 1;
    return mktime(&data);
  }
  static std::string format_time(time_t time, char const* fmt) {
    struct tm data;
    char buf[128];
    localtime_s(&data, &time);
    strftime(buf, sizeof buf, fmt, &data);
    return buf;
  }

  void load_data(json::Value const& data) {
    vod_data = data;
    btn_prev->show(data["_links"].has("prev"));
    btn_next->show(data["_links"].has("next"));
    if (data["_links"].has("self")) {
      if (data["_links"]["self"].getString().find("broadcasts=true") != std::string::npos) {
        btn_broadcasts->setCheck(true);
        btn_highlights->setCheck(false);
      } else {
        btn_broadcasts->setCheck(false);
        btn_highlights->setCheck(true);
      }
      btn_broadcasts->enable(true);
      btn_highlights->enable(true);
    } else {
      btn_broadcasts->enable(false);
      btn_highlights->enable(false);
    }

    list->clear();
    for (size_t i = 0; i < data["videos"].length(); ++i) {
      json::Value const& info = data["videos"][i];
      std::string id = info["_id"].getString();
      if (id[0] == 'v') id = id.substr(1);
      int index = list->addItem(id);
      list->setItemTextUtf8(index, 1, info["title"].getString());
      time_t ts = parse_time(info["recorded_at"].getString().c_str());
      list->setItemText(index, 2, format_time(ts, "%x"));
    }

    set_selection(json::Value());
  }

  void load_data(std::string const& url) {
    json::Value data;
    if (!parse(HttpRequest::get(url), data)) {
      MessageBox(hWnd, "Failed to load resource", "Error", MB_OK | MB_ICONHAND);
      return;
    }
    data["channel"] = channel->getText();
    load_data(data);
  }

  void set_image() {
    if (sel_mat.empty()) {
      sel_image->hide();
      sel_info->setPoint(PT_TOP, 0, 10);
    } else {
      sel_image->show();
      if (hBitmap) DeleteObject(hBitmap);
      int width = (sel_image->width() - 2) & (~3);
      int height = width * sel_mat.rows / sel_mat.cols;
      if (height != sel_image->height() - 2) {
        sel_image->setHeight(height + 2);
      }
      cv::Mat tmp;
      cv::resize(sel_mat, tmp, cv::Size(width, height), 0, 0, cv::INTER_LANCZOS4);
      hBitmap = CVBitmap(tmp);
      sel_image->setImage(hBitmap);
      sel_info->setPoint(PT_TOP, sel_image, PT_BOTTOM, 0, 8);
    }
  }
  void set_selection(json::Value const& data) {
    sel_data = data;
    if (data.has("_id")) {
      sel_info->show();
      File preview = HttpRequest::get(data["preview"].getString());
      if (preview) {
        std::vector<uint8> data(preview.size());
        preview.read(data.data(), data.size());
        sel_mat = cv::imdecode(data, 1);
      } else {
        sel_mat.release();
      }
      set_image();
      btn_load->enable();

      time_t ts = parse_time(data["recorded_at"].getString().c_str());
      std::string text = "Title: " + data["title"].getString();
      if (data["description"].type() == json::Value::tString) {
        text += "\nDescription: " + data["description"].getString();
      }
      text += "\nRecorded: " + format_time(ts, "%c");
      text += "\nDuration: " + ::format_time(data["length"].getNumber());
      sel_info->setText(text);
    } else {
      sel_image->hide();
      sel_info->hide();
      btn_load->disable();
    }
  }

  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
    if (message == WM_SIZE && list) {
      RECT rc;
      GetClientRect(list->getHandle(), &rc);
      list->setColumnWidth(0, 60);
      list->setColumnWidth(1, rc.right - 145);
      list->setColumnWidth(2, 80);

      set_image();
      sel_info->setText(sel_info->getText());
    } else if (message == WM_COMMAND) {
      int id = LOWORD(wParam);
      if (id == IDOK) {
        if (GetFocus() == channel->getHandle()) {
          id = ID_BUTTON_LIST;
        } else if (GetFocus() == list->getHandle()) {
          id = ID_BUTTON_LOAD;
        }
      }
      if (id == ID_BUTTON_LIST) {
        load_data(fmtstring("https://api.twitch.tv/kraken/channels/%s/videos?broadcasts=true", channel->getText().c_str()));
      } else if (id == ID_BUTTON_BROADCASTS) {
        load_data(fmtstring("https://api.twitch.tv/kraken/channels/%s/videos?broadcasts=true", channel->getText().c_str()));
      } else if (id == ID_BUTTON_HIGHLIGHTS) {
        load_data(fmtstring("https://api.twitch.tv/kraken/channels/%s/videos", channel->getText().c_str()));
      } else if (id == ID_BUTTON_PREV) {
        if (vod_data["_links"].has("prev")) {
          load_data(vod_data["_links"]["prev"].getString());
        }
      } else if (id == ID_BUTTON_NEXT) {
        if (vod_data["_links"].has("next")) {
          load_data(vod_data["_links"]["next"].getString());
        }
      } else if (id == ID_BUTTON_LOAD) {
        if (sel_data.has("_id")) {
          std::string id = sel_data["_id"].getString();
          if (id[0] == 'v') id = id.substr(1);
          int vod_id = std::stoi(id);
          if (vod_id && callback(vod_id)) {
            endModal();
          }
        }
      }
      return 0;
    } else if (message == WM_NOTIFY) {
      NMHDR* nmh = reinterpret_cast<NMHDR*>(lParam);
      if (nmh->hwndFrom == list->getHandle() && nmh->code == LVN_ITEMCHANGED) {
        NMLISTVIEW* nmv = reinterpret_cast<NMLISTVIEW*>(nmh);
        if (nmv->uNewState & LVIS_SELECTED) {
          set_selection(vod_data["videos"][nmv->iItem]);
        }
        return 0;
      }
    //} else if (message == WM_CLOSE) {
    //  endModal();
    //  return 0;
    } else if (message == WM_GETMINMAXINFO) {
      MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lParam);
      mm->ptMinTrackSize.x = 450;
      mm->ptMinTrackSize.y = 200;
      return 0;
    }
    return M_UNHANDLED;
  }
};

json::Value VodBrowser::vod_data;

class NewDownload : public DialogWindow {
public:
  NewDownload(Video* vod, std::function<void(DialogWindow*, json::Value const&)>&& callback, int width = 600, Window* parent = nullptr)
    : callback(std::move(callback))
    , vod(vod)
    , def_width(width)
  {
    if (vod) {
      create(400, 41, "VOD Scanner", WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT, parent);

      vod_id_input = new EditFrame(this, 0, ES_READONLY | ES_AUTOHSCROLL);
      vod_id_input->setPoint(PT_TOPLEFT, 10, 10);
      vod_id_input->setPoint(PT_RIGHT, -10, 0);
      vod_id_input->setHeight(21);

      initialize();
    } else {
      create(400, 41, "VOD Scanner", WS_OVERLAPPED | WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT, parent);

      vod_id_input = new EditFrame(this, 0, ES_NUMBER | ES_AUTOHSCROLL);
      vod_id_input->setPoint(PT_TOPLEFT, 10, 10);
      vod_id_input->setHeight(21);
      load_button = new ButtonFrame("Load", this, ID_BUTTON_LOAD);
      load_button->setSize(80, 21);
      load_button->setPoint(PT_TOPRIGHT, -10, 10);
      browse_button = new ButtonFrame("Browse", this, ID_BUTTON_BROWSE);
      browse_button->setSize(80, 21);
      browse_button->setPoint(PT_TOPRIGHT, load_button, PT_TOPLEFT, -6, 0);
      vod_id_input->setPoint(PT_RIGHT, browse_button, PT_LEFT, -6, 0);
      Edit_SetCueBannerText(vod_id_input->getHandle(), L"Enter VOD ID");
    }
  }

  NewDownload(json::Value const& config, std::function<void(DialogWindow*, json::Value const&)>&& callback, int width = 600, Window* parent = nullptr)
    : callback(std::move(callback))
    , def_width(width)
    , vod(Video::open(config))
  {
    create(400, 41, "VOD Scanner", WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT, parent);

    vod_id_input = new EditFrame(this, 0, ES_READONLY | ES_AUTOHSCROLL);
    vod_id_input->setPoint(PT_TOPLEFT, 10, 10);
    vod_id_input->setPoint(PT_RIGHT, -10, 0);
    vod_id_input->setHeight(21);

    initialize();

    int start = static_cast<int>(config["start_time"].getNumber());
    int end = static_cast<int>(config["end_time"].getNumber());
    start_time->setDate(start);
    end_time->setDate(end);
    range_slider->setRange(start, end);

    opt_delete_chunks->setCheck(config["delete_chunks"].getBoolean());
    opt_clean_output->setCheck(config["clean_output"].getBoolean());
    output_path->setText(config["path"].getString());
    max_threads->setCurSel(config["max_threads"].getInteger() - 1);
  }

  ~NewDownload() {
    if (hImage) DeleteObject(hImage);
    if (frame_worker) frame_worker->join();
  }

private:
  std::function<void(DialogWindow*, json::Value const&)> callback;
  EditFrame* vod_id_input;
  ButtonFrame* load_button = nullptr;
  ButtonFrame* browse_button = nullptr;
  RangeSliderFrame* range_slider = nullptr;
  StaticFrame* image_frame = nullptr;
  DateTimeFrame* start_time = nullptr;
  DateTimeFrame* end_time = nullptr;
  ButtonFrame* opt_delete_chunks = nullptr;
  ButtonFrame* opt_clean_output = nullptr;
  EditFrame* output_path = nullptr;
  ComboFrame* max_threads = nullptr;
  std::unique_ptr<Video> vod;
  int display_side = RangeSliderFrame::DRAG_LEFT | RangeSliderFrame::DRAG_END;
  int hover_index = -1;
  int def_width;

  enum { WM_FRAMELOADED = (WM_USER + 57) };

  std::mutex mutex;
  std::unique_ptr<std::thread> frame_worker;
  bool frame_loading = false;
  enum { FM_FULL, FM_CACHED, FM_STORYBOARD };
  int frame_mode;
  size_t frame_index;
  cv::Mat frame_image;
  cv::Mat display_image;

  HBITMAP hImage = nullptr;

  enum { VALIDATE_APPROX, VALIDATE_DIAG, VALIDATE_H, VALIDATE_V };
  enum { OFFSET_W = 22, OFFSET_H = 161 };
  void validate_size(int& width, int& height, int mode = VALIDATE_APPROX) {
    RECT rc = {0, 0, 0, 0};
    AdjustWindowRectEx(&rc, GetWindowLongPtr(hWnd, GWL_STYLE), FALSE, GetWindowLongPtr(hWnd, GWL_EXSTYLE));

    width -= (rc.right - rc.left) + OFFSET_W;
    height -= (rc.bottom - rc.top) + OFFSET_H;
    int w = vod->width(), h = vod->height();

    int min_width = 450;
    int min_height = min_width * h / w;

    if (mode == VALIDATE_H) {
      if (width < min_width) width = min_width;
      height = width * h / w;
    } else if (mode == VALIDATE_V) {
      if (height < min_height) height = min_height;
      width = height * w / h;
    } else {
      double coeff = (1.0 * width * w + 1.0 * height * h) / (1.0 * w * w + 1.0 * h * h);
      int dwidth = static_cast<int>(coeff * w);
      if (dwidth < min_width) dwidth = min_width;
      int dheight = dwidth * h / w;
      if (std::abs(width - dwidth) > 4 || std::abs(height - dheight) > 4) {
        width = dwidth;
        height = dheight;
      }
    }

    width += (rc.right - rc.left) + OFFSET_W;
    height += (rc.bottom - rc.top) + OFFSET_H;
  }

  static void frame_proc(NewDownload* wnd) {
    while (true) {
      wnd->mutex.lock();
      size_t index = wnd->frame_index;
      int mode = wnd->frame_mode;
      wnd->mutex.unlock();

      cv::Mat frame;
      if (mode != FM_STORYBOARD) {
        Video::Chunk chunk;
        wnd->vod->load(index, chunk, mode == FM_CACHED);
        frame = chunk.frame;
      } else {
        frame = wnd->vod->storyboard_image(index);
      }

      std::lock_guard<std::mutex> guard(wnd->mutex);
      if (index == wnd->frame_index && mode == wnd->frame_mode) {
        wnd->frame_loading = false;
        if (!frame.empty()) {
          wnd->frame_image = frame;
          PostMessage(wnd->hWnd, WM_FRAMELOADED, index, mode);
        }
        break;
      }
    }
  }

  void set_image(cv::Mat const& image) {
    if (image.empty()) return;
    display_image = image;
    cv::Mat tmp;
    cv::resize(image, tmp, cv::Size((image_frame->width() - 2) & (~3), image_frame->height() - 2), 0, 0, cv::INTER_LANCZOS4);
    if (hImage) DeleteObject(hImage);
    hImage = CVBitmap(tmp);
    image_frame->setImage(hImage);
  }
  void update_display() {
    double time = static_cast<double>(display_side & RangeSliderFrame::DRAG_LEFT ? range_slider->left() : range_slider->right());
    std::lock_guard<std::mutex> guard(mutex);
    frame_index = vod->find(time);
    frame_mode = (display_side & RangeSliderFrame::DRAG_END ? FM_FULL : FM_CACHED);
    if (!frame_loading) {
      if (frame_worker) frame_worker->join();
      frame_loading = true;
      frame_worker.reset(new std::thread(frame_proc, this));
    }
  }
  void set_storyboard(int index) {
    if (index == hover_index) return;
    std::lock_guard<std::mutex> guard(mutex);
    if (index >= 0) {
      cv::Mat image = vod->storyboard_image(index, true);
      if (!image.empty()) {
        if (!tip) tip.reset(new TipWindow(hWnd));
        tip->setImage(image);
      } else {
        if (tip) tip->removeImage();
        frame_index = index;
        frame_mode = FM_STORYBOARD;
        if (!frame_loading) {
          if (frame_worker) frame_worker->join();
          frame_loading = true;
          frame_worker.reset(new std::thread(frame_proc, this));
        }
      }
    } else {
      if (tip) tip->hideWindow();
    }
    hover_index = index;
  }

  class TipWindow : public Window {
  public:
    TipWindow(HWND hParent)
      : hDC(CreateCompatibleDC(NULL))
      , hBitmap(NULL)
    {
      if (WNDCLASSEX* wcx = createclass("VodPopup")) {
        wcx->hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);
        RegisterClassEx(wcx);
      }
      create(CW_USEDEFAULT, 0, 220, 124 + padding, "", WS_POPUP, WS_EX_TOPMOST, hParent);
      hFont = FontSys::changeSize(18);
      padding = FontSys::getTextSize(hFont, "0").cy + 4;
    }
    ~TipWindow() {
      if (hBitmap) DeleteObject(hBitmap);
      DeleteDC(hDC);
    }
    void removeImage() {
      if (hBitmap) DeleteObject(hBitmap);
      hBitmap = NULL;
      invalidate();
    }
    void setImage(cv::Mat const& image) {
      if (hBitmap) DeleteObject(hBitmap);
      size = image.size();
      SetWindowPos(hWnd, NULL, 0, 0, size.width, size.height + padding, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      if (size.width & 3) {
        cv::Mat tmp;
        int w = (size.width & (~3));
        cv::resize(image, tmp, cv::Size(w, w * size.height / size.width), 0, 0, cv::INTER_LANCZOS4);
        hBitmap = CVBitmap(tmp);
      } else {
        hBitmap = CVBitmap(image);
      }
      SelectObject(hDC, hBitmap);
      invalidate();
    }
  private:
    HDC hDC;
    HBITMAP hBitmap;
    HFONT hFont;
    cv::Size size;
    int padding;
    LRESULT onWndMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
      if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hPaintDC = BeginPaint(hWnd, &ps);
        if (hBitmap) {
          BitBlt(hPaintDC, 0, 0, size.width, size.height, hDC, 0, 0, SRCCOPY);
        }
        SelectObject(hPaintDC, hFont);
        SetBkColor(hPaintDC, 0x000000);
        SetTextColor(hPaintDC, 0xFFFFFF);
        RECT rc;
        GetClientRect(hWnd, &rc);
        rc.top = rc.bottom - padding;
        std::string text = getText();
        DrawText(hPaintDC, text.c_str(), text.size(), &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        EndPaint(hWnd, &ps);
        return 0;
      }
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  };
  std::unique_ptr<TipWindow> tip;

  void initialize() {
    if (load_button) load_button->hide();
    if (browse_button) browse_button->hide();
    vod_id_input->setPoint(PT_RIGHT, -10, 0);
    SendMessage(vod_id_input->getHandle(), EM_SETREADONLY, TRUE, 0);
    SetWindowLongPtr(vod_id_input->getHandle(), GWL_STYLE,
      GetWindowLongPtr(vod_id_input->getHandle(), GWL_STYLE) & (~ES_NUMBER));
    vod_id_input->setText(vod->title());

    SetWindowLongPtr(hWnd, GWL_STYLE, GetWindowLongPtr(hWnd, GWL_STYLE) | WS_THICKFRAME);
    int width = def_width;
    int vod_width = width - OFFSET_W;
    int vod_height = vod_width * vod->height() / vod->width();
    int height = vod_height + OFFSET_H;
    move(width, height);

    int max_time = static_cast<int>(vod->duration());

    start_time = new DateTimeFrame(this, ID_TIME_START, DTS_UPDOWN);
    start_time->setFormat("HH':'mm':'ss");
    start_time->setSize(80, 21);
    start_time->setPoint(PT_BOTTOMLEFT, 80, -64);
    start_time->setDate(0);
    addTip(start_time, "Start:");

    end_time = new DateTimeFrame(this, ID_TIME_END, DTS_UPDOWN);
    end_time->setFormat("HH':'mm':'ss");
    end_time->setSize(80, 21);
    end_time->setPoint(PT_BOTTOMLEFT, 80, -37);
    end_time->setDate(max_time);
    addTip(end_time, "End:");

    range_slider = new RangeSliderFrame(this, ID_RANGE_SLIDER);
    range_slider->setHeight(21);
    range_slider->setPoint(PT_BOTTOMLEFT, 10, -91);
    range_slider->setPoint(PT_RIGHT, -10, 0);
    range_slider->setLimits(0, max_time);
    range_slider->setRange(0, max_time);

    opt_delete_chunks = new ButtonFrame("Do not keep cache", this, 0, BS_AUTOCHECKBOX);
    opt_delete_chunks->setSize(120, 21);
    opt_delete_chunks->setPointEx(PT_BOTTOMLEFT, 0.4, 1, 0, -64);

    opt_clean_output = new ButtonFrame("Clean up output", this, 0, BS_AUTOCHECKBOX);
    opt_clean_output->setCheck(true);
    opt_clean_output->setSize(120, 21);
    opt_clean_output->setPointEx(PT_BOTTOMLEFT, 0.7, 1, 0, -64);

    max_threads = new ComboFrame(this);
    for (int i = 1; i <= 8; ++i) {
      max_threads->addString(fmtstring("%d", i), i);
    }
    max_threads->setCurSel(1);
    max_threads->setWidth(80);
    max_threads->setPointEx(PT_BOTTOMLEFT, 0.7, 1, 0, -37);
    addTip(max_threads, "Threads:");

    image_frame = new StaticFrame(this, 0, WS_BORDER | SS_BITMAP);
    image_frame->setPoint(PT_TOPLEFT, 10, 39);
    image_frame->setPoint(PT_BOTTOMRIGHT, -10, -120);

    ButtonFrame* btnDownload = new ButtonFrame("Download", this, ID_START_DOWNLOAD, BS_DEFPUSHBUTTON);
    btnDownload->setSize(80, 21);
    btnDownload->setPoint(PT_BOTTOMRIGHT, -10, -10);

    ButtonFrame* btnBrowse = new ButtonFrame("Browse", this, ID_BROWSE_PATH);
    btnBrowse->setSize(80, 21);
    btnBrowse->setPoint(PT_BOTTOMRIGHT, btnDownload, PT_BOTTOMLEFT, -6, 0);

    output_path = new EditFrame(this);
    output_path->setPoint(PT_BOTTOMLEFT, 80, -10);
    output_path->setPoint(PT_TOPRIGHT, btnBrowse, PT_TOPLEFT, -6, 0);
    addTip(output_path, "Output:");

    output_path->setText(vod->default_output());

    update_display();
  }

  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
    if (message == WM_COMMAND) {
      int id = LOWORD(wParam);
      if (id == IDOK) {
        if (GetFocus() == vod_id_input->getHandle() && !vod) {
          id = ID_BUTTON_LOAD;
        }
      }
      if (id == ID_BUTTON_LOAD && !vod) {
        if (vod_id_input->getText().empty()) return 0;
        try {
          vod.reset(Video::open_vod(std::stoi(vod_id_input->getText())));
        } catch (Exception& ex) {
          MessageBox(NULL, ex.what(), "Error", MB_OK | MB_ICONERROR);
          return 0;
        }
        initialize();
      } else if (id == ID_BUTTON_BROWSE && !vod) {
        int vod_id = 0;
        VodBrowser browser([&vod_id](int id) {
          vod_id = id;
          return true;
        }, this);
        browser.doModal();
        SetFocus(hWnd);
        if (vod_id) {
          try {
            vod.reset(Video::open_vod(vod_id));
            initialize();
          } catch (Exception& ex) {
            MessageBox(NULL, ex.what(), "Error", MB_OK | MB_ICONERROR);
          }
        }
      } else if (vod) {
        switch (id) {
        case ID_RANGE_SLIDER:
          if (HIWORD(wParam) == RangeSliderFrame::LEAVE) {
            if (tip) tip->hideWindow();
            hover_index = -1;
          } else {
            if (HIWORD(wParam) != RangeSliderFrame::HOVER) {
              display_side = HIWORD(wParam);
              if (display_side & RangeSliderFrame::DRAG_LEFT) {
                start_time->setDate(range_slider->left());
              } else {
                end_time->setDate(range_slider->right());
              }
              update_display();
            }
            double time = static_cast<double>(range_slider->hover());
            int index = vod->storyboard_index(time);
            set_storyboard(index);
            if (index >= 0) {
              if (!tip) tip.reset(new TipWindow(hWnd));
              RECT rc, rcl;
              GetClientRect(tip->getHandle(), &rc);
              GetWindowRect(range_slider->getHandle(), &rcl);
              POINT pt;
              GetCursorPos(&pt);
              SetWindowPos(tip->getHandle(), HWND_TOPMOST, pt.x, rcl.top - rc.bottom - 4, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
              tip->setText(format_time(time));
              ShowWindow(tip->getHandle(), SW_SHOWNA);
              tip->invalidate();
            }
          }
          break;
        case ID_BROWSE_PATH: {
          std::string path;
          if (browseForFolder(L"Select output directory", path)) {
            output_path->setText(path);
          }
          break;
        }
        case ID_START_DOWNLOAD: {
          json::Value config;
          vod->info(config);
          config["title"] = vod->title();
          config["start_time"] = static_cast<double>(range_slider->left());
          config["end_time"] = static_cast<double>(range_slider->right());
          config["delete_chunks"] = opt_delete_chunks->checked();
          config["clean_output"] = opt_clean_output->checked();
          config["path"] = output_path->getText();
          int threads = max_threads->getCurSel() + 1;
          if (threads < 0) threads = 1;
          if (threads > 8) threads = 8;
          config["max_threads"] = threads;
          callback(this, config);
          endModal();
          break;
        }
        }
      }
      return 0;
    } else if (message == WM_NOTIFY && vod) {
      NMHDR* nmh = reinterpret_cast<NMHDR*>(lParam);
      if (nmh->hwndFrom == start_time->getHandle() && nmh->code == DTN_DATETIMECHANGE) {
        int time = static_cast<int>(start_time->getDate() % (24 * 60 * 60));
        int max_time = static_cast<int>(vod->duration());
        int right = range_slider->right();
        if (time < 0 || (time > max_time && time > 22 * 60 * 60)) {
          time = 0;
          start_time->setDate(time);
        }
        if (time > right) {
          if (time > max_time) time = max_time;
          right = time;
          end_time->setDate(time);
        }
        range_slider->setRange(time, right);
        display_side = RangeSliderFrame::DRAG_LEFT | RangeSliderFrame::DRAG_END;
        update_display();
        return 0;
      }
      if (nmh->hwndFrom == end_time->getHandle() && nmh->code == DTN_DATETIMECHANGE) {
        int time = static_cast<int>(end_time->getDate() % (24 * 60 * 60));
        int max_time = static_cast<int>(vod->duration());
        int left = range_slider->left();
        if (time > max_time) {
          time = max_time;
          end_time->setDate(time);
        }
        if (time < left) {
          if (time < 0) time = 0;
          left = time;
          start_time->setDate(time);
        }
        range_slider->setRange(left, time);
        display_side = RangeSliderFrame::DRAG_RIGHT | RangeSliderFrame::DRAG_END;
        update_display();
        return 0;
      }
    } else if (message == WM_FRAMELOADED && vod) {
      std::lock_guard<std::mutex> guard(mutex);
      if (lParam == FM_STORYBOARD) {
        if (wParam != hover_index) return 0;
        if (frame_image.empty()) {
          if (tip) tip->hideWindow();
        } else {
          if (!tip) tip.reset(new TipWindow(hWnd));
          tip->setImage(frame_image);
        }
      } else {
        if (!frame_image.empty()) set_image(frame_image);
      }
      return 0;
    } else if (message == WM_SIZE && vod) {
      set_image(display_image);
      return 0;
    } else if (message == WM_SIZING && vod) {
      RECT* rc = reinterpret_cast<RECT*>(lParam);
      int width = rc->right - rc->left, height = rc->bottom - rc->top;
      if (wParam == WMSZ_BOTTOM || wParam == WMSZ_TOP) {
        validate_size(width, height, VALIDATE_V);
      } else if (wParam == WMSZ_LEFT || wParam == WMSZ_RIGHT) {
        validate_size(width, height, VALIDATE_H);
      } else {
        validate_size(width, height, VALIDATE_DIAG);
      }
      if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOP || wParam == WMSZ_TOPRIGHT) {
        rc->top = rc->bottom - height;
      } else {
        rc->bottom = rc->top + height;
      }
      if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_LEFT || wParam == WMSZ_BOTTOMLEFT) {
        rc->left = rc->right - width;
      } else {
        rc->right = rc->left + width;
      }
      return TRUE;
    } else if (message == WM_WINDOWPOSCHANGING && vod) {
      WINDOWPOS* pos = reinterpret_cast<WINDOWPOS*>(lParam);
      validate_size(pos->cx, pos->cy);
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return M_UNHANDLED;
  }
};

class DoDownload : public DialogWindow, public ChunkQueue {
public:
  DoDownload(int width, json::Value const& config, int queue, std::function<bool(DialogWindow*,int)>&& callback)
    : ChunkQueue(config)
    , start_time(config["start_time"].getNumber())
    , end_time(config["end_time"].getNumber())
    , hImage(NULL)
    , callback(std::move(callback))
  {
    int vod_width = width - OFFSET_W;
    int vod_height = vod_width * vod_->height() / vod_->width();
    int height = vod_height + OFFSET_H;
    std::string wndtitle = (queue ? fmtstring("VOD Scanner (%d in queue)", queue) : "VOD Scanner");
    create(width, height, wndtitle, WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT);

    EditFrame* title = new EditFrame(this, 0, ES_READONLY | ES_AUTOHSCROLL);
    title->setText(vod_->title());
    title->setPoint(PT_TOPLEFT, 10, 10);
    title->setPoint(PT_TOPRIGHT, -10, 10);
    title->setHeight(21);

    image_frame = new StaticFrame(this, 0, SS_BITMAP | WS_BORDER);
    image_frame->setPoint(PT_TOPLEFT, 10, 39);
    image_frame->setPoint(PT_BOTTOMRIGHT, -10, -39);

    time_numeric = new EditFrame(this, 0, ES_READONLY);
    time_numeric->setSize(140, 21);
    time_numeric->setPoint(PT_BOTTOMLEFT, 10, -10);
    time_numeric->setText(fmtstring("%s / %s", format_time(0).c_str(), format_time(end_time - start_time).c_str()));

    progress = new ProgressFrame(this);
    progress->setPoint(PT_BOTTOMRIGHT, -10, -10);
    progress->setPoint(PT_TOPLEFT, time_numeric, PT_TOPRIGHT, 6, 0);
    progress->setRange(static_cast<int>(start_time), static_cast<int>(end_time));

    start();
  }
  ~DoDownload() {
    if (hImage) DeleteObject(hImage);
  }
private:
  std::function<bool(DialogWindow*, int)> callback;
  EditFrame* time_numeric = nullptr;
  StaticFrame* image_frame = nullptr;
  ProgressFrame* progress = nullptr;
  double start_time, end_time;

  enum { WM_REPORT = (WM_USER + 57) };

  struct MessageData {
    int status;
    double time;
    cv::Mat frame;
  };
  std::mutex mutex;
  std::deque<MessageData> mdata;
  void report(int status, double time, cv::Mat const& frame) {
    std::lock_guard<std::mutex> guard(mutex);
    if (mdata.empty() || status != REPORT_PROGRESS) {
      MessageData m;
      m.status = status;
      m.time = time;
      m.frame = frame;
      mdata.push_back(m);
      PostMessage(hWnd, WM_REPORT, 0, 0);
    }
  }

  enum { VALIDATE_APPROX, VALIDATE_DIAG, VALIDATE_H, VALIDATE_V };
  enum { OFFSET_W = 22, OFFSET_H = 80 };
  void validate_size(int& width, int& height, int mode = VALIDATE_APPROX) {
    RECT rc = { 0, 0, 0, 0 };
    AdjustWindowRectEx(&rc, GetWindowLongPtr(hWnd, GWL_STYLE), FALSE, GetWindowLongPtr(hWnd, GWL_EXSTYLE));

    width -= (rc.right - rc.left) + OFFSET_W;
    height -= (rc.bottom - rc.top) + OFFSET_H;
    int w = vod_->width(), h = vod_->height();

    int min_width = 450;
    int min_height = min_width * h / w;

    if (mode == VALIDATE_H) {
      if (width < min_width) width = min_width;
      height = width * h / w;
    } else if (mode == VALIDATE_V) {
      if (height < min_height) height = min_height;
      width = height * w / h;
    } else {
      double coeff = (1.0 * width * w + 1.0 * height * h) / (1.0 * w * w + 1.0 * h * h);
      int dwidth = static_cast<int>(coeff * w);
      if (dwidth < min_width) dwidth = min_width;
      int dheight = dwidth * h / w;
      if (std::abs(width - dwidth) > 4 || std::abs(height - dheight) > 4) {
        width = dwidth;
        height = dheight;
      }
    }

    width += (rc.right - rc.left) + OFFSET_W;
    height += (rc.bottom - rc.top) + OFFSET_H;
  }

  cv::Mat display_image;
  HBITMAP hImage;

  void set_image(cv::Mat const& image) {
    if (image.empty()) return;
    display_image = image;
    cv::Mat tmp;
    cv::resize(image, tmp, cv::Size((image_frame->width() - 2) & (~3), image_frame->height() - 2), 0, 0, cv::INTER_LANCZOS4);
    if (hImage) DeleteObject(hImage);
    hImage = CVBitmap(tmp);
    image_frame->setImage(hImage);
  }

  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
    if (message == WM_REPORT) {
      MessageData msg;
      {
        std::lock_guard<std::mutex> guard(mutex);
        if (mdata.empty()) return 0;
        msg = mdata.front();
        mdata.pop_front();
      }
      progress->setPos(static_cast<int>(msg.time));
      set_image(msg.frame);
      if (msg.status == REPORT_PROGRESS) {
        time_numeric->setText(fmtstring("%s / %s", format_time(msg.time - start_time).c_str(), format_time(end_time - start_time).c_str()));
      } else if (msg.status == REPORT_FINISHED) {
        time_numeric->setText("Complete");
      } else if (msg.status == REPORT_STOPPED) {
        time_numeric->setText("Canceled");
      }
      if (msg.status != REPORT_PROGRESS && callback(this, msg.status)) {
        endModal();
      }
      return 0;
    } else if (message == WM_SIZE) {
      set_image(display_image);
      return 0;
    } else if (message == WM_SIZING) {
      RECT* rc = reinterpret_cast<RECT*>(lParam);
      int width = rc->right - rc->left, height = rc->bottom - rc->top;
      if (wParam == WMSZ_BOTTOM || wParam == WMSZ_TOP) {
        validate_size(width, height, VALIDATE_V);
      } else if (wParam == WMSZ_LEFT || wParam == WMSZ_RIGHT) {
        validate_size(width, height, VALIDATE_H);
      } else {
        validate_size(width, height, VALIDATE_DIAG);
      }
      if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOP || wParam == WMSZ_TOPRIGHT) {
        rc->top = rc->bottom - height;
      } else {
        rc->bottom = rc->top + height;
      }
      if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_LEFT || wParam == WMSZ_BOTTOMLEFT) {
        rc->left = rc->right - width;
      } else {
        rc->right = rc->left + width;
      }
      return TRUE;
    } else if (message == WM_WINDOWPOSCHANGING) {
      WINDOWPOS* pos = reinterpret_cast<WINDOWPOS*>(lParam);
      validate_size(pos->cx, pos->cy);
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return M_UNHANDLED;
  }
};

bool opt_new_download(DialogWindow* parent, bool hide, json::Value& config, int* width) {
  if (parent && hide) parent->hideWindow();
  bool start = false;
  NewDownload newdl(nullptr, [&start, &config, width](DialogWindow* wnd, json::Value const& json) {
    start = true;
    config = json;
    if (width) {
      *width = wnd->width();
    }
  }, width ? *width : 600, parent);
  newdl.doModal();
  if (parent && hide) parent->showWindow();
  if (!start) return false;
  delete_file((config["path"].getString() / "status.json").c_str());
  return true;
}

bool opt_open_video(DialogWindow* parent, bool hide, json::Value& config, int* width) {
  OPENFILENAME ofn;
  memset(&ofn, 0, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = (parent ? parent->getHandle() : NULL);
  ofn.lpstrFilter = "Video files\0*.webm;*.mkv;*.flv;*.vob;*.ogv;*.ogg;*.drc;*.gif;*.gifv;*.mng;*.avi;"
    "*.mov;*.qt;*.wmv;*.yuv;*.rm;*.rmvb;*.asf;*.amv;*.mp4;*.m4p;*.m4v;*.mpg;*.mp2;*.mpeg;*.mpe;*.mpv;"
    "*.m2v;*.svi;*.3gp;*.3g2;*.mxf;*.roq;*.nsv;*.flv;*.f4v;*.f4p;*.f4a;*.f4b\0All files\0*\0\0";
  char buf[512] = "";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = sizeof buf;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  if (!GetOpenFileName(&ofn)) {
    return false;
  }
  std::unique_ptr<Video> video_file;
  try {
    video_file.reset(Video::open_video(buf));
  } catch (Exception& ex) {
    MessageBox(NULL, ex.what(), "Error", MB_OK | MB_ICONERROR);
    return false;
  }

  if (parent && hide) parent->hideWindow();
  bool start = false;
  NewDownload newdl(video_file.release(), [&start, &config, width](DialogWindow* wnd, json::Value const& json) {
    start = true;
    config = json;
    if (width) {
      *width = wnd->width();
    }
  }, width ? *width : 600, parent);
  newdl.doModal();
  if (parent && hide) parent->showWindow();
  if (!start) return false;
  delete_file((config["path"].getString() / "status.json").c_str());
  return true;
}

bool opt_resume(DialogWindow* parent, bool hide, json::Value& config, int* width) {
  OPENFILENAME ofn;
  memset(&ofn, 0, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = (parent ? parent->getHandle() : NULL);
  ofn.lpstrFilter = "status.json file\0status.json\0\0";
  char path[512] = "";
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof path;
  ofn.lpstrDefExt = "vod";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  if (!GetOpenFileName(&ofn)) {
    return false;
  }

  json::Value status;
  if (!json::parse(File(path), status)) throw Exception("failed to parse status.json");
  config = status["config"];
  config["path"] = path::path(path);
  return true;
}

#define ID_LIST_QUEUE       130
#define ID_BUTTON_UP        131
#define ID_BUTTON_DOWN      132
#define ID_BUTTON_VOD       133
#define ID_BUTTON_VIDEO     134
#define ID_BUTTON_RESUME    137
#define ID_BUTTON_DEL       135
#define ID_BUTTON_START     136
#define ID_BUTTON_EDIT      138

class QueueDialog : public DialogWindow {
public:
  QueueDialog(std::function<void(DialogWindow*,json::Value const&)>&& callback, int* def_width = nullptr)
    : callback(std::move(callback))
    , def_width(def_width)
  {
    if (json::parse(File(path::root() / "queue.json"), queue)) {
      for (size_t i = 0; i < queue.length(); ++i) {
        json::Value status;
        if (queue[i].has("path") && json::parse(File(queue[i]["path"].getString() / "status.json"), status) && status.has("config")) {
          queue[i] = status["config"];
          if (!queue[i].has("resume")) {
            queue[i]["resume"] = queue[i]["start_time"];
          }
        }
      }
    }

    create(400, 300, "VOD Scanner", WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT);

    list = new ListFrame(this, ID_LIST_QUEUE, LVS_ALIGNLEFT | LVS_LIST | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
    list->setPoint(PT_TOPLEFT, 10, 10);
    list->setPoint(PT_BOTTOMRIGHT, -100, -10);

    btn_up = new ButtonFrame("Up", this, ID_BUTTON_UP);
    btn_up->setPoint(PT_TOPRIGHT, -10, 10);
    btn_up->setSize(80, 21);
    btn_down = new ButtonFrame("Down", this, ID_BUTTON_DOWN);
    btn_down->setPoint(PT_TOPLEFT, btn_up, PT_BOTTOMLEFT, 0, 6);
    btn_down->setSize(80, 21);
    btn_edit = new ButtonFrame("Edit", this, ID_BUTTON_EDIT);
    btn_edit->setPoint(PT_TOPLEFT, btn_down, PT_BOTTOMLEFT, 0, 6);
    btn_edit->setSize(80, 21);
    btn_del = new ButtonFrame("Delete", this, ID_BUTTON_DEL);
    btn_del->setPoint(PT_TOPLEFT, btn_edit, PT_BOTTOMLEFT, 0, 6);
    btn_del->setSize(80, 21);

    btn_vod = new ButtonFrame("Add Download", this, ID_BUTTON_VOD);
    btn_vod->setPointEx(PT_TOPRIGHT, 1, 0.5, -10, 2);
    btn_vod->setSize(80, 21);
    btn_video = new ButtonFrame("Add Video", this, ID_BUTTON_VIDEO);
    btn_video->setPoint(PT_TOPLEFT, btn_vod, PT_BOTTOMLEFT, 0, 6);
    btn_video->setSize(80, 21);
    btn_resume = new ButtonFrame("Add Existing", this, ID_BUTTON_RESUME);
    btn_resume->setPoint(PT_TOPLEFT, btn_video, PT_BOTTOMLEFT, 0, 6);
    btn_resume->setSize(80, 21);

    btn_start = new ButtonFrame("Start", this, ID_BUTTON_START);
    btn_start->setPoint(PT_BOTTOMRIGHT, -10, -10);
    btn_start->setSize(80, 21);

    for (size_t i = 0; i < queue.length(); ++i) {
      list->addItem(config_title(queue[i]));
    }

    update_sel();
  }
private:
  std::function<void(DialogWindow*, json::Value const&)> callback;
  json::Value queue;
  ListFrame* list;
  ButtonFrame* btn_up;
  ButtonFrame* btn_down;
  ButtonFrame* btn_edit;
  ButtonFrame* btn_vod;
  ButtonFrame* btn_video;
  ButtonFrame* btn_resume;
  ButtonFrame* btn_del;
  ButtonFrame* btn_start;
  int* def_width;

  std::string config_title(json::Value const& config) {
    if (config.has("resume")) {
      double pct = (config["resume"].getNumber() - config["start_time"].getNumber()) / (config["end_time"].getNumber() - config["start_time"].getNumber());
      if (pct < 0) pct = 0;
      if (pct > 1) pct = 1;
      return fmtstring("[%.0f%%] %s", pct * 100, config["title"].getString().c_str());
    }
    return config["title"].getString();
  }

  void update_sel() {
    int sel = ListView_GetNextItem(list->getHandle(), -1, LVNI_SELECTED);
    int count = ListView_GetItemCount(list->getHandle());
    btn_up->enable(sel > 0);
    btn_down->enable(sel >= 0 && sel < count - 1);
    btn_del->enable(sel >= 0);
    btn_edit->enable(sel >= 0 && !queue[sel].has("resume"));

    btn_start->enable(count > 0);
  }
  void flush() {
    json::write(File(path::root() / "queue.json", "wb"), queue);
    update_sel();
  }

  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
    if (message == WM_COMMAND) {
      int sel = ListView_GetNextItem(list->getHandle(), -1, LVNI_SELECTED);
      int count = ListView_GetItemCount(list->getHandle());
      json::Value tmp;
      int id = LOWORD(wParam);
      switch (id) {
      case ID_BUTTON_UP:
        if (sel > 0) {
          json::Value tmp = queue[sel - 1];
          queue[sel - 1] = queue[sel];
          queue[sel] = tmp;
          list->setItemText(sel - 1, 0, config_title(queue[sel - 1]));
          list->setItemText(sel, 0, config_title(queue[sel]));
          ListView_SetItemState(list->getHandle(), sel - 1, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
          flush();
        }
        break;
      case ID_BUTTON_DOWN:
        if (sel >= 0 && sel < count - 1) {
          json::Value tmp = queue[sel + 1];
          queue[sel + 1] = queue[sel];
          queue[sel] = tmp;
          list->setItemText(sel + 1, 0, config_title(queue[sel + 1]));
          list->setItemText(sel, 0, config_title(queue[sel]));
          ListView_SetItemState(list->getHandle(), sel + 1, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
          flush();
        }
        break;
      case ID_BUTTON_DEL:
        if (sel >= 0) {
          queue.remove(sel);
          ListView_DeleteItem(list->getHandle(), sel);
          flush();
        }
        break;
      case ID_BUTTON_EDIT:
        if (sel >= 0 && !queue[sel].has("resume")) {
          try {
            NewDownload dl(queue[sel], [this, sel](DialogWindow* wnd, json::Value const& json) {
              queue[sel] = json;
              if (def_width) *def_width = wnd->width();
              flush();
            }, def_width ? *def_width : 600, this);
            dl.doModal();
          } catch (Exception& ex) {
            MessageBox(hWnd, ex.what(), "Error", MB_OK | MB_ICONHAND);
          }
          flush();
        }
        break;
      case ID_BUTTON_VOD:
        if (opt_new_download(this, false, tmp, def_width)) {
          sel = (sel >= 0 ? sel + 1 : count);
          queue.insert(sel, tmp);
          list->insertItem(sel, config_title(tmp));
          ListView_SetItemState(list->getHandle(), sel, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
          flush();
        }
        break;
      case ID_BUTTON_VIDEO:
        if (opt_open_video(this, false, tmp, def_width)) {
          sel = (sel >= 0 ? sel + 1 : count);
          queue.insert(sel, tmp);
          list->insertItem(sel, config_title(tmp));
          ListView_SetItemState(list->getHandle(), sel, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
          flush();
        }
        break;
      case ID_BUTTON_RESUME:
        if (opt_resume(this, false, tmp, def_width)) {
          sel = (sel >= 0 ? sel + 1 : count);
          queue.insert(sel, tmp);
          list->insertItem(sel, config_title(tmp));
          ListView_SetItemState(list->getHandle(), sel, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
          flush();
        }
        break;
      case ID_BUTTON_START:
        callback(this, queue);
        endModal();
        break;
      }
      return 0;
    } else if (message == WM_NOTIFY) {
      NMHDR* nmh = reinterpret_cast<NMHDR*>(lParam);
      if (nmh->hwndFrom == list->getHandle() && nmh->code == LVN_ITEMCHANGED) {
        update_sel();
        return 0;
      }
    } else if (message == WM_GETMINMAXINFO) {
      MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lParam);
      mm->ptMinTrackSize.x = 300;
      mm->ptMinTrackSize.y = 250;
      return 0;
    }
    return M_UNHANDLED;
  }
};

bool opt_create_queue(DialogWindow* parent, bool hide, json::Value& config, int* width) {
  if (parent && hide) parent->hideWindow();
  bool start = false;
  QueueDialog queue([&start, &config](DialogWindow* wnd, json::Value const& json) {
    start = true;
    config = json;
  }, width);
  queue.doModal();
  if (parent && hide) parent->showWindow();
  return start;
}

bool opt_exit(DialogWindow* parent, bool hide, json::Value& config, int* width) {
  return true;
}

int do_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  typedef bool(*OptHandler)(DialogWindow*, bool, json::Value&, int*);
  std::vector<std::string> optNames{"New download", "Open video file", "Resume download", "Manage Queue", "Exit"};
  std::vector<OptHandler> optHandlers{opt_new_download, opt_open_video, opt_resume, opt_create_queue, opt_exit};

  size_t result = optNames.size() - 1;
  json::Value config;
  int width = 600;

  {
    LaunchDialog dlg(optNames, [&result, &config, &width, &optHandlers](DialogWindow* wnd, int choice) {
      bool res = optHandlers[choice](wnd, true, config, &width);
      if (res) result = choice;
      return res;
    });
    dlg.doModal();
  }

  if (result == optNames.size() - 1) {
    return 0;
  }

  if (result == optNames.size() - 2) {
    while (config.length()) {
      bool finished = false;
      DoDownload download(width, config[0], config.length() - 1, [&finished, &config, &width](DialogWindow* wnd, int status) {
        if (status == ChunkQueue::REPORT_FINISHED) {
          finished = true;
          config.remove(0U);
          json::write(File(path::root() / "queue.json", "wb"), config);
          width = wnd->width();
          return config.length() > 0;
        } else {
          config.clear();
          return false;
        }
      });
      download.doModal();
      if (!finished) break;
    }
  } else {
    DoDownload download(width, config, 0, [](DialogWindow* wnd, int status) {
      return false;
    });
    download.doModal();
  }

  return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  try {
    INITCOMMONCONTROLSEX iccex;
    iccex.dwSize = sizeof iccex;
    iccex.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS |
      ICC_BAR_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES |
      ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_DATE_CLASSES;
    InitCommonControlsEx(&iccex);
    OleInitialize(NULL);

    int result = do_main(hInstance, hPrevInstance, lpCmdLine, nCmdShow);

    OleFlushClipboard();
    OleUninitialize();
    return result;
  } catch (Exception& ex) {
    OleFlushClipboard();
    OleUninitialize();
    MessageBox(NULL, ex.what(), "Error", MB_OK | MB_ICONERROR);
    return 1;
  } catch (cv::Exception& ex) {
    OleFlushClipboard();
    OleUninitialize();
    MessageBox(NULL, ex.what(), "Error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
