#include "Knob.h"
#include "Anim.h"

// ---------------- 配置 ----------------
void Knob::setRange (double minValue, double maxValue, double interval)
{
    min_ = minValue; max_ = std::max (minValue, maxValue); interval_ = interval;
    value_ = juce::jlimit (min_, max_, value_);
    default_ = juce::jlimit (min_, max_, default_);
    updateSkew();                          // R36：范围变了要按新的 span 重算指数
    repaint();
}

// ---------------- R36：刻度映射（与参数的 NormalisableRange 同式） ----------------
// JUCE 的 setSkewForCentre(centre) 令 skew = ln(0.5)/ln((centre-min)/span)，
// 归一化 = pow(prop, skew)、物理值 = min + pow(n, 1/skew)·span。
// 这里原样复算，确保同一个物理值在"面板旋钮"和"宿主自动化车道"上是同一个位置。
void Knob::updateSkew()
{
    skew_    = 1.0f;
    hasSkew_ = false;
    if (! skewSet_)
        return;
    const double span = max_ - min_;
    const double c    = (span > 0.0) ? (skewCentre_ - min_) / span : 0.5;
    if (c > 0.0 && c < 1.0)
    {
        skew_    = (float) (std::log (0.5) / std::log (c));
        hasSkew_ = std::abs (skew_ - 1.0f) > 1.0e-4f;
    }
}

float Knob::toNorm (double v) const
{
    const double span = max_ - min_;
    if (span <= 0.0)
        return 0.0f;
    double prop = (v - min_) / span;
    prop = juce::jlimit (0.0, 1.0, prop);
    if (! hasSkew_)
        return (float) prop;
    return (float) std::pow (prop, (double) skew_);
}

double Knob::fromNorm (double n) const
{
    const double span = max_ - min_;
    n = juce::jlimit (0.0, 1.0, n);
    if (! hasSkew_)
        return min_ + n * span;
    return min_ + std::pow (n, 1.0 / (double) skew_) * span;
}

void Knob::setValue (double v, bool sendNotification)
{
    const double nv = juce::jlimit (min_, max_, v);
    if (std::abs (nv - value_) < 1.0e-9) return;
    value_ = nv;
    repaint();
    if (sendNotification && onValueChange) onValueChange (value_);
}

// ---------------- 几何 ----------------
juce::Rectangle<float> Knob::knobBounds() const
{
    const float w = (float) getWidth();
    const float d = juce::jmin (knobDiameter_, w);
    const float cy = showLabel_ ? knobDiameter_ * 0.5f + 1.0f
                                : (float) getHeight() * 0.5f - 4.0f;
    return juce::Rectangle<float> (w * 0.5f - d * 0.5f, cy, d, d);
}

// 角度约定统一为 JUCE addCentredArc 的"12 点钟起、顺时针"：
//   point = (cx + r·sin(θ), cy − r·cos(θ))
// 起点（最小）= 7:30 方向，终点（最大）= 4:30 方向，行程 270°，缺口在正下方。
//
// ⚠️ R36（用户实测"旋钮可以旋转一圈多，看不出指针位置"）：
//   这里原写 `end = pi * 3.75`（675°），而起点是 `pi * 1.25`（225°），
//   行程 = 450° —— 正好一圈（360°）再加 90°。于是：
//     · 轨道弧自己盖住自己一整圈，刻度环的 11 根刻度绕回起点附近；
//     · 归一化 0/1（最小/最大）在盘面上正好落在**同一方向**，指针转到头
//       和转到尾看起来一样，用户无法从指针读出位置；
//     · 两端读数在几何上重叠，中间值反而更"靠边"。
//   Pro-MB 的指针是 θ = −135° + 270°·value，固定 270° 行程（底部 90° 缺口）。
//   正确终点 = 225° + 270° = 495° = pi * 2.75。
float Knob::valueToAngle (double v) const
{
    const float norm = toNorm (v);                                   // R36：含 skew
    const float start = juce::MathConstants<float>::pi * 1.25f;      // 7:30 方向（= −135°）
    const float end   = juce::MathConstants<float>::pi * 2.75f;      // 4:30 方向（= +135°）
    return start + juce::jlimit (0.0f, 1.0f, norm) * (end - start);  // 270° 行程
}

