#include "LinkedDrawer.h"
#include "../PluginProcessor.h"

// ============================================================
// LinkedDrawer 实现（UI_PLAN_V2 §3.2）
// 从 DisplayView 拆出：−250 行，DisplayView 不再持有抽屉的任何状态。
// ============================================================

namespace
{
    // 与 DisplayView 的 sx/sy 等价：以 920×620 设计稿为基准等比缩放。
    // 抽屉自身不知道宿主缩放因子，由 setHostBounds 提供。
    struct Scale { float x = 1.0f, y = 1.0f; };
}

LinkedDrawer::LinkedDrawer (CrunchMBAudioProcessor& processor)
    : juce::Component ("CrunchMBLinkedDrawer"), proc_ (processor)
{
    // R34（N1）：必须是 (false, true)。
    //   · 第 1 个 false = 抽屉自身不拦截 → 点空白仍由 DisplayView 分发；
    //   · 第 2 个**必须 true** = 允许子组件（5 个 Knob）接收点击。
    // 原先写的是 (false, false)，而 JUCE 文档（juce_Component.h:969-973）明确：
    //   "if this is false and allowClicksOnThisComponent is false, then neither
    //    this component nor any child components can be clicked on"
    // 即整棵子树都点不动 → 用户实测"二级菜单里的旋钮完全没法操作"。
    setInterceptsMouseClicks (false, true);
}

LinkedDrawer::~LinkedDrawer() = default;

void LinkedDrawer::setTheme (MBTheme::Theme t)
{
    pal_ = MBTheme::palette (t);
    // R34（N6/N8）：必须把新调色板下发给 5 个 Knob。
    // Knob 是独立子组件、自带 pal_，只 repaint() 自己是刷不动它们的——
    // 齿轮接上主题切换后，抽屉旋钮会停留在旧配色。
    // 对照 BandPopover::setTheme，那里一直是循环设置的。
    for (auto& kb : knobs_)
        if (kb != nullptr) kb->setPalette (pal_);
    repaint();
}

void LinkedDrawer::setHostBounds (juce::Rectangle<int> hostBounds)
{
    if (hostBounds_ == hostBounds)
        return;
    hostBounds_ = hostBounds;
    layoutKnobs (true);
    repaint();
}

juce::Rectangle<float> LinkedDrawer::panelRect() const
{
    // 面板：宽 5×92 = 460（注意：梯形**按钮**是 456，面板是 460，二者不同宽），
    // 居中；贴在标签页上方（留 4px 间隙）。
    // R36（B3）：高度 120 → 148 —— 新增一行标题栏（标题 + 锁定按钮），
    //   同时给放大后的字号留出版面（原先 5 个旋钮几乎顶满，字大了会被裁）。
    const float W = (float) hostBounds_.getWidth();
    const float H = (float) hostBounds_.getHeight();
    const float pw = W * (460.0f / 920.0f);          // sx(5 × 92)
    const float ph = H * (148.0f / 620.0f);
    const float px = (W - pw) * 0.5f;
    const float buttonTop = H - H * (32.0f / 620.0f) - H * (29.0f / 620.0f);   // global bar + 按钮高
    return juce::Rectangle<float> (px, buttonTop - ph - H * (4.0f / 620.0f), pw, ph);
}

juce::Rectangle<float> LinkedDrawer::buttonRect() const
{
    const float W = (float) hostBounds_.getWidth();
    const float H = (float) hostBounds_.getHeight();
    const float cw = W * (456.0f / 920.0f), ch = H * (29.0f / 620.0f);
    const float cx = (W - cw) * 0.5f;
    const float cy = H - H * (32.0f / 620.0f) - ch + 1.0f;
    return juce::Rectangle<float> (cx, cy, cw, ch);
}

// R36（B3）：「锁定」按钮 —— 面板标题栏右上角，按下态（sticky）用 accent 实心。
juce::Rectangle<float> LinkedDrawer::pinRect() const
{
    const auto panel = panelRect();
    const float s = panel.getWidth() * (24.0f / 460.0f);
    return juce::Rectangle<float> (panel.getRight() - s - panel.getWidth() * (12.0f / 460.0f),
                                   panel.getY() + panel.getHeight() * (8.0f / 148.0f),
                                   s, s);
}

