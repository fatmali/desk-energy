// Energy dashboard for the 320x170 ST7789: score, hourly history (or the
// focus timer while it runs), and a level bar. Drawn strip by strip into the
// same GFXcanvas16 the sketch already uses, so it needs no extra RAM.
//
// Include this AFTER <Fonts/FreeSans9pt7b.h> and <Fonts/FreeSans24pt7b.h>;
// the sketch already includes both and older Adafruit GFX releases do not
// guard those headers against being included twice.
#pragma once
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <string.h>

#include "EnergyDigits.h"
#include "EnergyState.h"

namespace energy_dashboard {
constexpr uint16_t rgb(int r, int g, int b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t scoreColor(int score) {
  return score < 25 ? rgb(244, 139, 155) :
         score < 50 ? rgb(245, 186, 102) :
         score < 75 ? rgb(128, 223, 198) : rgb(184, 88, 255);
}

static_assert(scoreColor(0) == scoreColor(24) && scoreColor(24) != scoreColor(25) &&
              scoreColor(25) == scoreColor(49) && scoreColor(49) != scoreColor(50) &&
              scoreColor(50) == scoreColor(74) && scoreColor(74) != scoreColor(75) &&
              scoreColor(75) == scoreColor(100), "Dashboard colour tiers must match score boundaries");
}  // namespace energy_dashboard

class EnergyDashboard {
 public:
  static constexpr int Width = 320;
  static constexpr int Height = 170;
  enum class Timer : uint8_t { Hidden, Paused, Running };

  struct Frame {
    EnergyStatus status;
    int score;                // 0-100, displayed only while fresh or stale
    Timer timer;
    const char *timerLabel;   // "FOCUS", "BREAK", "PAUSED"
    uint32_t timerSeconds;
    int clockMinutes;         // local minutes since midnight, or -1 if unknown
  };

  // Returns true when something visible changed and a redraw is needed.
  bool prepare(const Frame &next, uint32_t nowMs) {
    Frame f = next;
    f.score = energyScoreVisible(f.status) ? (f.score < 0 ? 0 : f.score > 100 ? 100 : f.score) : 0;
    if (!f.timerLabel) f.timerLabel = "";
    bool changed = !started_ || f.status != frame_.status || f.score != frame_.score ||
        f.timer != frame_.timer || f.clockMinutes != frame_.clockMinutes ||
        (f.timer != Timer::Hidden &&
         (f.timerSeconds != frame_.timerSeconds || strcmp(f.timerLabel, label_) != 0));
    if (f.status == EnergyStatus::Fresh) changed |= record(uint8_t(f.score), nowMs / 3600000UL);
    frame_ = f;
    strncpy(label_, f.timerLabel, sizeof(label_) - 1);
    started_ = true;
    if (changed) layoutStale_ = true;
    return changed;
  }

  // Draws rows [y, y + rows) of the screen into the strip canvas.
  void render(GFXcanvas16 &c, int y, int rows) {
    if (layoutStale_) layout(c);
    c.fillScreen(Background);
    c.setTextSize(1);
    c.setTextWrap(false);
    const auto hits = [&](int top, int bottom) { return y < bottom && y + rows > top; };

    if (hits(10, 34)) {
      text(c, &FreeSansBold9pt7b, Violet, Margin, 28 - y, "ENERGY");
      if (clock_[0]) text(c, &FreeSans9pt7b, Muted, clockX_, 28 - y, clock_);
      if (frame_.status == EnergyStatus::Stale) {
        const int x = clock_[0] ? clockX_ - 12 : Width - Margin - 5;
        c.drawCircle(x, 21 - y, 4, Amber);
        c.drawLine(x, 21 - y, x, 18 - y, Amber);
        c.drawLine(x, 21 - y, x + 2, 22 - y, Amber);
      }
    }

    if (hits(46, 102)) {
      if (energyScoreVisible(frame_.status)) {
        const uint16_t color = energy_dashboard::scoreColor(frame_.score);
        text(c, &EnergyDigits, color, Margin - 2, NumberBaseline - y, number_);
        text(c, &FreeSansBold12pt7b, color, percentX_, NumberBaseline - y, "%");
      } else {
        for (int i = 0; i < 2; ++i) c.fillRoundRect(Margin + i * 34, 72 - y, 26, 8, 4, Muted);
      }
      if (frame_.timer == Timer::Hidden) {
        for (int i = 0; i < HistorySlots; ++i) {
          const bool empty = history_[i] == Empty;
          const int h = empty ? BarWidth : max(BarWidth, history_[i] * BarMaxHeight / 100);
          c.fillRoundRect(PanelLeft + i * BarPitch, NumberBaseline - h - y, BarWidth, h, BarWidth / 2 - 1,
                          empty ? Track : energy_dashboard::scoreColor(history_[i]));
        }
      } else {
        const bool paused = frame_.timer == Timer::Paused;
        text(c, &FreeSansBold9pt7b, paused ? Muted : Violet, labelX_, 58 - y, label_);
        text(c, &FreeSans24pt7b, paused ? Muted : Ivory, timeX_, NumberBaseline - y, time_);
      }
    }

    if (hits(BarTop, BarTop + BarHeight)) {
      const int width = Width - 2 * Margin;
      c.fillRoundRect(Margin, BarTop - y, width, BarHeight, BarHeight / 2, Track);
      if (energyScoreVisible(frame_.status) && frame_.score > 0) {
        c.fillRoundRect(Margin, BarTop - y, max(BarHeight, width * frame_.score / 100), BarHeight,
                        BarHeight / 2, energy_dashboard::scoreColor(frame_.score));
      }
    }

    if (!energyScoreVisible(frame_.status) && hits(150, Height)) {
      text(c, &FreeSans9pt7b, Muted, Margin, 162 - y, energyStatusText(frame_.status));
    }
  }

