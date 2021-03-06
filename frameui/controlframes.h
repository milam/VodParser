#pragma once

#include "frameui/frame.h"
#include "frameui/window.h"
#include "frameui/framewnd.h"
#include <CommCtrl.h>

class ButtonFrame : public WindowFrame {
  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam);
public:
  ButtonFrame(std::string const& text, Frame* parent, int id = 0, int style = BS_PUSHBUTTON);
  void setCheck(bool check) {
    SendMessage(hWnd, BM_SETCHECK, check ? BST_CHECKED : BST_UNCHECKED, 0);
  }
  bool checked() const {
    return SendMessage(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
  }
};
class LinkFrame : public WindowFrame {
  HFONT hFont;
  HFONT uFont;
  uint32 _color;
  uint32 _flags;
  bool pressed;
  bool hover;
  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam);
public:
  LinkFrame(std::string const& text, Frame* parent, int id = 0);

  void setColor(uint32 color);
  void setFlags(int flags);
  uint32 color() const {
    return _color;
  }
  int flags() const {
    return _flags;
  }

  void resetSize();
};

class HotkeyFrame : public WindowFrame {
public:
  HotkeyFrame(Frame* parent, int id = 0);

  int getKey() const {
    return SendMessage(hWnd, HKM_GETHOTKEY, 0, 0) & 0xFFFF;
  }
  void setKey(int key) {
    SendMessage(hWnd, HKM_SETHOTKEY, key & 0xFFFF, 0);
  }
};

class EditFrame : public WindowFrame {
  HBRUSH background;
  uint32 bgcolor;
  uint32 fgcolor;
  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam);
public:
  EditFrame(Frame* parent, int id = 0, int style = ES_AUTOHSCROLL);
  ~EditFrame();
  void setFgColor(uint32 color);
  void setBgColor(uint32 color);
};

class ComboFrame : public WindowFrame {
  void onMove(void* data);
  int boxHeight;
public:
  ComboFrame(Frame* parent, int id = 0, int style = CBS_DROPDOWNLIST);
  void reset();
  int addString(std::string const& text, int data = 0);
  void delString(int pos);
  int getItemData(int item) const;
  void setItemData(int item, int data);
  int getCurSel() const;
  void setCurSel(int sel);
  int getCount() const;
  void setBoxHeight(int ht) {
    boxHeight = ht;
    onMove(0);
  }
};

class StaticFrame : public WindowFrame {
public:
  StaticFrame(Frame* parent, int id = 0, int style = 0, int exStyle = 0);
  StaticFrame(std::string const& text, Frame* parent, int id = 0, int style = 0, int exStyle = 0);
  void setImage(HANDLE image, int type = IMAGE_BITMAP);
  void resetSize();
};

class RichEditFrame : public WindowFrame {
  static DWORD CALLBACK StreamCallback(DWORD_PTR cookie, LPBYTE buff, LONG cb, LONG* pcb);
public:
  RichEditFrame(Frame* parent, int id = 0, int style = WS_VSCROLL | WS_HSCROLL |
    ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_READONLY);
  void setBackgroundColor(uint32 color);
  void setRichText(std::string const& text);
};

class SliderFrame : public WindowFrame {
public:
  SliderFrame(Frame* parent, int id = 0, int style = TBS_AUTOTICKS | TBS_BOTH);

  void setPos(int pos);
  void setRange(int minValue, int maxValue);
  void setLineSize(int size);
  void setPageSize(int size);
  void setTicFreq(int freq);
  int getPos();
};
class RangeSliderFrame : public WindowFrame {
public:
  RangeSliderFrame(Frame* parent, int id = 0);

  enum {
    DRAG_START    = 0x01,
    DRAG_MOVE     = 0x02,
    DRAG_END      = 0x04,
    DRAG_LEFT     = 0x10,
    DRAG_RIGHT    = 0x20,
    HOVER         = 0,
    LEAVE         = 0x100,
  };