bool LinkedDrawer::hitTestPin (juce::Point<float> p) const
{
    return pinRect().expanded (2.0f).contains (p);
}

void LinkedDrawer::setSticky (bool s)
{
    if (s == sticky_)
        return;
    sticky_ = s;
    if (sticky_ && ! open_)
        setOpen (true);           // 点锁定 = 明确要它停住
    repaint();
}

bool LinkedDrawer::hitTestButton (juce::Point<float> p) const
{
    return buttonRect().contains (p);
}

bool LinkedDrawer::hitTestPanel (juce::Point<float> p) const
{
    return open_ && panelRect().contains (p);
}

void LinkedDrawer::setOpen (bool open)
{
    if (open == open_)
        return;
    open_ = open;
    if (open)
        ensureKnobs();
    // 目标变化时从"当前显示值"起跳，不回弹
    tween_.start (juce::Time::getMillisecondCounter(), open ? 1.0f : 0.0f,
                  Anim::kDrawerMs, anim_);
    animTarget_ = open ? 1.0f : 0.0f;
    layoutKnobs (false);
    repaint();
}

void LinkedDrawer::toggle()
{
    setOpen (! open_);
}

// ---------------- Phase D-2：Pro-MB sticky 语义 ----------------
// R36（B2）：hoverButton() 已删除 —— 底栏按钮改纯点击展开。
// R36（B3）：新增 setSticky()，由面板标题栏的「锁定」按钮驱动，带按下态。
bool LinkedDrawer::clickButton()
{
    if (! sticky_)
    {
        // 第一次点击：变 sticky 并保持打开
        sticky_ = true;
        setOpen (true);
    }
    else
    {
        // 已 sticky：再点 = 收起并解除 sticky
        sticky_ = false;
        setOpen (false);
    }
    return true;
}

void LinkedDrawer::clickOutside()
{
    if (! open_ && ! sticky_)
        return;
    sticky_ = false;                 // 点外面 = 完全收起（连 sticky 一起解除）
    setOpen (false);
}

bool LinkedDrawer::advance (double nowMs)
{
    anim_ = tween_.value (nowMs);
    if (std::abs (anim_ - animTarget_) < Anim::kSnapEps)
    {
        anim_ = animTarget_;
        tween_.snapTo (animTarget_);
    }
    layoutKnobs (false);
    syncKnobVisibility();
    return ! tween_.done (nowMs);
}

void LinkedDrawer::snapToCurrent()
{
    anim_ = animTarget_ = open_ ? 1.0f : 0.0f;
    tween_.snapTo (anim_);
    if (open_) ensureKnobs();
    layoutKnobs (true);
    syncKnobVisibility();
}

void LinkedDrawer::ensureKnobs()
{
    if (knobs_[0] != nullptr)
        return;

    // 4 个偏移旋钮（Δ 语义）+ 1 个 Mix（绝对值）
    struct Cfg { const char* label; double mn, mx, def; int dec; const char* suf; };
    const Cfg cfgs[kLinkedKnobs] = {
        { "THRESHOLD", -60.0,   60.0,  0.0, 1, "dB" },
        { "ATTACK",   -200.0,  200.0,  0.0, 1, "ms" },
        { "RELEASE",  -900.0,  900.0,  0.0, 1, "ms" },
        { "MAKEUP",    -36.0,   36.0,  0.0, 1, "dB" },
        { "MIX",         0.0,  100.0, 100.0, 0, "%" },
    };

    for (int k = 0; k < kLinkedKnobs; ++k)
    {
        auto* kb = new Knob();
        kb->setRange (cfgs[k].mn, cfgs[k].mx, (k == kLinkedMixIndex) ? 1.0 : 0.1);
        kb->setDefaultValue (cfgs[k].def);
        kb->setValue (cfgs[k].def, false);
        kb->setLabel (cfgs[k].label);
        kb->setValueSuffix (cfgs[k].suf);
        kb->setDecimals (cfgs[k].dec);
        kb->setKnobDiameter (52.0f);
        kb->setShowLabel (true);
        kb->setLinkedMark (true);                       // §5.2：⊞ 联动标识
        if (k != kLinkedMixIndex)
        {
            kb->setValuePrefix (juce::String::fromUTF8 ("\xCE\x94 "));   // §5.2：偏移读数带 Δ
            // R36（K6）：4 个偏移旋钮是**双极**量（0 = 不偏移），弧必须从 12 点钟
            //   起画。此前 setBipolar 全仓库从未被调用，于是 Δ=0 也画成半圈——
            //   看起来像"已经推了一半"，与"无偏移"的语义完全相反。
            kb->setBipolar (true);
            kb->setBipolarOrigin (0.0);
        }
        const int kk = k;
        kb->onValueChange = [this, kk] (double v) { applyOffset (kk, v); };
        addAndMakeVisible (kb);
        knobs_[(size_t) k].reset (kb);
    }
}