// ---------------- 交互 ----------------
void Knob::commitValue (double v)
{
    if (interval_ > 0.0) v = std::round (v / interval_) * interval_;
    setValue (v, true);
}

void Knob::commitValueFine (double v)
{
    // Phase：细调提交——**跳过 interval 量化**。
    // 否则 Shift 细调在小 interval 参数上完全无效：step 0.1 会被
    // round(v/1.0)*1.0 量化回 0，用户按 Shift 却毫无变化。
    setValue (v, true);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (editing_) { closeEditor (true); return; }
    if(selectionFeedback_){grabKeyboardFocus();repaint();}

    dragStartValue_ = value_;
    dragStartNorm_  = (double) toNorm (value_);      // R36：按归一化行程拖动
    dragging_ = true;
    mouseDownHeld_ = true;          // Phase C-2：DOWN 态
    slowAccum_ = 0.0f;
    shiftFine_ = e.mods.isShiftDown();

    // Ctrl+click = 复位（FabFilter 官方：Ctrl/Cmd + click resets to default）
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        setValue (default_, true);
        dragging_ = false;
        mouseDownHeld_ = false;
        return;
    }
    if (onDragStart) onDragStart();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_) return;

    const double span = max_ - min_;
    if (span <= 0.0) return;

    // 垂直模式：上拖=增大。220px 走完全程；Shift 微调 ×0.1。
    // R36：行程按**归一化**算（含 skew），否则 Attack/Release 这类对数参数
    //      低端极敏感、高端拖不动——同一个 220px 在全行程上的手感必须一致。
    const float dy = (float) (e.getMouseDownY() - e.getPosition().getY());
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;

    const double newNorm = dragStartNorm_ + (double) dy / 220.0 * (double) fine;
    commitValue (fromNorm (newNorm));
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (dragging_ && onDragEnd) onDragEnd();
    dragging_ = false;
    mouseDownHeld_ = false;         // Phase C-2：DOWN 态解除
    slowAccum_ = 0.0f;
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    openEditor();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (editing_) return;

    // R36：有 skew 的旋钮按**归一化**步进，否则 1 个 interval 在低端等于跳掉半个行程。
    if (hasSkew_)
    {
        double dn = (wheel.deltaY < 0) ? -0.02 : 0.02;
        if (e.mods.isShiftDown()) dn *= 0.1;
        if (e.mods.isCtrlDown() || e.mods.isCommandDown()) dn *= 5.0;
        setValue (fromNorm ((double) toNorm (value_) + dn), true);
        return;
    }

    const double span = max_ - min_;
    double step = (interval_ > 0.0) ? interval_ : span * 0.02;
    if (wheel.deltaY < 0) step = -step;

    // Phase：Shift = 细调（与 mouseDrag 的 shiftFine_ 语义一致）。
    // 此前滚轮没有细调，而拖动有——交互不一致：用户拖着能精调，
    // 滚轮却一跳一个 interval。Pro-MB / Future MB 的滚轮都支持 Shift 细调。
    if (e.mods.isShiftDown())
    {
        commitValueFine (value_ + step * 0.1);
        return;
    }

    // Ctrl/Cmd = 加速（大步进，用于快速跨量程）
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        step *= 5.0;

    commitValue (value_ + step);
}

// ---------------- Phase C-2：四态 ----------------
void Knob::mouseEnter (const juce::MouseEvent&) { hovered_ = true; }
void Knob::mouseExit  (const juce::MouseEvent&) { hovered_ = false; }

void Knob::advanceStates (double nowMs)
{
    // 三个维度各自独立 Tween，从"当前显示值"起跳，目标变化不打断。
    const double ms = Anim::kKnobMs;
    tweenTo (hoverT_,  nowMs, hovered_        ? 1.0f : 0.0f, ms);
    tweenTo (downT_,   nowMs, mouseDownHeld_  ? 1.0f : 0.0f, ms);
    tweenTo (rotateT_, nowMs, dragging_       ? 1.0f : 0.0f, ms);

    // R36：把"还在动"上报给宿主，否则空闲降频会把 120ms 渐变切成跳变。
    animating_ = ! (hoverT_.done (nowMs) && downT_.done (nowMs) && rotateT_.done (nowMs));

    // R36：paint() 改用**宿主注入的时钟**，不再自取 juce::Time 实时钟。
    //   原先 paint 读实时钟、advance 读宿主钟，两套时基在测试虚拟时钟下分叉，
    //   实际运行也只是"恰好同源"，属于隐患不是保障。
    lastNowMs_ = nowMs;
}

