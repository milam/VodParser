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
  void create(int width, int height, std::string const& text, int style, int exStyle) {
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
      text, style, exStyle);
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
  LaunchDialog(std::vector<std::string> const& options, std::function<bool(HWND, int)>&& callback)
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
  std::function<bool(HWND, int)> callback_;

  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
    if (message == WM_COMMAND) {
      int id = LOWORD(wParam);
      if (id >= ID_BUTTON_0 && id < ID_BUTTON_0 + buttons_.size()) {
        if (callback_(hWnd, id - ID_BUTTON_0)) {
          endModal();
        }
      }
      return 0;
    }
    return M_UNHANDLED;
  }
};

#define ID_BUTTON_LOAD      131
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

class NewDownload : public DialogWindow {
public:
  NewDownload(std::function<void(HWND, json::Value const&)>&& callback)
    : callback(std::move(callback))
  {
    create(400, 41, "VOD Scanner", WS_OVERLAPPED | WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT);

    vod_id_input = new EditFrame(this, 0, ES_NUMBER | ES_AUTOHSCROLL);
    vod_id_input->setPoint(PT_TOPLEFT, 10, 10);
    vod_id_input->setHeight(21);
    load_button = new ButtonFrame("Load", this, ID_BUTTON_LOAD);
    load_button->setSize(80, 21);
    load_button->setPoint(PT_TOPRIGHT, -10, 10);
    vod_id_input->setPoint(PT_RIGHT, load_button, PT_LEFT, -6, 0);
    Edit_SetCueBannerText(vod_id_input->getHandle(), L"Enter VOD ID");

//    vod_id_input->setText("79588945");
//    onMessage(WM_COMMAND, ID_BUTTON_LOAD, 0);
  }

  ~NewDownload() {
    if (hImage) DeleteObject(hImage);
    if (frame_worker) frame_worker->join();
  }

private:
  std::function<void(HWND, json::Value const&)> callback;
  EditFrame* vod_id_input;
  ButtonFrame* load_button;
  RangeSliderFrame* range_slider = nullptr;
  StaticFrame* image_frame = nullptr;
  DateTimeFrame* start_time = nullptr;
  DateTimeFrame* end_time = nullptr;
  ButtonFrame* opt_delete_chunks = nullptr;
  ButtonFrame* opt_clean_output = nullptr;
  EditFrame* output_path = nullptr;
  ComboFrame* max_threads = nullptr;
  std::unique_ptr<VOD> vod;
  int display_side = RangeSliderFrame::DRAG_LEFT | RangeSliderFrame::DRAG_END;
  int hover_index = -1;

  enum { WM_FRAMELOADED = (WM_USER + 57) };

  std::mutex mutex;
  std::unique_ptr<std::thread> frame_worker;
  bool frame_loading = false;
  bool frame_small;
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
      bool small = wnd->frame_small;
      wnd->mutex.unlock();

      cv::VideoCapture cap;
      VOD::Chunk chunk;
      if (wnd->vod->load(index, chunk, small)) {
        cap.open(chunk.path);
      }