 private:
  static constexpr uint16_t Background = energy_dashboard::rgb(14, 14, 26);
  static constexpr uint16_t Ivory = energy_dashboard::rgb(250, 244, 232);
  static constexpr uint16_t Violet = energy_dashboard::scoreColor(75);
  static constexpr uint16_t Amber = energy_dashboard::scoreColor(25);
  static constexpr uint16_t Muted = energy_dashboard::rgb(120, 120, 150);
  static constexpr uint16_t Track = energy_dashboard::rgb(40, 38, 66);

  static constexpr int Margin = 20;
  static constexpr int NumberBaseline = 98;
  static constexpr int PanelLeft = 180;       // history bars / timer live in x 180..300
  static constexpr int PanelCentre = 240;
  static constexpr int HistorySlots = 7;      // one bar per hour, newest on the right
  static constexpr int BarWidth = 12;
  static constexpr int BarPitch = 18;
  static constexpr int BarMaxHeight = 52;
  static constexpr int BarTop = 128;
  static constexpr int BarHeight = 16;
  static constexpr uint8_t Empty = 255;

  static int max(int a, int b) { return a > b ? a : b; }

  static void text(GFXcanvas16 &c, const GFXfont *font, uint16_t color, int x, int baseline,
                   const char *value) {
    c.setFont(font);
    c.setTextColor(color);
    c.setCursor(x, baseline);
    c.print(value);
  }

  static void bounds(GFXcanvas16 &c, const GFXfont *font, const char *value, int &left, int &width) {
    int16_t x1, y1;
    uint16_t w, h;
    c.setFont(font);
    c.setTextSize(1);
    c.getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
    left = x1;
    width = w;
  }

  // Keeps the last seven hourly values. Hours are counted from boot, so the
  // history is empty after a restart and does not depend on the clock.
  bool record(uint8_t score, uint32_t hour) {
    bool shifted = false;
    if (!historyStarted_) {
      historyStarted_ = true;
    } else if (hour != historyHour_) {
      const uint32_t steps = hour - historyHour_;
      for (int i = 0; i < HistorySlots; ++i) {
        history_[i] = i + steps < uint32_t(HistorySlots) ? history_[i + steps] : Empty;
      }
      shifted = true;
    }
    historyHour_ = hour;
    history_[HistorySlots - 1] = score;
    return shifted;
  }

  void layout(GFXcanvas16 &c) {
    int left, width;
    snprintf(number_, sizeof(number_), "%d", frame_.score);
    bounds(c, &EnergyDigits, number_, left, width);
    percentX_ = Margin - 2 + left + width + 4;
    if (frame_.clockMinutes >= 0) {
      snprintf(clock_, sizeof(clock_), "%02d:%02d", frame_.clockMinutes / 60 % 24, frame_.clockMinutes % 60);
      bounds(c, &FreeSans9pt7b, clock_, left, width);
      clockX_ = Width - Margin - width - left;
    } else {
      clock_[0] = 0;
    }
    snprintf(time_, sizeof(time_), "%02lu:%02lu",
             static_cast<unsigned long>(frame_.timerSeconds / 60 % 100),
             static_cast<unsigned long>(frame_.timerSeconds % 60));
    bounds(c, &FreeSans24pt7b, time_, left, width);
    timeX_ = PanelCentre - width / 2 - left;
    bounds(c, &FreeSansBold9pt7b, label_, left, width);
    labelX_ = PanelCentre - width / 2 - left;
    layoutStale_ = false;
  }

  Frame frame_ = {};
  char label_[12] = {};
  char number_[8] = {};
  char clock_[8] = {};
  char time_[8] = {};
  int percentX_ = 0, clockX_ = 0, timeX_ = 0, labelX_ = 0;
  uint8_t history_[HistorySlots] = {Empty, Empty, Empty, Empty, Empty, Empty, Empty};
  uint32_t historyHour_ = 0;
  bool historyStarted_ = false;
  bool started_ = false;
  bool layoutStale_ = true;
};