void LinkedDrawer::layoutKnobs (bool force)
{
    const auto panel = panelRect();
    const float unitW = panel.getWidth() / (float) kLinkedKnobs;
    const float knobW = unitW * (84.0f / 92.0f);
    // R36（B3）：旋钮区从标题栏下方开始（标题栏 = 32/148）。
    const float knobH = panel.getHeight() * (108.0f / 148.0f);
    for (int k = 0; k < kLinkedKnobs; ++k)
    {
        if (knobs_[(size_t) k] == nullptr)
            continue;
        // 旋钮顶部留出标签行（Knob 的标签画在旋钮上方），
        // 否则标签会突出到面板外、贴到绘图区上
        const float x = panel.getX() + unitW * (float) k + (unitW - knobW) * 0.5f;
        // R35（A 同款）：去掉纵向滑入，改为位置恒定 + 透明度跟随 anim_。
        // 原先这里也是 `+ (1 - anim) * panel.getHeight()`，把旋钮最多下移
        // 一整个面板高度，而面板只做 alpha 淡入 → 旋钮从面板外滑上来。
        const float y = panel.getY() + panel.getHeight() * (32.0f / 148.0f);
        const int nx = (int) x, ny = (int) y;
        auto* kb = knobs_[(size_t) k].get();
        // A3：只在位置变化超过 1px 时才 setBounds，避免每帧 5 次 resized()
        if (force || kb->getX() != nx || kb->getY() != ny
                 || kb->getWidth() != (int) knobW || kb->getHeight() != (int) knobH)
            kb->setBounds (nx, ny, (int) knobW, (int) knobH);
        kb->setAlpha (juce::jlimit (0.0f, 1.0f, anim_));
    }
}

// R35（N4）：MIX 是抽屉里**唯一绝对值**的旋钮（其余 4 个是 Δ 偏移）。
// 宿主自动化或 BYP 宏改过 Mix 后，这把旋钮的读数会停在旧值；
// 偏移旋钮按定义不需要回读（applyOffset 现场读参数再叠加）。
void LinkedDrawer::syncMixFromParams()
{
    auto& kb = knobs_[(size_t) kLinkedMixIndex];
    if (kb == nullptr)
        return;
    const double v = getParamPhysical ("band0Mix");
    if (std::abs (v - kb->getValue()) > 1.0e-6)
        kb->setValue (v, false);      // 不回告，避免与参数互相打架
}

void LinkedDrawer::syncKnobVisibility()
{
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->setVisible (open_ || anim_ > 0.001f);
}

void LinkedDrawer::tickEditCursors (juce::uint32 nowMs)
{
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->setEditCursorBlink (nowMs);
}

void LinkedDrawer::advanceKnobStates (double nowMs)
{
    // Phase C-2：四态渐变。旋钮自身没有 Timer（14 个旋钮各起一个太浪费），
    // 由宿主的 60fps 定时器统一推进。
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->advanceStates (nowMs);
}

