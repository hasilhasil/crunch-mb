#pragma once

#include <JuceHeader.h>

#include "MBTheme.h"
#include "Anim.h"

class CrunchMBAudioProcessor;

// ============================================================
// FreqInputEditor — 自绘频率输入框（从 DisplayView 拆出）
//
// 为什么自绘而不用 juce::TextEditor：
//   TextEditor 在宿主 VST3 编辑器里会弹一个独立顶层窗口（#7 用户实测），
//   而不是内嵌在插件面板上。所以这里用 Graphics 手绘 + keyPressed 收键。
//
// 两种模式：
//   · divider >= 0 → 编辑分割线（crossover）频率
//   · band    >= 0 → 编辑频段中心频率
//
// 语法：数字 / '.' / 'k'（'1k' → 1000）。Enter 确认，Esc 取消，Backspace 删。
// ============================================================
class FreqInputEditor : public juce::Component
{
public:
    /** 应用回调：divider>=0 时为分割线索引，否则 band>=0 为段索引。 */
    std::function<void (int divider, int band, float hz)> onApply;

    FreqInputEditor() = default;
    ~FreqInputEditor() override = default;

    void paint (juce::Graphics&) override;

    // ---- 状态 ----
    bool isActive() const noexcept { return active_; }
    int  divider() const noexcept { return divider_; }
    int  band()    const noexcept { return band_; }

    /** 打开输入框。x/y = 锚点屏幕坐标（设计稿 920×620 基准的像素）。 */
    void open (int divider, int band, float x, float y);
    void close (bool apply);

    /** 键盘处理（DisplayView::keyPressed 转发）。返回 true 表示已消费。 */
    bool keyPressed (const juce::KeyPress& key);

    /** 光标闪烁推进（半周期 Anim::kBlinkMs）。 */
    void tickCursor (juce::uint32 nowMs);

    void setTheme (MBTheme::Theme t) { pal_ = MBTheme::palette (t); repaint(); }

private:
    bool  active_ = false;
    int   divider_ = -1;
    int   band_    = -1;
    float x_ = 0.0f, y_ = 0.0f;
    juce::String text_;
    bool  cursorVisible_ = true;
    juce::uint32 lastBlinkMs_ = 0;
    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);
};
