#pragma once

#include <lsl_cpp.h>

#include <functional>
#include <memory>
#include <vector>

// Sends samples to LSL, and tests swap in a fake through StreamOutletFactory.
class StreamOutlet {
public:
    virtual ~StreamOutlet() = default;
    virtual void pushSample(const std::vector<double>& sample, double timestamp) = 0;
};

using StreamOutletFactory =
    std::function<std::unique_ptr<StreamOutlet>(const lsl::stream_info&)>;

std::unique_ptr<StreamOutlet> createLslStreamOutlet(const lsl::stream_info& info);
