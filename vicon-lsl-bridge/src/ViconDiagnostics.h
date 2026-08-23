#pragma once

#include "ViconFrameTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace vicon_lsl {

struct DiagnosticEmission {
    std::vector<std::string> log_lines;
    std::string status_message;

    bool shouldReportStatus() const { return !status_message.empty(); }
};

class DiagnosticAggregator {
public:
    explicit DiagnosticAggregator(unsigned int repeat_interval = 100);

    DiagnosticEmission record(const std::vector<ViconDiagnostic>& diagnostics);
    void clear();

private:
    unsigned int repeat_interval_;
    std::unordered_map<std::string, unsigned int> counts_;
};

const char* toString(DiagnosticSeverity severity);
const char* toString(ViconReadStatus status);

std::string formatDiagnostic(const ViconDiagnostic& diagnostic);
std::string diagnosticKey(const ViconDiagnostic& diagnostic);
std::string summarizeDiagnostics(const std::vector<ViconDiagnostic>& diagnostics);

} // namespace vicon_lsl
