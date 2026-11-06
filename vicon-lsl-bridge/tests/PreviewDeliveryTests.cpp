#include "preview/PreviewDeliveryMailbox.h"
#include "TestSupport.h"

TEST_CASE("Live preview mailbox bounds slow-renderer delivery to the newest frame") {
    vicon_lsl::PreviewDeliveryMailbox mailbox;
    for (int index = 0; index < 1000; ++index) {
        vicon_lsl::PreviewFrame frame;
        frame.timestamp = static_cast<double>(index);
        mailbox.publish(std::move(frame), 1000 + index);
    }
    vicon_lsl::PreviewFrame displayed;
    vicon_lsl::PreviewDeliveryMetrics metrics;
    REQUIRE(mailbox.takeLatest(displayed, metrics, 2025));
    REQUIRE_EQ(displayed.timestamp, 999.0);
    REQUIRE_EQ(metrics.replaced_before_display, 999ULL);
    REQUIRE_EQ(metrics.display_latency_ms, static_cast<std::int64_t>(26));
    // Only the newest frame was kept, so nothing is left after taking it.
    REQUIRE(!mailbox.takeLatest(displayed, metrics, 2030));

    mailbox.addCoalescedInputSamples(17);
    REQUIRE(!mailbox.takeLatest(displayed, metrics, 2040));
    REQUIRE_EQ(metrics.coalesced_input_samples, 17ULL);
    REQUIRE_EQ(metrics.replaced_before_display, 999ULL);
}