// R36 测试钩子：几何量必须与状态无关（Pro-MB 只有亮度/色相变化）。
float Knob::arcThicknessForTest() const
{
    const float d = juce::jmin (knobDiameter_, (float) getWidth());
    return juce::jmax (2.0f, d * 0.10f);
}

float Knob::arcRadiusForTest() const
{
    const float d = juce::jmin (knobDiameter_, (float) getWidth());
    return d * 0.5f - arcThicknessForTest() * 0.5f;
}

float Knob::capRadiusForTest() const
{
    const float d = juce::jmin (knobDiameter_, (float) getWidth());
    return d * 0.5f - arcThicknessForTest() - juce::jmax (1.0f, d * 0.05f);
}

// ---------------- 内嵌输入 ----------------
void Knob::openEditor()
{
    editing_ = true;
    editText_.clear();
    editCursorOn_ = true;
    editCursorLastToggleMs_ = juce::Time::getMillisecondCounter();
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
}

// A2 修复：光标闪烁由时间驱动（半周期 Anim::kBlinkMs），与重绘率解耦。
void Knob::setEditCursorBlink (juce::uint32 nowMs)
{
    if (! editing_)
        return;
    if (editCursorLastToggleMs_ == 0)
        editCursorLastToggleMs_ = nowMs;
    if ((int) (nowMs - editCursorLastToggleMs_) >= (int) Anim::kBlinkMs)
    {
        editCursorOn_ = ! editCursorOn_;
        editCursorLastToggleMs_ = nowMs;
        repaint();
    }
}

void Knob::closeEditor (bool apply)
{
    if (! editing_) return;
    editing_ = false;
    if (apply && editText_.isNotEmpty())
    {
        double v = editText_.getDoubleValue();
        // 支持 "50%" 形式
        if (editText_.containsChar ('%') && suffix_ != "%")
            v = min_ + (max_ - min_) * editText_.getDoubleValue() / 100.0;
        if(onDragStart)onDragStart();
        commitValue (v);
        if(onDragEnd)onDragEnd();
    }
    editText_.clear();
    repaint();
}

bool Knob::keyPressed (const juce::KeyPress& key)
{
    if (editing_) return keyPressedEditing (key);

    // Phase（R18 修复）：非编辑态的方向键步进。
    // 此前 keyPressed 开头就是 `if (! editing_) return false;`——
    // 旋钮在正常态下**完全无键盘入口**，只能鼠标拖动/滚轮。
    // Pro-MB / Future MB 的旋钮均支持 ↑/↓ 步进与 PgUp/PgDn 大步进。
    const auto kc = key.getKeyCode();
    const double span = max_ - min_;
    const double coarse = (interval_ > 0.0) ? interval_ * 10.0 : span * 0.10;
    const double fine   = (interval_ > 0.0) ? interval_        : span * 0.01;

    if (kc == juce::KeyPress::upKey || kc == juce::KeyPress::rightKey)
    {
        const double step = key.getModifiers().isShiftDown() ? fine : coarse;
        setValue (juce::jlimit (min_, max_, value_ + step), true);
        return true;
    }
    if (kc == juce::KeyPress::downKey || kc == juce::KeyPress::leftKey)
    {
        const double step = key.getModifiers().isShiftDown() ? fine : coarse;
        setValue (juce::jlimit (min_, max_, value_ - step), true);
        return true;
    }
    if (kc == juce::KeyPress::pageUpKey)
    {
        setValue (juce::jlimit (min_, max_, value_ + coarse * 2.0), true);
        return true;
    }
    if (kc == juce::KeyPress::pageDownKey)
    {
        setValue (juce::jlimit (min_, max_, value_ - coarse * 2.0), true);
        return true;
    }
    if (kc == juce::KeyPress::homeKey)
    {
        setValue (min_, true);
        return true;
    }
    if (kc == juce::KeyPress::endKey)
    {
        setValue (max_, true);
        return true;
    }

    // Enter / 空格 / 双击同等语义：进入文本编辑（精确输入）
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        openEditor();
        return true;
    }

    // Backspace / Delete = 复位到默认值（与 Ctrl+click 同义）
    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
    {
        setValue (default_, true);
        return true;
    }

    return false;
}

