#include "MBBackend.h"

// 头文件里只前向声明 CrunchMBAudioProcessor（避免与 PluginProcessor.h 循环包含），
// 完整类型在这里引入。
#include "../PluginProcessor.h"

// ============================================================
// MBBackend 实现
// ============================================================

namespace
{
    constexpr const char* kPluginVersion = "1.0.0";

    /** 参数类别判定：band{i}... 前缀 → Band；其余 → Global。 */
    ParamInfo::Category categoryOf (const juce::String& id, int& bandIndexOut)
    {
        bandIndexOut = -1;
        if (id.startsWith ("band") && id.length() > 4 && juce::CharacterFunctions::isDigit (id[4]))
        {
            const int b = id.substring (4, 5).getIntValue();
            if (b >= 0 && b < Param::kNumBands)
            {
                bandIndexOut = b;
                return ParamInfo::Category::Band;
            }
        }
        return ParamInfo::Category::Global;
    }
}

// ---------------- 参数面 ----------------

void MBBackend::buildParamCache()
{
    paramCache_.clear();
    for (auto* p : proc_.apvts.processor.getParameters())
        if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            paramCache_[wid->paramID] = p;
    paramCacheBuilt_ = true;
}

void MBBackend::setParamNormFast (const juce::String& id, float norm01)
{
    const float n = juce::jlimit (0.0f, 1.0f, norm01);
    if (paramCacheBuilt_)
    {
        const auto it = paramCache_.find (id);
        if (it != paramCache_.end() && it->second != nullptr)
        {
            it->second->setValueNotifyingHost (n);
            return;
        }
    }
    if (auto* p = proc_.apvts.getParameter (id))
        p->setValueNotifyingHost (n);
}

float MBBackend::getParamFast (const juce::String& id) const
{
    if (! paramCacheBuilt_)
        return const_cast<MBBackend*> (this)->getParam (id).value;

    const auto it = paramCache_.find (id);
    if (it == paramCache_.end() || it->second == nullptr)
        return const_cast<MBBackend*> (this)->getParam (id).value;

    juce::AudioProcessorParameter* p = it->second;
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        return rp->getNormalisableRange().convertFrom0to1 (p->getValue());
    return p->getValue();
}

float MBBackend::getParamNorm (const juce::String& id) const
{
    // Phase（R19）：与 getParamFast 同路径，只是返回 normalized 本身。
    // 之前 UI 层各自实现这个换算，BandPopover 写对了、DisplayView 写错了
    // （直接 return getParamFast）——同一换算写两遍必然一处漂移。
    if (! paramCacheBuilt_)
        return normalizedOf (id);

    const auto it = paramCache_.find (id);
    if (it == paramCache_.end() || it->second == nullptr)
        return normalizedOf (id);

    return juce::jlimit (0.0f, 1.0f, it->second->getValue());
}

float MBBackend::normalizedOf (const juce::String& id) const
{
    if (auto* p = proc_.apvts.getParameter (id))
        return juce::jlimit (0.0f, 1.0f, p->getValue());
    return 0.0f;
}

ParamInfo MBBackend::describeParam (juce::AudioProcessorParameter* p)
{
    ParamInfo info;
    if (p == nullptr)
        return info;      // 全零/空值，调用方判 id 是否为空

    // ⚠️ 不能直接用 p->getName()——AudioProcessorParameterWithID::getName()
    // 返回的是**显示名**（如 "Low Threshold"），不是参数 ID。取 ID 必须走
    // paramID 成员。（曾因此把全部参数误判为 Global 类别。）
    juce::String pid;
    if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        pid = wid->paramID;
    else
        pid = p->getName (1024);

    info.id          = pid;
    info.displayName = p->getName (1024);
    info.isAutomatable = p->isAutomatable();
    info.isChoice    = false;
    info.choices.clear();

    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (p))
    {
        info.isChoice  = true;
        info.choices   = ch->choices;
        info.unit      = {};
        info.minValue  = 0.0f;
        info.maxValue  = (float) (ch->choices.size() - 1);
        // getDefaultValue 是 private；用 normalized 默认值反推索引
        const float defNorm = p->getDefaultValue();
        const int   defIdx  = juce::roundToInt (defNorm * (float) (ch->choices.size() - 1));
        info.defaultValue = (float) juce::jlimit (0, ch->choices.size() - 1, defIdx);
    }
    else if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
    {
        const auto& r = rp->getNormalisableRange();
        info.minValue    = r.start;
        info.maxValue    = r.end;
        info.defaultValue = r.convertFrom0to1 (p->getDefaultValue());
        info.unit        = p->getLabel();
    }
    else
    {
        // Bool 等其他类型：0..1
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = p->getDefaultValue();
        info.unit = p->getLabel();
    }

    info.category  = categoryOf (info.id, info.bandIndex);
    return info;
}

