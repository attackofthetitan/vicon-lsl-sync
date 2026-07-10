#include "StreamOutlet.h"

namespace {

class LslStreamOutlet final : public StreamOutlet {
public:
    explicit LslStreamOutlet(const lsl::stream_info& info) : outlet_(info) {}

    void pushSample(const std::vector<double>& sample, double timestamp) override {
        outlet_.push_sample(sample, timestamp);
    }

private:
    lsl::stream_outlet outlet_;
};

} // namespace

std::unique_ptr<StreamOutlet> createLslStreamOutlet(const lsl::stream_info& info) {
    return std::make_unique<LslStreamOutlet>(info);
}