void LinkedDrawer::applyOffset (int k, double newValue)
{
    // ---- Mix（k=4）：**绝对值语义**（§5.2 v1.6 例外）——四段 Mix 同设为该值 ----
    if (k == kLinkedMixIndex)
    {
        const double old = getParamPhysical ("band0Mix");
        if (std::abs (newValue - old) < 1.0e-9)
            return;
        for (int b = 0; b < Param::kNumBands; ++b)
            setParamPhysical ("band" + juce::String (b) + "Mix", (float) newValue);
        return;
    }

    // ---- 前 4 个旋钮：Δ 偏移语义 —— 四段同名参数线性偏移 ----
    const double delta = newValue - offset_[(size_t) k];
    if (std::abs (delta) < 1.0e-9)
    {
        offset_[(size_t) k] = newValue;
        return;
    }
    offset_[(size_t) k] = newValue;

    const juce::String name = (k == 0) ? "Threshold"
                           : (k == 1) ? "Attack"
                           : (k == 2) ? "Release" : "Makeup";
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const juce::String id = "band" + juce::String (b) + name;
        setParamPhysical (id, getParamPhysical (id) + (float) delta);   // 内部钳到参数范围
    }
}

float LinkedDrawer::getParamPhysical (const juce::String& id) const
{
    // Phase：走 facade（原为 proc_.getParameters() 线性扫描 + 两次 dynamic_cast）
    return proc_.getBackend().getParamFast (id);
}

void LinkedDrawer::setParamPhysical (const juce::String& id, float physical) const
{
    proc_.getBackend().setParam (id, physical);
}