std::vector<ParamInfo> MBBackend::listParams()
{
    std::vector<ParamInfo> out;
    const auto& params = proc_.apvts.processor.getParameters();
    out.reserve ((size_t) params.size());
    for (auto* p : params)
        out.push_back (describeParam (p));
    return out;
}

ParamValue MBBackend::getParam (const juce::String& id)
{
    ParamValue v;
    v.id = id;
    v.value = 0.0f;
    v.isValid = false;

    if (auto* p = proc_.apvts.getParameter (id))
    {
        // ⚠️ 必须是**实际值**（规范 §2："读取单个参数当前值（实际值）"）。
        //    两条路都不对：
        //      · p->getValue()             → normalized（0..1），违反规范
        //      · getRawParameterValue(id)   → 名字像 raw normalized，实际返回
        //                                  denormalisedValue（JUCE 命名陷阱）
        //    正确做法：经 RangedAudioParameter 把 normalized 转成实际值。
        //    Choice/Bool 参数的"实际值"取当前索引 / 0..1。
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            v.value = rp->getNormalisableRange().convertFrom0to1 (p->getValue());
        else
            v.value = p->getValue();
        v.isValid = true;
    }
    return v;
}

std::vector<ParamValue> MBBackend::getParams (const std::vector<juce::String>& ids)
{
    std::vector<ParamValue> out;
    out.reserve (ids.size());
    for (const auto& id : ids)
        out.push_back (getParam (id));
    return out;
}

SetResult MBBackend::setParam (const juce::String& id, float value)
{
    SetResult r;
    r.applied = false;
    r.actualValue = 0.0f;

    if (auto* p = proc_.apvts.getParameter (id))
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            // 钳制到参数范围，再转 normalized 写入
            const float clamped = juce::jlimit (rp->getNormalisableRange().start,
                                                rp->getNormalisableRange().end,
                                                value);
            p->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (clamped));
            r.applied = true;
            r.actualValue = clamped;
            return r;
        }
        // Choice / Bool：value 视为索引或 0/1，钳制后写
        const float clamped = juce::jlimit (0.0f, 1.0f, value);
        p->setValueNotifyingHost (clamped);
        r.applied = true;
        r.actualValue = clamped;
        return r;
    }

    r.reason = "unknown parameter id: " + id;
    return r;
}

// ---------------- 状态面 ----------------

StateSnapshot MBBackend::getStateSnapshot()
{
    StateSnapshot s;
    s.state = proc_.apvts.copyState();
    s.pluginName = proc_.getName();
    s.pluginVersion = kPluginVersion;
    s.parameterCount = (int) proc_.apvts.processor.getParameters().size();
    return s;
}

bool MBBackend::loadState (const juce::ValueTree& state)
{
    if (! state.isValid())
        return false;
    proc_.apvts.replaceState (state);
    return true;
}

// ---------------- 实时数据面（RT_SAFE）----------------

MeteringSnapshot MBBackend::getMeteringSnapshot()
{
    MeteringSnapshot s;
    s.inputPeakDb  = juce::Decibels::gainToDecibels (proc_.getInputPeak(),  -120.0f);
    s.outputPeakDb = juce::Decibels::gainToDecibels (proc_.getOutputPeak(), -120.0f);
    s.gainReductionDb = proc_.getDSP().getBandGainReductionDb();
    s.timestampMs = juce::Time::getMillisecondCounter();
    return s;
}

HistoryData MBBackend::readHistory (int points)
{
    HistoryData h;
    unsigned counter = 0;
    int written = 0;
    proc_.readHistoryPoints (points, h.inputDb, h.outputDb, counter, written);
    h.counter = counter;
    h.pointsWritten = written;
    h.gainReductionDb.clear();     // MB 不记录 GR 历史（规范 §2）
    return h;
}

// ---------------- 分析面 ----------------

CurveData MBBackend::getTransferCurve (int bandIndex, float minDb, float maxDb, int points)
{
    CurveData c;
    const int n = juce::jmax (2, points);
    c.xValues.reserve ((size_t) n);
    c.yValues.reserve ((size_t) n);

    const float lo = juce::jmin (minDb, maxDb);
    const float hi = juce::jmax (minDb, maxDb);

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) (n - 1);
        const float inDb = lo + (hi - lo) * t;
        c.xValues.push_back (inDb);
        c.yValues.push_back (proc_.getDSP().getBandStaticGainDb (bandIndex, inDb));
    }
    return c;
}

