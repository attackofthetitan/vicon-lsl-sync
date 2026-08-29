#pragma once

#include <memory>

#include "gui/GuiServices.h"
#include "gui/LabRecorderFilenamePolicy.h"

class QLabel;
class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QProgressBar;
class QLineEdit;
class QPushButton;
class QSplitter;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QTabWidget;
class QTreeWidget;
class QWidget;

namespace vicon_lsl {
class PreviewPanel;
}

namespace vicon_lsl::gui_detail {

struct BridgeWindowUi {
    QSplitter* main_splitter = nullptr;
    QTabWidget* controls_tabs = nullptr;
    QLabel* workflow_state_label = nullptr;
    QLabel* recording_indicator_label = nullptr;
    QLabel* recording_elapsed_label = nullptr;
    QLabel* recording_path_label = nullptr;
    QLabel* run_identifier_label = nullptr;
    QLabel* bridge_state_label = nullptr;
    QLabel* recorder_state_label = nullptr;
    QLabel* preview_state_label = nullptr;
    QLabel* calibration_state_label = nullptr;
    QLabel* file_state_dashboard_label = nullptr;
    QLabel* verification_state_label = nullptr;
    QLabel* recorder_owner_label = nullptr;
    QLabel* recorder_endpoint_label = nullptr;
    QLabel* storage_label = nullptr;
    QLabel* drop_label = nullptr;
    QLabel* shutdown_label = nullptr;
    QPushButton* start_session_button = nullptr;
    QPushButton* stop_session_button = nullptr;
    QPushButton* run_preflight_button = nullptr;
    QPushButton* emergency_stop_button = nullptr;

    QComboBox* preset_combo = nullptr;
    QPushButton* reset_configuration_button = nullptr;
    QPushButton* save_preset_button = nullptr;
    QPushButton* load_preset_button = nullptr;
    QPushButton* import_configuration_button = nullptr;
    QPushButton* export_configuration_button = nullptr;
    QCheckBox* recorder_only_check = nullptr;
    QTreeWidget* preflight_tree = nullptr;
    QLineEdit* preflight_override_reason_edit = nullptr;
    QPushButton* preflight_override_button = nullptr;

    QLineEdit* server_edit = nullptr;
    QLineEdit* marker_stream_edit = nullptr;
    QLineEdit* segment_stream_edit = nullptr;
    QPushButton* start_button = nullptr;
    QPushButton* stop_button = nullptr;
    QLabel* status_label = nullptr;
    QLabel* markers_label = nullptr;
    QLabel* segments_label = nullptr;
    QLabel* frames_label = nullptr;
    QLabel* frame_rate_label = nullptr;
    QLabel* last_error_label = nullptr;
    QPushButton* acknowledge_error_button = nullptr;

    QLineEdit* study_root_edit = nullptr;
    QLineEdit* filename_template_edit = nullptr;
    QLineEdit* participant_edit = nullptr;
    QLineEdit* session_edit = nullptr;
    QLineEdit* task_edit = nullptr;
    QSpinBox* run_spin = nullptr;
    QLineEdit* acquisition_edit = nullptr;
    QLineEdit* modality_edit = nullptr;
    QLineEdit* filename_preview_label = nullptr;
    QLabel* path_validation_label = nullptr;
    QDoubleSpinBox* storage_warning_spin = nullptr;
    QCheckBox* allow_overwrite_check = nullptr;
    QCheckBox* allow_outside_root_check = nullptr;
    QCheckBox* automatic_run_increment_check = nullptr;
    QPushButton* find_next_run_button = nullptr;
    QLineEdit* labrecorder_executable_edit = nullptr;
    QLineEdit* labrecorder_host_edit = nullptr;
    QSpinBox* labrecorder_port_spin = nullptr;
    QPushButton* launch_labrecorder_button = nullptr;
    QPushButton* connect_labrecorder_button = nullptr;
    QPushButton* detach_labrecorder_button = nullptr;
    QCheckBox* automatic_launch_check = nullptr;
    QCheckBox* record_every_visible_check = nullptr;
    QPushButton* refresh_streams_button = nullptr;
    QPushButton* start_recording_button = nullptr;
    QPushButton* stop_recording_button = nullptr;
    QPushButton* browse_root_button = nullptr;
    QPushButton* browse_labrecorder_button = nullptr;
    QLabel* labrecorder_status_label = nullptr;
    QLabel* labrecorder_operation_label = nullptr;
    QProgressBar* labrecorder_operation_progress = nullptr;
    QLabel* readiness_label = nullptr;

    QPushButton* discover_streams_button = nullptr;
    QTableWidget* stream_table = nullptr;
    QComboBox* marker_binding_combo = nullptr;
    QComboBox* segment_binding_combo = nullptr;
    QComboBox* gaze_binding_combo = nullptr;
    QComboBox* calibration_binding_combo = nullptr;
    QCheckBox* marker_follow_name_check = nullptr;
    QCheckBox* segment_follow_name_check = nullptr;
    QCheckBox* gaze_follow_name_check = nullptr;
    QCheckBox* calibration_follow_name_check = nullptr;
    QCheckBox* preview_external_streams_check = nullptr;
    QLabel* stream_discovery_status_label = nullptr;

    QComboBox* event_severity_filter = nullptr;
    QComboBox* event_component_filter = nullptr;
    QPlainTextEdit* event_log = nullptr;
    QPushButton* copy_diagnostics_button = nullptr;
    QPushButton* export_diagnostics_button = nullptr;
    QPushButton* verification_details_button = nullptr;
    QPushButton* open_verified_recording_button = nullptr;
    vicon_lsl::PreviewPanel* preview_panel = nullptr;

    LabRecorderFilenameFields filenameFields() const;
    void setBridgeInputsEnabled(bool enabled) const;
    bool configurableTooltipsPresent() const;
    bool accessibilityContractSatisfied() const;
};

std::unique_ptr<BridgeWindowUi> buildBridgeWindowUi(
    QWidget* window,
    bool enable_preview,
    const gui::GuiServices& services = gui::defaultGuiServices());

} // namespace vicon_lsl::gui_detail