      std::lock_guard<std::mutex> guard(wnd->mutex);
      if (index == wnd->frame_index && small == wnd->frame_small) {
        wnd->frame_loading = false;
        if (cap.isOpened()) {
          cap >> wnd->frame_image;
          PostMessage(wnd->hWnd, WM_FRAMELOADED, 0, 0);
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
    frame_small = (display_side & RangeSliderFrame::DRAG_END ? false : true);
    if (!frame_loading) {
      if (frame_worker) frame_worker->join();
      frame_loading = true;
      frame_worker.reset(new std::thread(frame_proc, this));
    }
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
      create(CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, "", WS_POPUP, WS_EX_TOPMOST, hParent);
      hFont = FontSys::changeSize(18);
      padding = FontSys::getTextSize(hFont, "0").cy + 4;
    }
    ~TipWindow() {
      if (hBitmap) DeleteObject(hBitmap);
      DeleteDC(hDC);
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

  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam) override {
    if (message == WM_COMMAND) {
      int id = LOWORD(wParam);
      if (id == ID_BUTTON_LOAD && !vod) {
        if (vod_id_input->getText().empty()) return 0;
        try {
          vod.reset(new VOD(std::stoi(vod_id_input->getText())));
        } catch (Exception& ex) {
          MessageBox(NULL, ex.what(), "Error", MB_OK | MB_ICONERROR);
          return 0;
        }

        load_button->hide();
        vod_id_input->setPoint(PT_RIGHT, -10, 0);
        SendMessage(vod_id_input->getHandle(), EM_SETREADONLY, TRUE, 0);
        SetWindowLongPtr(vod_id_input->getHandle(), GWL_STYLE,
          GetWindowLongPtr(vod_id_input->getHandle(), GWL_STYLE) & (~ES_NUMBER));
        vod_id_input->setText(fmtstring("[%d] %s", vod->id(), vod->info()["title"].getString().c_str()));

        SetWindowLongPtr(hWnd, GWL_STYLE, GetWindowLongPtr(hWnd, GWL_STYLE) | WS_THICKFRAME);
        int width = 600;
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

        output_path->setText(path::root() / fmtstring("%d", vod->id()));

        update_display();
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
            if (index != hover_index) {
              if (index >= 0) {
                cv::Mat img = vod->storyboard_image(index);
                if (img.empty()) {
                  index = -1;
                  if (tip) tip->hideWindow();
                } else {
                  if (!tip) tip.reset(new TipWindow(hWnd));
                  tip->setImage(img);
                }
              } else {
                if (tip) tip->hideWindow();
              }
              hover_index = index;
            }
            if (index >= 0 && tip) {
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
          config["vod_id"] = vod->id();
          config["start_time"] = static_cast<double>(range_slider->left());
          config["end_time"] = static_cast<double>(range_slider->right());
          config["delete_chunks"] = opt_delete_chunks->checked();
          config["clean_output"] = opt_clean_output->checked();
          config["path"] = output_path->getText();
          int threads = max_threads->getCurSel() + 1;
          if (threads < 0) threads = 1;
          if (threads > 8) threads = 8;
          config["max_threads"] = threads;
          callback(hWnd, config);
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
        if (time < 0) time = 0;
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
        if (time > max_time) time = max_time;
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
      if (!frame_image.empty()) set_image(frame_image);
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
  DoDownload(int width, json::Value const& config)
    : ChunkQueue(config)
    , start_time(config["start_time"].getNumber())
    , end_time(config["end_time"].getNumber())
    , hImage(NULL)
  {
    int vod_width = width - OFFSET_W;
    int vod_height = vod_width * vod_.height() / vod_.width();
    int height = vod_height + OFFSET_H;
    create(width, height, "VOD Scanner", WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT);

    EditFrame* title = new EditFrame(this, 0, ES_READONLY | ES_AUTOHSCROLL);
    title->setText(fmtstring("[%d] %s", vod_.id(), vod_.info()["title"].getString().c_str()));
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
    int w = vod_.width(), h = vod_.height();

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

int do_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

  int result;
  std::string path;

  {
    LaunchDialog dlg({"New download", "Resume download", "Exit"}, [&result, &path](HWND hWnd, int choice) {
      if (choice == 1) {
        OPENFILENAME ofn;
        memset(&ofn, 0, sizeof ofn);
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = hWnd;
        ofn.lpstrFilter = "status.json file\0status.json\0\0";
        char buf[512] = "";
        ofn.lpstrFile = buf;
        ofn.nMaxFile = sizeof buf;
        ofn.lpstrDefExt = "vod";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetOpenFileName(&ofn)) {
          return false;
        }
        path = buf;
        result = choice;
        return true;
      } else {
        result = choice;
        return true;
      }
    });
    dlg.doModal();
  }

  json::Value config;
  int width = 600;

  if (result == 0) {
    bool start = false;
    NewDownload newdl([&start, &config, &width](HWND hWnd, json::Value const& json) {
      start = true;
      config = json;
      RECT rc;
      GetClientRect(hWnd, &rc);
      width = rc.right;
    });
    newdl.doModal();
    if (!start) return 0;
    delete_file((config["path"].getString() / "status.json").c_str());
    //delete_file((config["path"].getString() / "picks.txt").c_str());
  } else if (result == 1) {
    json::Value status;
    if (!json::parse(File(path), status)) throw Exception("failed to parse status.json");
    config = status["config"];
    config["path"] = path::path(path);

    //SetCurrentDirectory(path::root().c_str());
    //VOD vod(config["vod_id"].getInteger());
    //VOD::Chunk chunk;
    //if (vod.load(0, chunk)) {
    //  cv::VideoCapture vid;
    //  vid.open(chunk.path);
    //}
  } else {
    return 0;
  }

  DoDownload download(width, config);
  download.doModal();

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
  }
}