void LinkedDrawer::paint (juce::Graphics& g)
{
    // ⚠️ R36（B3）：这里**不能**在收起时提前 return。
    //   旧代码是 `if (anim_ < 0.004f && ! open_) return;` —— 于是标签页本身
    //   在关闭状态下完全不绘制：底栏上方那条"LINKED CONTROL"根本看不见，
    //   用户只能凭记忆点一块空白（快照 01_main.png 里能直接看到这个问题）。
    //   现在只让**面板**跟 anim_ 淡入淡出，标签页恒常可见。
    const float a = juce::jlimit (0.0f, 1.0f, anim_);

    // ============================================================
    // R36（B3）：标签页与面板重做。
    //
    // 用户实测："LINKED CONTROL 条太丑了；点它面板浮动时应该有一个明显
    // 按下去的按钮样式，现在没有。"
    //
    // 旧实现：一个左右各带一段三次贝塞尔"斜切"的梯形，纯 panel 色 + 1px 描边，
    // 文字 left 对齐堆在中间，**开与不开长得一模一样**（只多一个 ▲ 字符）。
    // 现在：
    //   · 标签页 = 上圆角、底边贴住 global 条的"页签"形（不再斜切）；
    //   · 三态：NORMAL（track 底/次要文字）、HOVER（提亮）、ACTIVE（accent 实心，
    //     文字反白 —— 这就是"按下去"的观感）；
    //   · 左侧加联动图标（两个相交圆 + 连接线），右侧箭头随开合翻转；
    //   · 面板加标题栏：左边标题+说明，右边「锁定」按钮（带按下态）。
    // ============================================================
    const auto br = buttonRect();
    const float W = br.getWidth(), H = br.getHeight();
    const bool btnHot = br.contains (lastMousePos_);
    const bool btnOn  = open_ || sticky_;

    juce::Path tp;
    {
        const float r  = juce::jmin (W * 0.035f, H * 0.42f);   // 顶部圆角
        const float x0 = br.getX() + W * (10.0f / 456.0f);
        const float x1 = br.getRight() - W * (10.0f / 456.0f);
        const float y0 = br.getY() + H * (3.0f / 29.0f);
        const float y1 = br.getBottom() + 1.0f;                 // 与 global 条叠合 1px
        tp.startNewSubPath (x0, y1);
        tp.lineTo (x0, y0 + r);
        tp.quadraticTo (x0, y0, x0 + r, y0);
        tp.lineTo (x1 - r, y0);
        tp.quadraticTo (x1, y0, x1, y0 + r);
        tp.lineTo (x1, y1);
        tp.closeSubPath();
    }

    // 底色：未开=深色页签 / 悬停=提亮 / 打开=accent 实心（"按下去"）
    juce::Colour fill = pal_.panel.withAlpha (0.94f);
    if (btnOn)
        fill = pal_.accent.withAlpha (0.92f);
    else if (btnHot)
        fill = pal_.panel.brighter (0.10f).withAlpha (0.96f);
    g.setColour (fill);
    g.fillPath (tp);
    g.setColour (btnOn ? pal_.accent.brighter (0.35f) : pal_.gridZ);
    g.strokePath (tp, juce::PathStrokeType (1.2f));

    // ---- 左侧联动图标：两个相交圆 + 中间连接线（表示"四段联动"）----
    {
        const float s  = H * 0.44f;
        const float ix = br.getX() + W * (16.0f / 456.0f);
        const float iy = br.getY() + H * 0.5f - s * 0.5f;
        const auto col = btnOn ? juce::Colours::white.withAlpha (0.95f) : pal_.accent;
        g.setColour (col);
        const float lw = juce::jmax (1.2f, s * 0.14f);
        g.drawEllipse (ix, iy, s * 0.62f, s, lw);
        g.drawEllipse (ix + s * 0.38f, iy, s * 0.62f, s, lw);
        g.fillRect (ix + s * 0.30f, iy + s * 0.44f, s * 0.40f, lw);
    }

    // ---- 标题（左）+ 说明（中）+ 箭头（右）----
    const auto titleCol = btnOn ? juce::Colours::white.withAlpha (0.96f) : pal_.text;
    const auto hintCol  = btnOn ? juce::Colours::white.withAlpha (0.72f) : pal_.dim;
    g.setColour (titleCol);
    g.setFont (MBTheme::font (MBTheme::bold(), 11.5f));
    g.drawText ("LINKED CONTROL",
                juce::Rectangle<float> (br.getX() + W * (36.0f / 456.0f), br.getY(),
                                        W * (140.0f / 456.0f), H),
                juce::Justification::centredLeft);
    g.setColour (hintCol);
    g.setFont (MBTheme::font (MBTheme::regular(), 9.5f));
    g.drawText (juce::String::fromUTF8 ("\xE5\x9B\x9B\xE6\xAE\xB5\xE8\x81\x94\xE5\x8A\xA8 \xC2\xB7 \xE7\xBA\xBF\xE6\x80\xA7\xE5\x81\x8F\xE7\xA7\xBB"),
                juce::Rectangle<float> (br.getX() + W * (166.0f / 456.0f), br.getY(),
                                        W * (150.0f / 456.0f), H),
                juce::Justification::centredLeft);
    // 箭头：收起 ▲ / 展开 ▼
    {
        const float cx = br.getRight() - W * (18.0f / 456.0f);
        const float cy = br.getCentreY();
        const float w  = H * 0.30f, h = H * 0.17f;
        juce::Path ar;
        if (btnOn) { ar.startNewSubPath (cx - w, cy - h); ar.lineTo (cx + w, cy - h); ar.lineTo (cx, cy + h); }
        else       { ar.startNewSubPath (cx - w, cy + h); ar.lineTo (cx + w, cy + h); ar.lineTo (cx, cy - h); }
        ar.closeSubPath();
        g.setColour (btnOn ? juce::Colours::white.withAlpha (0.92f) : pal_.dim);
        g.fillPath (ar);
    }

    // ---- 面板（玻璃）+ 标题栏 ----
    if (a > 0.004f)
    {
        const auto panel = panelRect();
        const float corner = panel.getWidth() * (10.0f / 456.0f);
        g.setColour (pal_.popA.withMultipliedAlpha (a));
        g.fillRoundedRectangle (panel, corner);
        g.setColour (pal_.accent.withAlpha (0.55f * a));
        g.drawRoundedRectangle (panel, corner, 1.0f);

        // 标题栏：标题 + 说明 + 锁定按钮，底部一条 1px 分隔线
        const float hdrH = panel.getHeight() * (32.0f / 148.0f);
        auto hdr = panel.withHeight (hdrH);
        g.setColour (pal_.text.withMultipliedAlpha (a));
        g.setFont (MBTheme::font (MBTheme::semiBold(), 11.0f));
        g.drawText (juce::String::fromUTF8 ("\xE5\x9B\x9B\xE6\xAE\xB5\xE5\x90\x8C\xE6\xAD\xA5\xE8\xB0\x83\xE6\x95\xB4"),
                    hdr.withTrimmedLeft (panel.getWidth() * (14.0f / 460.0f))
                       .withTrimmedRight (panel.getWidth() * (60.0f / 460.0f)),
                    juce::Justification::centredLeft);
        g.setColour (pal_.grid.withMultipliedAlpha (a));
        g.fillRect (panel.getX() + panel.getWidth() * (14.0f / 460.0f),
                    hdr.getBottom() - 1.0f,
                    panel.getWidth() - panel.getWidth() * (28.0f / 460.0f), 1.0f);

        // ---- 锁定按钮（带按下态）----
        const auto pin = pinRect();
        const bool pinHot = pin.expanded (2.0f).contains (lastMousePos_);
        juce::Colour pinFill = juce::Colours::transparentBlack;
        juce::Colour pinGlyph = pal_.dim;
        if (sticky_)
        {
            pinFill  = pal_.accent.withAlpha (0.95f * a);       // 按下：实心 accent
            pinGlyph = juce::Colours::white.withAlpha (0.96f * a);
        }
        else if (pinHot)
        {
            pinFill  = pal_.track.withAlpha (0.85f * a);
            pinGlyph = pal_.text.withAlpha (a);
        }
        else
        {
            pinFill  = pal_.track.withAlpha (0.45f * a);
            pinGlyph = pal_.dim.withAlpha (a);
        }
        g.setColour (pinFill);
        g.fillRoundedRectangle (pin, pin.getWidth() * 0.26f);
        if (sticky_)
        {
            g.setColour (pal_.accent.brighter (0.40f).withAlpha (0.9f * a));
            g.drawRoundedRectangle (pin, pin.getWidth() * 0.26f, 1.0f);
        }

        // 锁形图标：锁体（圆角矩形）+ 锁梁（半圆）。未锁时锁梁开口向右上。
        {
            const float s  = pin.getWidth();
            const float bw = s * 0.46f, bh = s * 0.36f;
            const float bx = pin.getCentreX() - bw * 0.5f;
            const float by = pin.getCentreY() - bh * 0.5f + s * 0.07f;
            g.setColour (pinGlyph);
            g.fillRoundedRectangle (bx, by, bw, bh, s * 0.06f);
            const float sw = s * 0.10f;
            juce::Path shackle;
            const float ar = s * 0.19f;
            const float shx = pin.getCentreX();
            const float shy = by;
            if (sticky_)
            {
                // 闭锁：完整半圆
                shackle.addCentredArc (shx, shy, ar, ar, 0.0f,
                                       -juce::MathConstants<float>::halfPi,
                                        juce::MathConstants<float>::halfPi, true);
            }
            else
            {
                // 开锁：右半边抬起，留一个缺口
                shackle.addCentredArc (shx, shy, ar, ar, 0.0f,
                                       -juce::MathConstants<float>::halfPi,
                                        juce::MathConstants<float>::halfPi * 0.35f, true);
            }
            g.strokePath (shackle, juce::PathStrokeType (sw, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        // 「锁定」文字（按钮左侧，仅 sticky 时高亮）
        g.setColour (sticky_ ? pal_.accent.withAlpha (a) : pal_.dim.withAlpha (0.85f * a));
        g.setFont (MBTheme::font (MBTheme::regular(), 9.0f));
        g.drawText (sticky_ ? juce::String::fromUTF8 ("\xE5\xB7\xB2\xE9\x94\x81\xE5\xAE\x9A")
                            : juce::String::fromUTF8 ("\xE9\x94\x81\xE5\xAE\x9A"),
                    juce::Rectangle<float> (pin.getX() - panel.getWidth() * (46.0f / 460.0f),
                                            pin.getY(), panel.getWidth() * (42.0f / 460.0f),
                                            pin.getHeight()),
                    juce::Justification::centredRight);
    }
}

void LinkedDrawer::resized()
{
    layoutKnobs (true);
}
