#pragma once

#include <memory>

#include "gui/BridgeWindowSettings.h"
#include "gui/LabRecorderFilenamePolicy.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace vicon_lsl {
class PreviewPanel;
}

namespace vicon_lsl::gui_detail {

struct BridgeWindowUi {
    QLineEdit* server_edit = nullptr;
    QLineEdit* marker_stream_edit = nullptr;
    QLineEdit* segment_stream_edit = nullptr;
    QPushButton* start_button = nullptr;
    QPushButton* stop_button = nullptr;
    QPushButton* start_session_button = nullptr;
    QPushButton* stop_session_button = nullptr;
    QLabel* session_status_label = nullptr;
    QLabel* status_label = nullptr;
    QLabel* markers_label = nullptr;
    QLabel* segments_label = nullptr;
    QLabel* frames_label = nullptr;
    QLabel* frame_rate_label = nullptr;
    QLabel* last_error_label = nullptr;

    QLineEdit* study_root_edit = nullptr;
    QLineEdit* filename_template_edit = nullptr;
    QLineEdit* participant_edit = nullptr;
    QLineEdit* session_edit = nullptr;
    QLineEdit* task_edit = nullptr;
    QSpinBox* run_spin = nullptr;
    QLineEdit* acquisition_edit = nullptr;
    QLineEdit* modality_edit = nullptr;
    QLineEdit* filename_preview_label = nullptr;
    QLineEdit* labrecorder_executable_edit = nullptr;
    QLineEdit* labrecorder_host_edit = nullptr;
    QSpinBox* labrecorder_port_spin = nullptr;
    QPushButton* launch_labrecorder_button = nullptr;
    QPushButton* connect_labrecorder_button = nullptr;
    QPushButton* refresh_streams_button = nullptr;
    QPushButton* start_recording_button = nullptr;
    QPushButton* stop_recording_button = nullptr;
    QPushButton* browse_root_button = nullptr;
    QPushButton* browse_labrecorder_button = nullptr;
    QLabel* labrecorder_status_label = nullptr;
    QLabel* readiness_label = nullptr;
    vicon_lsl::PreviewPanel* preview_panel = nullptr;

    void applySettings(const BridgeWindowSettings& settings) const;
    BridgeWindowSettings settings() const;
    LabRecorderFilenameFields filenameFields() const;
    void setBridgeInputsEnabled(bool enabled) const;
};

std::unique_ptr<BridgeWindowUi> buildBridgeWindowUi(QWidget* window, bool enable_preview);

} // namespace vicon_lsl::gui_detail
