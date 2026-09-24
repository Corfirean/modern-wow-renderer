#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>
#include <mutex>

namespace renderer
{
    struct DiagnosticCounters
    {
        uint64_t totalFrames = 0;
        uint64_t totalDrawCalls = 0;
        uint64_t classifiedDrawCalls = 0;
        uint64_t unknownDrawCalls = 0;
        uint64_t shadowCasterDraws = 0;
        uint64_t waterDraws = 0;
        uint64_t uiDraws = 0;
        uint64_t cameraCaptures = 0;
        uint64_t depthCaptures = 0;
        uint64_t compositedFrames = 0;
    };

    struct DrawClassification;

    class RendererDiagnostics
    {
    public:
        static RendererDiagnostics& Instance();

        void SetLogPath(const std::wstring& path);

        void OnFrameBegin();
        void OnFrameEnd();

        void RecordDrawCall(bool classified, bool isWater, bool isShadowCaster, bool isUI);
        void RecordDrawCall(const DrawClassification& dc);
        void RecordCameraCapture();
        void RecordDepthCapture();
        void RecordComposite();

        void Log(const std::string& category, const std::string& message);
        void LogOnce(const std::string& category, const std::string& key, const std::string& message);

        const DiagnosticCounters& GetCounters() const { return m_counters; }

    private:
        RendererDiagnostics() = default;

        void FlushAggregatedLog();

        DiagnosticCounters m_counters;
        uint64_t m_currentFrameDrawCalls = 0;
        std::wstring m_logPath;
        std::unordered_set<std::string> m_loggedOnceKeys;
        mutable std::mutex m_mutex;
    };
}