  void setLimits(int minValue, int maxValue) {
    min_ = minValue;
    max_ = maxValue;
    invalidate();
  }
  void setRange(int left, int right) {
    left_ = left;
    right_ = right;
    drag_ = 0;
    invalidate();
  }
  void setLeft(int left) {
    left_ = left;
    drag_ = 0;
    invalidate();
  }
  void setRight(int right) {
    right_ = right;
    drag_ = 0;
    invalidate();
  }

  int left() const {
    return left_;
  }
  int right() const {
    return right_;
  }

  int hover() const {
    return hover_;
  }

private:
  int min_, max_;
  int left_, right_;
  int drag_, hover_, hovering_;
  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam);
};

class ProgressFrame : public WindowFrame {
public:
  ProgressFrame(Frame* parent, int id = 0, int style = 0);

  void setRange(int minValue, int maxValue);
  void setPos(int pos);
};

class UpDownFrame : public WindowFrame {
public:
  UpDownFrame(Frame* parent, int id = 0, int style = 0);
};

class TabFrame : public WindowFrame {
protected:
  std::vector<Frame*> tabs;
  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam);
public:
  TabFrame(Frame* parent, int id = 0, int style = 0);

  size_t numTabs() const {
    return tabs.size();
  }
  Frame* addTab(std::string const& text, Frame* frame = NULL);
  Frame* getTab(size_t pos) const {
    return (pos >= tabs.size() ? nullptr : tabs[pos]);
  }

  void clear();

  int getCurSel() const {
    return TabCtrl_GetCurSel(hWnd);
  }
  void setCurSel(int sel);
};

class ColorFrame : public WindowFrame {
  uint32 color;
  LRESULT onMessage(uint32 message, WPARAM wParam, LPARAM lParam);
public:
  ColorFrame(uint32 clr, Frame* parent, uint32 id);
  ~ColorFrame();

  void setColor(uint32 clr) {
    color = clr;
    invalidate();
  }
  uint32 getColor() {
    return color;
  }
};

class TreeViewFrame : public WindowFrame {
public:
  TreeViewFrame(Frame* parent, int id = 0, int style = 0);

  void setImageList(HIMAGELIST list, int type) {
    TreeView_SetImageList(hWnd, list, type);
  }
  void setItemHeight(int height) {
    TreeView_SetItemHeight(hWnd, height);
  }

  HTREEITEM insertItem(TVINSERTSTRUCT* tvis) {
    return TreeView_InsertItem(hWnd, tvis);
  }
  void deleteItem(HTREEITEM item) {
    TreeView_DeleteItem(hWnd, item);
  }

  LPARAM getItemData(HTREEITEM item);

  void setItemText(HTREEITEM item, std::string const& text);
};

class DateTimeFrame : public WindowFrame {
public:
  DateTimeFrame(Frame* parent, int id = 0, int style = DTS_SHORTDATEFORMAT);
  void setFormat(char const* fmt);

  bool isDateSet() const;
  uint64 getDate() const;
  void setNoDate();
  void setDate(uint64 date);
};

class ListFrame : public WindowFrame {
public:
  ListFrame(Frame* parent, int id = 0, int style = LVS_ALIGNLEFT | LVS_REPORT |
    LVS_NOCOLUMNHEADER | LVS_NOSCROLL | LVS_SINGLESEL, int styleEx = WS_EX_CLIENTEDGE);
  void clear();
  void setColumns(int numColumns);
  void setColumn(int column, int width, int fmt);
  void setColumnWidth(int column, int width);
  void setColumn(int column, int width, std::string const& text);
  int addItem(std::string const& name);
  int addItem(std::string const& name, uint32 param);
  int insertItem(int pos, std::string const& name);
  void setItemText(int item, int column, std::string const& text);
  void setItemTextUtf8(int item, int column, std::string const& text);
};