bool Knob::keyPressedEditing (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)  { closeEditor (false); return true; }
    if (key == juce::KeyPress::returnKey)  { closeEditor (true);  return true; }
    if (key == juce::KeyPress::backspaceKey)
    { editText_ = editText_.substring (0, juce::jmax (0, editText_.length() - 1)); repaint(); return true; }

    const juce::juce_wchar ch = key.getTextCharacter();
    // 某些构造路径（如 KeyPress('7')）只设 keyCode 不设 textCharacter，
    // 此时 getTextCharacter() 返回 0。回退到 keyCode 兜底，保证真实键盘
    // 与程序化构造都能输入。
    const juce::juce_wchar c = (ch != 0) ? ch : (juce::juce_wchar) key.getKeyCode();
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '%')
    {
        // 限制输入长度：无上限时用户可无限键入，getDoubleValue() 对超长
        // 字符串行为未定义（且视觉上会溢出旋钮）。
        if (editText_.length() < 12)
            editText_ += juce::String::charToString (c);
        repaint(); return true;
    }
    return false;
}

// ---------------- Phase C-1：BACK 层缓存 ----------------
// 外圈刻度环 + 深色圆体底。与当前值无关 → 只在直径/主题变化时重建。
// 对齐 Pro-MB 的 KNOBLARGEBACK*（预渲染刻度环位图）：刻度环底部留缺口，
// 与 270° 行程一致；环在值弧**外侧**。
void Knob::buildBackLayer (float d)
{
    // +10px 余量容纳外圈刻度与新拟物双阴影羽化
    const int pad = themeTokens_.isNeumorphic ? 12 : 4;
    const int px  = juce::roundToInt (d) + pad;
    backLayer_ = juce::Image (juce::Image::ARGB, px, px, true);
    juce::Graphics g (backLayer_);

    const float cx = px * 0.5f, cy = px * 0.5f;
    const float r  = d * 0.5f;
    const float a0 = valueToAngle (min_);
    const float a1 = valueToAngle (max_);

    // ---- 外圈刻度环（11 根短刻度，跨 270°，底部留缺口）----
    const int   nTicks = 11;
    const float tickInner = r + juce::jmax (1.5f, d * 0.035f);
    const float tickOuter = r + juce::jmax (3.5f, d * 0.075f);
    g.setColour (themeTokens_.isNeumorphic ? pal_.dim.withAlpha (0.55f) : pal_.gridZ);
    for (int i = 0; i < nTicks; ++i)
    {
        const float t = (float) i / (float) (nTicks - 1);
        const float a = a0 + t * (a1 - a0);
        const float x1 = cx + std::sin (a) * tickInner;
        const float y1 = cy - std::cos (a) * tickInner;
        const float x2 = cx + std::sin (a) * tickOuter;
        const float y2 = cy - std::cos (a) * tickOuter;
        g.drawLine (x1, y1, x2, y2, juce::jmax (1.0f, d * 0.018f));
    }

    const float bodyR = r - juce::jmax (1.0f, d * 0.02f);
    const juce::Rectangle<float> bodyBox (cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    if (themeTokens_.isNeumorphic)
    {
        // 新拟物：外环底座为柔和内凹圆槽（insetCircle）
        UiPack::NeuPainter::drawInsetCircle (g, bodyBox, pal_, themeTokens_.insetButton, d / 72.0f);
    }
    else
    {
        juce::ColourGradient bodyGrad (pal_.track.brighter (0.10f), cx, cy - bodyR,
                                       pal_.track.darker (0.25f), cx, cy + bodyR, false);
        g.setGradientFill (bodyGrad);
        g.fillEllipse (bodyBox);
    }
}

// ---------------- 绘制 ----------------
void Knob::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const auto kb = knobBounds();
    const float d = kb.getWidth();
    const float cx = kb.getCentreX();
    const float cy = kb.getCentreY();
    const float r  = d * 0.5f;

    // ---- 标签（旋钮上方）----
    if (showLabel_ && label_.isNotEmpty())
    {
        // R36（B4）：字号倍率放大后标签行也要跟着变高，否则文字被裁。
        const auto labFont = MBTheme::font (MBTheme::uiMedium(), labelTextSize_>0?labelTextSize_:d * (14.0f / 72.0f));
        const float labH = labFont.getHeight() + 1.0f;
        g.setFont (labFont);
        g.setColour (pal_.dim);
        auto labelBox = juce::Rectangle<float> (0.0f, kb.getY() - juce::jmax (labH + 3.0f, juce::jmin (18.0f, d * 0.36f)),
                                                w, labH);
        g.drawText (label_, labelBox, juce::Justification::centredTop);

        // §5.2：⊞ 联动标识（标签右侧小方块+加号，表示作用于四段）
        if (linkedMark_)
        {
            const float bw = juce::jmin (8.0f, d * 0.17f);
            const float bx = labelBox.getRight() - bw - 2.0f;
            const float by = labelBox.getY() + 3.0f;
            g.setColour (pal_.accent.withAlpha (0.85f));
            g.drawRoundedRectangle (bx, by, bw, bw, 2.0f, 1.0f);
            g.fillRect (bx + bw * 0.2f, by + bw * 0.45f, bw * 0.6f, juce::jmax (1.0f, bw * 0.1f));
            g.fillRect (bx + bw * 0.45f, by + bw * 0.2f, juce::jmax (1.0f, bw * 0.1f), bw * 0.6f);
        }
    }

    // ---- Phase C-1：BACK 层（刻度环 + 圆体底），缓存 Image ----
    // 直径变了才重建（主题变化经 setPalette/setKnobDiameter 失效缓存）。
    // 否则直接贴图，省掉每帧 11 根刻度 + 渐变填充。
    if (backLayerDiameter_ != d)
    {
        buildBackLayer (d);
        backLayerDiameter_ = d;
    }
    g.drawImageAt (backLayer_, (int) (cx - backLayer_.getWidth() * 0.5f),
                                  (int) (cy - backLayer_.getHeight() * 0.5f));

    // ---- Phase C-2：四态合成（Pro-MB Overlay{NORMAL,ACTIVE,DOWN,ROTATE}）----
    // ⚠️ R36 重做。旧实现有两个毛病（用户实测"旋钮动效不合理"）：
    //   1) `glow = max(hover, down, rotate)` 把 DOWN 和 ACTIVE 压成同一个亮度，
    //      而帽的提亮又是 `0.25*hover + 0.15*down`——按下时环一样亮、帽反而更暗，
    //      自相矛盾，按下去没有"压下去"的观感；
    //   2) glow 直接乘进 thick / 弧半径 / capR，于是悬停会让环变粗 35%、
    //      帽半径缩 10%、指针缩短——旋钮在"呼吸"。Pro-MB 的 BACK 层只换
    //      NORMAL/ACTIVE 两张纹理（亮度差），几何在任何状态下**不动**。
    //
    // 现在：几何只由直径决定（恒量），状态只改颜色。
    //   优先级 ROTATE（暖）> DOWN（压暗）> ACTIVE（提亮）> NORMAL
    const double nowMs = lastNowMs_;                 // R36：宿主注入的时钟
    const float hoverK  = hoverT_.value (nowMs);
    const float downK   = downT_.value (nowMs);
    const float rotateK = rotateT_.value (nowMs);
    // 按下时 ACTIVE 让位给 DOWN（不是叠加），拖动时再让位给 ROTATE
    const float activeK = juce::jmax (0.0f, hoverK * (1.0f - downK));
    const float pressK  = downK * (1.0f - rotateK);

    // ---- OVERLAY：值弧（轨道 + 值）----
    const float a0 = valueToAngle (min_);
    const float a1 = valueToAngle (max_);
    const float av = valueToAngle (value_);
    // R36：弧粗恒定（几何冻结）
    const float thick = juce::jmax (2.0f, d * 0.10f);
    const float arcR  = r - thick * 0.5f;

    juce::Path track;
    track.addCentredArc (cx, cy, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (pal_.track.withAlpha (0.55f));   // BACK 已有圆体，轨道淡一点避免叠重
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path val;
    // R36：双极弧的原点是**零点**（bipolarOrigin_），不是默认值。
    //   旧代码用 `valueToAngle (default_)`，Range 默认 −6 dB 时弧从 −6 起画，
    //   而且 setBipolar 全仓库从未被调用 → 整段是死代码，所有旋钮都从最小值起画。
    const float from = bipolar_ ? valueToAngle (bipolarOrigin_) : a0;
    // R36：双极旋钮在零点处画一根短刻线 —— 否则值为 0 时弧长为零，
    //   旋钮看起来"什么都没有"，用户读不出零点在哪一侧。
    if (bipolar_)
    {
        const float tickR0 = arcR - thick * 0.75f;
        const float tickR1 = arcR + thick * 0.75f;
        g.setColour (pal_.dim.withAlpha (0.85f));
        g.drawLine (cx + std::sin (from) * tickR0, cy - std::cos (from) * tickR0,
                    cx + std::sin (from) * tickR1, cy - std::cos (from) * tickR1,
                    juce::jmax (1.0f, d * 0.02f));
    }
    val.addCentredArc (cx, cy, arcR, arcR, 0.0f,
                       juce::jmin (from, av), juce::jmax (from, av), true);
    // 弧色：NORMAL → 悬停提亮 → 拖动转"暖色"（Pro-MB OverlayRotate 的暖辉光）
    // R36：暖色固定为金色，而不是主题 accent 的提亮版——否则 Blue 主题下
    //   ROTATE 只是"更亮的蓝"，和 ACTIVE 分不开（快照 06 里两者几乎一样）。
    const juce::Colour warmCol = pal_.accent4;
    juce::Colour arcCol = pal_.accent.brighter (0.10f * activeK);
    if (pressK > 0.001f)
        arcCol = arcCol.darker (0.32f * pressK);
    if (rotateK > 0.001f)
        arcCol = arcCol.interpolatedWith (warmCol.brighter (0.10f), rotateK * 0.85f);
    g.setColour (arcCol);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // ---- 帽 + 指针 ----
    const float capR = r - thick - juce::jmax (1.0f, d * 0.05f);   // R36：恒定
    const juce::Rectangle<float> capBox (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);
    if (themeTokens_.isNeumorphic)
    {
        // 新拟物 neu-circle 旋钮帽物理：
        //   · NORMAL -> 外凸双阴影 shadowButton
        //   · ACTIVE -> Hover Shadowing（外阴影向 shadowHover 收缩，零位移）
        //   · DOWN   -> Extrude to Intrude（转为内凹 insetButton）
        const float ks = juce::jmax (0.65f, d / 72.0f);
        if (pressK > 0.45f)
        {
            UiPack::NeuPainter::drawInsetCircle (g, capBox, pal_, themeTokens_.insetButton, ks);
        }
        else
        {
            UiPack::DualShadowSpec spec = themeTokens_.shadowButton;
            spec.offset = spec.offset + (themeTokens_.shadowHover.offset - spec.offset) * activeK;
            spec.blur   = spec.blur   + (themeTokens_.shadowHover.blur   - spec.blur)   * activeK;
            UiPack::NeuPainter::drawRaisedCircle (g, capBox, pal_, spec, ks);
        }
    }
    else
    {
        juce::Colour capCol = pal_.knob;
        capCol = capCol.brighter (0.16f * activeK);                    // ACTIVE：提亮
        capCol = capCol.darker   (0.26f * pressK);                     // DOWN：压暗（要能与 ACTIVE 分辨）
        if (rotateK > 0.001f)
            capCol = capCol.interpolatedWith (pal_.accent4.interpolatedWith (pal_.knob, 0.35f), rotateK * 0.62f);
        juce::ColourGradient grad (capCol.brighter (0.12f), cx, cy - capR,
                                   capCol.darker (0.15f), cx, cy + capR, false);
        g.setGradientFill (grad);
        g.fillEllipse (capBox);
    }

    const float ang = av;
    // 指针：与弧同一角度约定 —— (cx + R·sin(ang), cy − R·cos(ang))
    // 严格收在内部帽圆 capR 边界之内（绝不越界压到外环），且仅在靠圆形边界的外端添加平滑圆角
    const float pw    = juce::jmax (1.8f, d * 0.07f);
    const float halfW = pw * 0.5f;
    const float pR0   = capR * 0.24f;
    const float pR1   = themeTokens_.isNeumorphic
                            ? (capR * 0.86f)
                            : juce::jmin (capR * 0.86f, capR - juce::jmax (1.5f, d * 0.035f));
    if (themeTokens_.isNeumorphic)
        g.setColour (rotateK > 0.05f ? pal_.accent : pal_.text.withAlpha (0.85f));
    else
        g.setColour (pal_.knob.contrasting (1.0f).withAlpha (pal_.light ? 0.75f : 0.80f));

    if (pR1 > pR0 + 0.5f)
    {
        juce::Path needle;
        needle.addRoundedRectangle (-halfW, -pR1, pw, pR1 - pR0,
                                    halfW, halfW,
                                    true,  true,     // 靠内部圆外边界的一端：添加平滑圆角
                                    false, false);   // 靠圆心内侧的一端：保持利落平直
        needle.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.fillPath (needle);
    }

    // ---- 数值读数（旋钮下方）----§5.2：偏移旋钮带 "Δ " 前缀
    juce::String txt = valuePrefix_ + juce::String (value_, decimals_);
    if (suffix_.isNotEmpty()) txt << " " << suffix_;
    const auto valFont = MBTheme::font (MBTheme::regular(), valueTextSize_>0?valueTextSize_:d * (15.0f / 72.0f));
    if(selectionFeedback_ && hasKeyboardFocus(true)){
        const auto box=juce::Rectangle<float>(3,kb.getBottom(),w-6,valFont.getHeight()+5);
        if(themeTokens_.isNeumorphic)
            UiPack::NeuPainter::drawInsetRect(g, box, 5.0f, pal_, themeTokens_.insetFocus, 0.85f, 1.0f, true);
        else {
            g.setColour(pal_.accent.withAlpha(.15f));g.fillRoundedRectangle(box,4);
            g.setColour(pal_.accent);g.drawRoundedRectangle(box,4,1);
        }
    }
    g.setFont (valFont);
    g.setColour (editing_ ? pal_.accent : pal_.text);
    g.drawText (txt, juce::Rectangle<float> (0.0f, kb.getBottom() + 1.0f, w,
                                             valFont.getHeight() + 2.0f),
                juce::Justification::centredTop);

    // ---- 编辑框（自绘内嵌输入）----
    if (editing_)
    {
        const float bw = juce::jmax (w, 78.0f), bh = 22.0f;
        auto box = juce::Rectangle<float> (w * 0.5f - bw * 0.5f, cy - bh * 0.5f, bw, bh);
        if (themeTokens_.isNeumorphic)
        {
            UiPack::NeuPainter::drawInsetRect (g, box, 6.0f, pal_, themeTokens_.insetFocus, 1.0f, 1.0f, true);
            g.setColour (pal_.accent.withAlpha (0.5f));
            g.drawRoundedRectangle (box, 6.0f, 1.0f);
        }
        else
        {
            g.setColour (pal_.surface.withAlpha (0.97f));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (pal_.accent);
            g.drawRoundedRectangle (box, 5.0f, 1.2f);
        }
        g.setColour (pal_.text);
        g.setFont (MBTheme::font (MBTheme::regular(), valueTextSize_>0?valueTextSize_:12.0f));
        // A2 修复：paint() 只读光标状态，不再翻转（翻转已移到 setEditCursorBlink）
        g.drawText (editText_ + (editCursorOn_ ? "|" : " "), box, juce::Justification::centred);
    }
}

void Knob::resized() {}