CurveData MBBackend::getFrequencyResponse (int bandIndex, float minFreq, float maxFreq, int points)
{
    CurveData c;
    const int n = juce::jmax (2, points);
    c.xValues.reserve ((size_t) n);
    c.yValues.reserve ((size_t) n);

    // 对数频率轴（频谱一贯做法）
    const float f0 = juce::jmax (1.0f, juce::jmin (minFreq, maxFreq));
    const float f1 = juce::jmax (f0 * 1.001f, juce::jmax (minFreq, maxFreq));
    const float l0 = std::log (f0), l1 = std::log (f1);

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) (n - 1);
        const float f = std::exp (l0 + (l1 - l0) * t);
        c.xValues.push_back (f);
        c.yValues.push_back (proc_.getDSP().getCrossover().getMagnitudeDb (bandIndex, f));
    }
    return c;
}

// ---------------- 插件面 ----------------

void MBBackend::setBypass (bool bypassed)
{
    // 与全局 BYPASS 胶囊同一语义：四段 Mix = 0 宏
    for (int i = 0; i < Param::kNumBands; ++i)
    {
        const auto id = Param::bandMix (i);
        if (auto* p = proc_.apvts.getParameter (id))
            p->setValueNotifyingHost (bypassed ? 0.0f : 1.0f);
    }
}

bool MBBackend::isBypassed()
{
    for (int i = 0; i < Param::kNumBands; ++i)
    {
        if (auto* p = proc_.apvts.getParameter (Param::bandMix (i)))
            if (p->getValue() > 0.001f)
                return false;
    }
    return true;
}

juce::String MBBackend::getPluginName()    { return proc_.getName(); }
juce::String MBBackend::getPluginVersion() { return kPluginVersion; }

// ---------------- IMBBackend：多段扩展 ----------------

MultibandHistoryData MBBackend::readMultibandHistory (int points)
{
    MultibandHistoryData h;
    unsigned counter = 0;
    int written = 0;
    proc_.readHistoryPoints (points, h.inputDb, h.outputDb, counter, written);
    h.counter = counter;
    h.pointsWritten = written;
    return h;
}

MBGroupSetResult MBBackend::setBandGroupDelta (const juce::String& paramName, float delta)
{
    MBGroupSetResult res;
    res.applied = false;
    res.actualValues = { 0.0f, 0.0f, 0.0f, 0.0f };

    bool any = false;
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const juce::String id = groupParamId (paramName, b);
        if (auto* p = proc_.apvts.getParameter (id))
        {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto& nr = rp->getNormalisableRange();
                // 线性恒定偏移（非比例缩放）：实际值上加 delta，再钳制
                const float cur = nr.convertFrom0to1 (p->getValue());
                const float next = juce::jlimit (nr.start, nr.end, cur + delta);
                p->setValueNotifyingHost (nr.convertTo0to1 (next));
                res.actualValues[(size_t) b] = next;
                any = true;
            }
        }
    }
    res.applied = any;
    if (! any)
        res.applied = false;
    return res;
}

SetResult MBBackend::setCrossover (int crossoverIndex, float freqHz)
{
    SetResult r;
    r.applied = false;
    r.actualValue = 0.0f;

    if (crossoverIndex < 0 || crossoverIndex > 2)
    {
        r.reason = "crossoverIndex must be 0..2";
        return r;
    }

    // 读当前三点，改目标点，走统一的最小带宽钳制（每段 ≥ 1 八度）
    float x[3] = { 200.0f, 1000.0f, 6000.0f };
    const char* ids[3] = { Param::ID::xover1, Param::ID::xover2, Param::ID::xover3 };
    for (int i = 0; i < 3; ++i)
        if (auto* p = proc_.apvts.getParameter (ids[i]))
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                x[i] = rp->getNormalisableRange().convertFrom0to1 (p->getValue());

    x[crossoverIndex] = freqHz;
    Param::clampCrossovers (x[0], x[1], x[2]);

    // 回写全部三点（钳制可能连带约束邻居）
    for (int i = 0; i < 3; ++i)
        if (auto* p = proc_.apvts.getParameter (ids[i]))
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                p->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (x[i]));

    r.applied = true;
    r.actualValue = x[crossoverIndex];
    return r;
}

std::array<float, 4> MBBackend::getBandGainReduction()
{
    return proc_.getDSP().getBandGainReductionDb();
}

bool MBBackend::getLatestSpectrum (std::array<float, 512>& pre,
                                   std::array<float, 512>& post,
                                   std::vector<float>& freqs)
{
    auto& eng = proc_.getSpectrumEngine();
    const bool got = eng.getLatest (pre, post);
    if (got)
        eng.getFrequencies (freqs);
    return got;
}

juce::String MBBackend::groupParamId (const juce::String& paramName, int band)
{
    // 规范 §3：paramName ∈ {threshold, attack, release, makeup, mix}
    const juce::String n = paramName.toLowerCase();
    if      (n == "threshold") return Param::bandThreshold (band);
    else if (n == "attack")    return Param::bandAttack    (band);
    else if (n == "release")   return Param::bandRelease   (band);
    else if (n == "makeup")    return Param::bandMakeup    (band);
    else if (n == "mix")       return Param::bandMix       (band);
    return {};
}
