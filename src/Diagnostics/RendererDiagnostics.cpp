#include "RendererDiagnostics.h"
#include "../Core/ShaderCache.h"
#include "../Scene/MaterialType.h"
#include <fstream>
#include <filesystem>
#include <sstream>

namespace renderer
{
    RendererDiagnostics& RendererDiagnostics::Instance()
    {
        static RendererDiagnostics instance;
        return instance;
    }

    void RendererDiagnostics::SetLogPath(const std::wstring& path)
    {
        std::lock_guard lock(m_mutex);
        m_logPath = path;
    }

    void RendererDiagnostics::OnFrameBegin()
    {
        m_currentFrameDrawCalls = 0;
    }

    void RendererDiagnostics::OnFrameEnd()
    {
        std::lock_guard lock(m_mutex);
        ++m_counters.totalFrames;

        if (m_counters.totalFrames % 600 == 120)
        {
            FlushAggregatedLog();
        }
    }

    void RendererDiagnostics::RecordDrawCall(bool classified, bool isWater, bool isShadowCaster, bool isUI)
    {
        std::lock_guard lock(m_mutex);
        ++m_counters.totalDrawCalls;
        ++m_currentFrameDrawCalls;

        if (classified)
            ++m_counters.classifiedDrawCalls;
        else
            ++m_counters.unknownDrawCalls;

        if (isWater)
            ++m_counters.waterDraws;
        if (isShadowCaster)
            ++m_counters.shadowCasterDraws;
        if (isUI)
            ++m_counters.uiDraws;
    }

    void RendererDiagnostics::RecordDrawCall(const DrawClassification& dc)
    {
        bool classified = (dc.material != MaterialType::Unknown);
        RecordDrawCall(classified, dc.isWater, dc.castsShadow, dc.isUI);
    }

    void RendererDiagnostics::RecordCameraCapture()
    {
        std::lock_guard lock(m_mutex);
        ++m_counters.cameraCaptures;
    }

    void RendererDiagnostics::RecordDepthCapture()
    {
        std::lock_guard lock(m_mutex);
        ++m_counters.depthCaptures;
    }

    void RendererDiagnostics::RecordComposite()
    {
        std::lock_guard lock(m_mutex);
        ++m_counters.compositedFrames;
    }

    void RendererDiagnostics::Log(const std::string& category, const std::string& message)
    {
        std::lock_guard lock(m_mutex);
        if (m_logPath.empty())
            return;

        std::ofstream out(std::filesystem::path(m_logPath), std::ios::app);
        if (out)
        {
            out << "[" << category << "] " << message << "\n";
        }
    }

    void RendererDiagnostics::LogOnce(const std::string& category, const std::string& key, const std::string& message)
    {
        std::lock_guard lock(m_mutex);
        if (m_loggedOnceKeys.count(key) > 0)
            return;

        m_loggedOnceKeys.insert(key);

        if (m_logPath.empty())
            return;

        std::ofstream out(std::filesystem::path(m_logPath), std::ios::app);
        if (out)
        {
            out << "[" << category << "] " << message << "\n";
        }
    }

    void RendererDiagnostics::FlushAggregatedLog()
    {
        if (m_logPath.empty())
            return;

        std::ofstream out(std::filesystem::path(m_logPath), std::ios::app);
        if (!out)
            return;

        auto& sc = ShaderCache::Instance();
        out << "[Perf] frames=" << m_counters.totalFrames
            << " draws=" << m_counters.totalDrawCalls
            << " classified=" << m_counters.classifiedDrawCalls
            << " unknown=" << m_counters.unknownDrawCalls
            << " shadows=" << m_counters.shadowCasterDraws
            << " water=" << m_counters.waterDraws
            << " ui=" << m_counters.uiDraws
            << " camera=" << m_counters.cameraCaptures
            << " depth=" << m_counters.depthCaptures
            << " composited=" << m_counters.compositedFrames
            << " shaderCacheHits=" << sc.GetHits()
            << " shaderCacheMisses=" << sc.GetMisses()
            << "\n";
    }
}
