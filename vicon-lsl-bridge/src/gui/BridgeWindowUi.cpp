#include "gui/BridgeWindowUi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "StreamDefaults.h"
#include "gui/PreviewPanel.h"

namespace vicon_lsl::gui_detail {
namespace {

QLabel* makeTooltipLabel(const QString& text,
                         QWidget* control,
                         const QString& tooltip) {
    auto* label = new QLabel(text);
    label->setToolTip(tooltip);
    if (control) {
        label->setBuddy(control);
        control->setToolTip(tooltip);
        if (control->accessibleName().isEmpty()) {
            QString accessible = text;
            accessible.remove('&');
            accessible.remove(':');
            control->setAccessibleName(accessible.trimmed());
        }
    }
    return label;
}

QLabel* makeStateValue(const QString& text, const QString& accessible_name) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByKeyboard |
                                   Qt::TextSelectableByMouse);
    label->setAccessibleName(accessible_name);
    return label;
}

QScrollArea* scrollable(QWidget* page) {
    auto* area = new QScrollArea();
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(page);
    return area;
}

void setButtonShortcut(QPushButton* button,
                       const QKeySequence& shortcut,
                       const QString& accessible_name) {
    button->setShortcut(shortcut);
    button->setAccessibleName(accessible_name);
}

} // namespace

std::unique_ptr<BridgeWindowUi> buildBridgeWindowUi(
    QWidget* window,
    bool enable_preview,
    const gui::GuiServices& services) {
    auto ui = std::make_unique<BridgeWindowUi>();

    window->setWindowTitle("Vicon LSL Bridge");
    window->setMinimumSize(680, 540);

    auto* main_layout = new QVBoxLayout(window);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    auto* dashboard = new QGroupBox("Session Dashboard");
    auto* dashboard_layout = new QGridLayout(dashboard);
    dashboard_layout->setContentsMargins(8, 8, 8, 8);
    dashboard_layout->setHorizontalSpacing(10);
    dashboard_layout->setVerticalSpacing(4);

    ui->recording_indicator_label = makeStateValue("NOT RECORDING", "Recording indicator");
    QFont indicator_font = ui->recording_indicator_label->font();
    indicator_font.setBold(true);
    indicator_font.setPointSize(indicator_font.pointSize() + 2);
    ui->recording_indicator_label->setFont(indicator_font);
    ui->workflow_state_label = makeStateValue("Idle", "Session workflow state");
    ui->recording_elapsed_label = makeStateValue("00:00:00", "Recording elapsed time");
    ui->run_identifier_label = makeStateValue("run 1", "Recording run identifier");
    ui->recording_path_label = makeStateValue("No validated destination", "Recording destination");
    ui->recording_path_label->setToolTip(
        "The exact normalized destination used by the recorder.");

    dashboard_layout->addWidget(ui->recording_indicator_label, 0, 0);
    dashboard_layout->addWidget(new QLabel("Workflow:"), 0, 1);
    dashboard_layout->addWidget(ui->workflow_state_label, 0, 2);
    dashboard_layout->addWidget(new QLabel("Elapsed:"), 0, 3);
    dashboard_layout->addWidget(ui->recording_elapsed_label, 0, 4);
    dashboard_layout->addWidget(ui->run_identifier_label, 0, 5);
    dashboard_layout->addWidget(new QLabel("Destination:"), 1, 0);
    dashboard_layout->addWidget(ui->recording_path_label, 1, 1, 1, 5);

    ui->bridge_state_label = makeStateValue("Idle", "Bridge lifecycle state");
    ui->recorder_state_label = makeStateValue("Disconnected", "Recorder state");
    ui->preview_state_label = makeStateValue("Idle", "Preview lifecycle state");
    ui->calibration_state_label = makeStateValue("Manual", "Calibration state");
    ui->file_state_dashboard_label = makeStateValue("No file", "Preview file state");
    ui->verification_state_label = makeStateValue("Not verified", "Verification state");
    dashboard_layout->addWidget(new QLabel("Bridge:"), 2, 0);
    dashboard_layout->addWidget(ui->bridge_state_label, 2, 1);
    dashboard_layout->addWidget(new QLabel("Recorder:"), 2, 2);
    dashboard_layout->addWidget(ui->recorder_state_label, 2, 3);
    dashboard_layout->addWidget(new QLabel("Preview:"), 2, 4);
    dashboard_layout->addWidget(ui->preview_state_label, 2, 5);
    dashboard_layout->addWidget(new QLabel("Calibration:"), 3, 0);
    dashboard_layout->addWidget(ui->calibration_state_label, 3, 1);
    dashboard_layout->addWidget(new QLabel("File:"), 3, 2);
    dashboard_layout->addWidget(ui->file_state_dashboard_label, 3, 3);
    dashboard_layout->addWidget(new QLabel("Verification:"), 3, 4);
    dashboard_layout->addWidget(ui->verification_state_label, 3, 5);

    ui->recorder_owner_label = makeStateValue("External or unavailable", "Recorder ownership");
    ui->recorder_endpoint_label = makeStateValue("localhost:22345", "Recorder endpoint");
    ui->storage_label = makeStateValue("Storage: unknown", "Available recording storage");
    ui->drop_label = makeStateValue(
        "Display replacements 0; input backlog discarded 0; latency 0 ms",
        "Preview delivery health distinct from source-rate health");
    dashboard_layout->addWidget(new QLabel("Ownership:"), 4, 0);
    dashboard_layout->addWidget(ui->recorder_owner_label, 4, 1);
    dashboard_layout->addWidget(new QLabel("Endpoint:"), 4, 2);
    dashboard_layout->addWidget(ui->recorder_endpoint_label, 4, 3);
    dashboard_layout->addWidget(ui->storage_label, 4, 4);
    dashboard_layout->addWidget(ui->drop_label, 4, 5);

    auto* dashboard_buttons = new QHBoxLayout();
    ui->start_session_button = new QPushButton("Start Session");
    ui->stop_session_button = new QPushButton("Stop Session");
    ui->run_preflight_button = new QPushButton("Run Preflight");
    ui->emergency_stop_button = new QPushButton("Emergency Stop Recording");
    setButtonShortcut(ui->start_session_button, QKeySequence("Ctrl+Shift+R"),
                      "Start guided session");
    setButtonShortcut(ui->stop_session_button, QKeySequence("Ctrl+Shift+T"),
                      "Stop guided session");
    setButtonShortcut(ui->run_preflight_button, QKeySequence(Qt::Key_F5),
                      "Run session preflight");
    setButtonShortcut(ui->emergency_stop_button, QKeySequence("Ctrl+Shift+S"),
                      "Emergency stop recording");
    ui->start_session_button->setToolTip(
        "Start the bridge, preview, preflight, and recorder in a guided sequence.");
    ui->stop_session_button->setToolTip(
        "Stop recording, preview, bridge, then the owned recorder in reverse safe order.");
    ui->run_preflight_button->setToolTip("Evaluate every configured session requirement now.");
    ui->emergency_stop_button->setToolTip(
        "Request one recorder Stop immediately without duplicating an in-flight Stop.");
    dashboard_buttons->addWidget(ui->start_session_button);
    dashboard_buttons->addWidget(ui->stop_session_button);
    dashboard_buttons->addWidget(ui->run_preflight_button);
    dashboard_buttons->addWidget(ui->emergency_stop_button);
    dashboard_buttons->addStretch(1);
    ui->shutdown_label = makeStateValue(QString(), "Shutdown progress");
    dashboard_buttons->addWidget(ui->shutdown_label, 1);
    dashboard_layout->addLayout(dashboard_buttons, 5, 0, 1, 6);
    dashboard_layout->setColumnStretch(1, 1);
    dashboard_layout->setColumnStretch(3, 1);
    dashboard_layout->setColumnStretch(5, 1);
    main_layout->addWidget(dashboard);

    ui->main_splitter = new QSplitter(Qt::Horizontal);
    ui->main_splitter->setChildrenCollapsible(false);
    ui->main_splitter->setAccessibleName("Main session and preview splitter");
    ui->controls_tabs = new QTabWidget();
    ui->controls_tabs->setMinimumWidth(360);
    ui->controls_tabs->setDocumentMode(true);
    ui->controls_tabs->setAccessibleName("Session controls");

    // Session workflow and reproducible configuration.
    auto* session_page = new QWidget();
    auto* session_layout = new QVBoxLayout(session_page);
    session_layout->setContentsMargins(6, 6, 6, 6);
    session_layout->setSpacing(8);
    auto* preset_group = new QGroupBox("Session Configuration");
    auto* preset_layout = new QGridLayout(preset_group);
    ui->preset_combo = new QComboBox();
    ui->preset_combo->setEditable(true);
    ui->preset_combo->setToolTip("Named, versioned session configurations stored for reuse.");
    ui->reset_configuration_button = new QPushButton("Reset");
    ui->save_preset_button = new QPushButton("Save Preset");
    ui->load_preset_button = new QPushButton("Load Preset");
    ui->import_configuration_button = new QPushButton("Import");
    ui->export_configuration_button = new QPushButton("Export");
    ui->reset_configuration_button->setToolTip("Reset scientific session settings to safe defaults.");
    ui->save_preset_button->setToolTip("Save all reproducibility settings under the entered preset name.");
    ui->load_preset_button->setToolTip("Load the selected session preset.");
    ui->import_configuration_button->setToolTip("Import a versioned session configuration JSON file.");
    ui->export_configuration_button->setToolTip("Export the current session configuration as JSON.");
    preset_layout->addWidget(makeTooltipLabel("Preset:", ui->preset_combo,
                                               ui->preset_combo->toolTip()), 0, 0);
    preset_layout->addWidget(ui->preset_combo, 0, 1, 1, 4);
    preset_layout->addWidget(ui->reset_configuration_button, 1, 0);
    preset_layout->addWidget(ui->save_preset_button, 1, 1);
    preset_layout->addWidget(ui->load_preset_button, 1, 2);
    preset_layout->addWidget(ui->import_configuration_button, 1, 3);
    preset_layout->addWidget(ui->export_configuration_button, 1, 4);
    session_layout->addWidget(preset_group);

    ui->recorder_only_check = new QCheckBox("Recorder-only workflow");
    ui->recorder_only_check->setToolTip(
        "Permit a deliberate recording workflow without requiring the Vicon bridge.");
    session_layout->addWidget(ui->recorder_only_check);

    auto* preflight_group = new QGroupBox("Preflight Result");
    auto* preflight_layout = new QVBoxLayout(preflight_group);
    ui->preflight_tree = new QTreeWidget();
    ui->preflight_tree->setColumnCount(4);
    ui->preflight_tree->setHeaderLabels({"Level", "Component", "Result", "Details"});
    ui->preflight_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->preflight_tree->setRootIsDecorated(false);
    ui->preflight_tree->setAccessibleName("Preflight checks and corrective actions");
    preflight_layout->addWidget(ui->preflight_tree, 1);
    auto* override_row = new QHBoxLayout();
    ui->preflight_override_reason_edit = new QLineEdit();
    ui->preflight_override_reason_edit->setPlaceholderText(
        "Required reason for Record anyway");
    ui->preflight_override_reason_edit->setToolTip(
        "Auditable explanation for deliberately overriding required preflight failures.");
    ui->preflight_override_button = new QPushButton("Record Anyway");
    ui->preflight_override_button->setToolTip(
        "Accept required failures only when a non-empty reason is recorded.");
    override_row->addWidget(makeTooltipLabel("Override reason:",
                                             ui->preflight_override_reason_edit,
                                             ui->preflight_override_reason_edit->toolTip()));
    override_row->addWidget(ui->preflight_override_reason_edit, 1);
    override_row->addWidget(ui->preflight_override_button);
    preflight_layout->addLayout(override_row);
    session_layout->addWidget(preflight_group, 1);
    ui->controls_tabs->addTab(scrollable(session_page), "Session");

    // Bridge controls and persistent status.
    auto* bridge_page = new QWidget();
    auto* bridge_layout = new QVBoxLayout(bridge_page);
    bridge_layout->setContentsMargins(6, 6, 6, 6);
    bridge_layout->setSpacing(8);
    auto* settings_group = new QGroupBox("Connection Settings");
    auto* form = new QFormLayout(settings_group);
    form->setContentsMargins(8, 8, 8, 8);
    form->setVerticalSpacing(4);
    ui->server_edit = new QLineEdit("localhost:801");
    ui->marker_stream_edit = new QLineEdit(vicon_lsl::stream_defaults::ViconMarkers);
    ui->segment_stream_edit = new QLineEdit(vicon_lsl::stream_defaults::ViconSegments);
    ui->marker_stream_edit->setObjectName("bridgeMarkerOutput");
    ui->segment_stream_edit->setObjectName("bridgeSegmentOutput");
    form->addRow(makeTooltipLabel("&Vicon server:", ui->server_edit,
                                  "Vicon DataStream endpoint, for example localhost:801."),
                 ui->server_edit);
    form->addRow(makeTooltipLabel("&Marker stream:", ui->marker_stream_edit,
                                  "LSL stream name for Vicon marker samples."),
                 ui->marker_stream_edit);
    form->addRow(makeTooltipLabel("&Segment stream:", ui->segment_stream_edit,
                                  "LSL stream name for Vicon segment samples."),
                 ui->segment_stream_edit);
    bridge_layout->addWidget(settings_group);
    auto* bridge_buttons = new QHBoxLayout();
    ui->start_button = new QPushButton("Start Streaming");
    ui->stop_button = new QPushButton("Stop Bridge");
    ui->stop_button->setEnabled(false);
    setButtonShortcut(ui->start_button, QKeySequence("Ctrl+B"), "Start bridge streaming");
    setButtonShortcut(ui->stop_button, QKeySequence("Ctrl+Shift+B"), "Stop bridge streaming");
    ui->start_button->setToolTip("Connect to Vicon and publish the configured LSL streams.");
    ui->stop_button->setToolTip("Request asynchronous bridge shutdown.");
    bridge_buttons->addWidget(ui->start_button);
    bridge_buttons->addWidget(ui->stop_button);
    bridge_buttons->addStretch(1);
    bridge_layout->addLayout(bridge_buttons);

    auto* bridge_status_group = new QGroupBox("Persistent Bridge State");
    auto* bridge_status = new QGridLayout(bridge_status_group);
    ui->status_label = makeStateValue("Disconnected", "Detailed bridge state");
    ui->markers_label = makeStateValue("0", "Discovered Vicon markers");
    ui->segments_label = makeStateValue("0", "Discovered Vicon segments");
    ui->frames_label = makeStateValue("0", "Vicon frame number");
    ui->frame_rate_label = makeStateValue("0.0 Hz", "Effective Vicon frame rate");
    ui->last_error_label = makeStateValue("-", "Last application error");
    ui->acknowledge_error_button = new QPushButton("Acknowledge Error");
    ui->acknowledge_error_button->setToolTip(
        "Clear the persistent Last error display without deleting the event history.");
    bridge_status->addWidget(new QLabel("Bridge state:"), 0, 0);
    bridge_status->addWidget(ui->status_label, 0, 1, 1, 3);
    bridge_status->addWidget(new QLabel("Vicon frames:"), 1, 0);
    bridge_status->addWidget(ui->frames_label, 1, 1);
    bridge_status->addWidget(new QLabel("Vicon rate:"), 1, 2);
    bridge_status->addWidget(ui->frame_rate_label, 1, 3);
    bridge_status->addWidget(new QLabel("Markers:"), 2, 0);
    bridge_status->addWidget(ui->markers_label, 2, 1);
    bridge_status->addWidget(new QLabel("Segments:"), 2, 2);
    bridge_status->addWidget(ui->segments_label, 2, 3);
    bridge_status->addWidget(new QLabel("Last error:"), 3, 0);
    bridge_status->addWidget(ui->last_error_label, 3, 1, 1, 2);
    bridge_status->addWidget(ui->acknowledge_error_button, 3, 3);
    bridge_status->setColumnStretch(1, 1);
    bridge_status->setColumnStretch(3, 1);
    bridge_layout->addWidget(bridge_status_group);
    bridge_layout->addStretch(1);
    ui->controls_tabs->addTab(scrollable(bridge_page), "Bridge");

    // Recorder destination, process ownership, and direct controls.
    auto* recording_page = new QWidget();
    auto* recording_layout = new QVBoxLayout(recording_page);
    recording_layout->setContentsMargins(6, 6, 6, 6);
    recording_layout->setSpacing(8);
    auto* destination_group = new QGroupBox("Exact Recording Destination");
    auto* recording_form = new QFormLayout(destination_group);
    recording_form->setVerticalSpacing(4);
    auto* root_layout = new QHBoxLayout();
    ui->study_root_edit = new QLineEdit();
    ui->browse_root_button = new QPushButton("Browse");
    ui->browse_root_button->setToolTip("Choose the recording study root directory.");
    root_layout->addWidget(ui->study_root_edit, 1);
    root_layout->addWidget(ui->browse_root_button);
    ui->filename_template_edit = new QLineEdit(
        "sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf");
    ui->participant_edit = new QLineEdit("P001");
    ui->session_edit = new QLineEdit("S001");
    ui->task_edit = new QLineEdit("Task");
    ui->run_spin = new QSpinBox();
    ui->run_spin->setRange(1, 999999);
    ui->run_spin->setValue(1);
    ui->acquisition_edit = new QLineEdit("vicon");
    ui->modality_edit = new QLineEdit("beh");
    ui->filename_preview_label = new QLineEdit();
    ui->filename_preview_label->setReadOnly(true);
    ui->filename_preview_label->setAccessibleName("Exact normalized recording destination");
    recording_form->addRow(makeTooltipLabel("Study &root:", ui->study_root_edit,
                                             "Canonical root under which recordings are written."),
                           root_layout);
    recording_form->addRow(makeTooltipLabel("File &template:", ui->filename_template_edit,
        "Tokens: %p participant, %s session, %b task/block, %r or %n run, %a acquisition, %m modality."),
        ui->filename_template_edit);
    auto* metadata_grid = new QGridLayout();
    metadata_grid->addWidget(makeTooltipLabel("Participant:", ui->participant_edit,
                                               "Participant identifier substituted for %p."), 0, 0);
    metadata_grid->addWidget(ui->participant_edit, 0, 1);
    metadata_grid->addWidget(makeTooltipLabel("Session:", ui->session_edit,
                                               "Session identifier substituted for %s."), 0, 2);
    metadata_grid->addWidget(ui->session_edit, 0, 3);
    metadata_grid->addWidget(makeTooltipLabel("Task/block:", ui->task_edit,
                                               "Task identifier substituted for %b."), 1, 0);
    metadata_grid->addWidget(ui->task_edit, 1, 1);
    metadata_grid->addWidget(makeTooltipLabel("Run:", ui->run_spin,
                                               "Run number substituted for %r."), 1, 2);
    metadata_grid->addWidget(ui->run_spin, 1, 3);
    metadata_grid->addWidget(makeTooltipLabel("Acquisition:", ui->acquisition_edit,
                                               "Acquisition label substituted for %a."), 2, 0);
    metadata_grid->addWidget(ui->acquisition_edit, 2, 1);
    metadata_grid->addWidget(makeTooltipLabel("Modality:", ui->modality_edit,
                                               "Modality label substituted for %m."), 2, 2);
    metadata_grid->addWidget(ui->modality_edit, 2, 3);
    metadata_grid->setColumnStretch(1, 1);
    metadata_grid->setColumnStretch(3, 1);
    recording_form->addRow("Metadata:", metadata_grid);
    recording_form->addRow("Exact destination:", ui->filename_preview_label);
    ui->path_validation_label = makeStateValue("Not validated", "Recording path validation");
    recording_form->addRow("Validation:", ui->path_validation_label);
    ui->storage_warning_spin = new QDoubleSpinBox();
    ui->storage_warning_spin->setRange(0.0, 10000.0);
    ui->storage_warning_spin->setDecimals(1);
    ui->storage_warning_spin->setSuffix(" GiB");
    ui->storage_warning_spin->setValue(10.0);
    recording_form->addRow(makeTooltipLabel("Storage warning:", ui->storage_warning_spin,
                                             "Warn when available storage falls below this threshold."),
                           ui->storage_warning_spin);
    auto* path_policy = new QHBoxLayout();
    ui->allow_overwrite_check = new QCheckBox("Allow overwrite");
    ui->allow_outside_root_check = new QCheckBox("Allow outside study root");
    ui->automatic_run_increment_check = new QCheckBox("Increment run after verified completion");
    ui->allow_overwrite_check->setToolTip("Advanced opt-in to overwrite an existing destination.");
    ui->allow_outside_root_check->setToolTip(
        "Advanced opt-in to write outside the canonical study root.");
    ui->automatic_run_increment_check->setToolTip(
        "Increment only after the output exists and the configured verification policy passes.");
    path_policy->addWidget(ui->allow_overwrite_check);
    path_policy->addWidget(ui->allow_outside_root_check);
    path_policy->addWidget(ui->automatic_run_increment_check);
    recording_form->addRow("Path policy:", path_policy);
    ui->find_next_run_button = new QPushButton("Find Next Run");
    ui->find_next_run_button->setToolTip("Choose the first run number whose normalized destination is unused.");
    recording_form->addRow(QString(), ui->find_next_run_button);
    recording_layout->addWidget(destination_group);

    auto* recorder_group = new QGroupBox("Recorder Connection and Ownership");
    auto* recorder_layout = new QVBoxLayout(recorder_group);
    auto* labrecorder_form = new QFormLayout();
    auto* executable_layout = new QHBoxLayout();
    ui->labrecorder_executable_edit = new QLineEdit();
    ui->browse_labrecorder_button = new QPushButton("Browse");
    ui->browse_labrecorder_button->setToolTip("Choose a custom graphical recorder executable.");
    executable_layout->addWidget(ui->labrecorder_executable_edit, 1);
    executable_layout->addWidget(ui->browse_labrecorder_button);
    labrecorder_form->addRow(makeTooltipLabel("Recorder executable:",
        ui->labrecorder_executable_edit,
        "Custom graphical recorder path; otherwise the bundled recorder is used."),
        executable_layout);
    ui->labrecorder_host_edit = new QLineEdit("localhost");
    ui->labrecorder_port_spin = new QSpinBox();
    ui->labrecorder_port_spin->setRange(1, 65535);
    ui->labrecorder_port_spin->setValue(22345);
    auto* endpoint = new QHBoxLayout();
    endpoint->addWidget(makeTooltipLabel("Host:", ui->labrecorder_host_edit,
                                         "Recorder remote-control host."));
    endpoint->addWidget(ui->labrecorder_host_edit, 1);
    endpoint->addWidget(makeTooltipLabel("Port:", ui->labrecorder_port_spin,
                                         "Recorder remote-control TCP port."));
    endpoint->addWidget(ui->labrecorder_port_spin);
    labrecorder_form->addRow("Endpoint:", endpoint);
    recorder_layout->addLayout(labrecorder_form);
    ui->automatic_launch_check = new QCheckBox("Automatically launch recorder when endpoint is unavailable");
    ui->record_every_visible_check =
        new QCheckBox("Use external graphical recorder (records every visible stream)");
    ui->automatic_launch_check->setToolTip(
        "Try the configured endpoint first, then launch the bundled or custom recorder asynchronously.");
    ui->record_every_visible_check->setToolTip(
        "Control an external graphical recorder through its remote endpoint. Leave disabled to use exact per-stream selection.");
    recorder_layout->addWidget(ui->automatic_launch_check);
    recorder_layout->addWidget(ui->record_every_visible_check);
    auto* recorder_buttons = new QGridLayout();
    ui->launch_labrecorder_button = new QPushButton("Launch Recorder");
    ui->connect_labrecorder_button = new QPushButton("Connect");
    ui->detach_labrecorder_button = new QPushButton("Disconnect / Detach");
    ui->refresh_streams_button = new QPushButton("Refresh Recorder Streams");
    ui->start_recording_button = new QPushButton("Start Recording");
    ui->stop_recording_button = new QPushButton("Stop Recording");
    ui->launch_labrecorder_button->setToolTip("Launch one owned graphical recorder asynchronously.");
    ui->connect_labrecorder_button->setToolTip("Connect to the configured remote-control endpoint.");
    ui->detach_labrecorder_button->setToolTip(
        "Disconnect from an external recorder or explicitly detach an owned process without ending it.");
    ui->refresh_streams_button->setToolTip("Ask the graphical recorder to refresh its visible stream list.");
    ui->start_recording_button->setToolTip("Run preflight and begin one deterministic recording operation.");
    ui->stop_recording_button->setToolTip("Request one safe recorder Stop operation.");
    setButtonShortcut(ui->start_recording_button, QKeySequence("Ctrl+R"), "Start recording");
    setButtonShortcut(ui->stop_recording_button, QKeySequence("Ctrl+S"), "Stop recording");
    recorder_buttons->addWidget(ui->launch_labrecorder_button, 0, 0);
    recorder_buttons->addWidget(ui->connect_labrecorder_button, 0, 1);
    recorder_buttons->addWidget(ui->detach_labrecorder_button, 0, 2);
    recorder_buttons->addWidget(ui->refresh_streams_button, 1, 0);
    recorder_buttons->addWidget(ui->start_recording_button, 1, 1);
    recorder_buttons->addWidget(ui->stop_recording_button, 1, 2);
    recorder_layout->addLayout(recorder_buttons);
    ui->labrecorder_status_label = makeStateValue("Disconnected", "Detailed recorder status");
    ui->labrecorder_operation_label = makeStateValue("Idle", "Recorder operation progress");
    ui->labrecorder_operation_progress = new QProgressBar();
    ui->labrecorder_operation_progress->setRange(0, 1);
    ui->labrecorder_operation_progress->setValue(0);
    ui->labrecorder_operation_progress->setTextVisible(true);
    ui->labrecorder_operation_progress->setAccessibleName("Recorder command batch progress");
    ui->readiness_label = makeStateValue("Readiness not evaluated", "Recording readiness");
    recorder_layout->addWidget(ui->labrecorder_status_label);
    recorder_layout->addWidget(ui->labrecorder_operation_label);
    recorder_layout->addWidget(ui->labrecorder_operation_progress);
    recorder_layout->addWidget(ui->readiness_label);
    recording_layout->addWidget(recorder_group);
    recording_layout->addStretch(1);
    ui->controls_tabs->addTab(scrollable(recording_page), "Recording");

    // Identity-first discovery and recording selection.
    auto* streams_page = new QWidget();
    auto* streams_layout = new QVBoxLayout(streams_page);
    streams_layout->setContentsMargins(6, 6, 6, 6);
    auto* discovery_row = new QHBoxLayout();
    ui->discover_streams_button = new QPushButton("Discover LSL Streams");
    setButtonShortcut(ui->discover_streams_button, QKeySequence("Ctrl+D"),
                      "Discover visible LSL streams");
    ui->discover_streams_button->setToolTip(
        "Discover candidate streams and retain exact source identity, host, schema, and metadata.");
    ui->preview_external_streams_check = new QCheckBox("Preview external streams");
    ui->preview_external_streams_check->setObjectName("previewExternalStreams");
    ui->preview_external_streams_check->setToolTip(
        "Advanced override: preview sources need not follow the bridge output names.");
    ui->stream_discovery_status_label = makeStateValue("Not discovered", "Stream discovery status");
    discovery_row->addWidget(ui->discover_streams_button);
    discovery_row->addWidget(ui->preview_external_streams_check);
    discovery_row->addWidget(ui->stream_discovery_status_label, 1);
    streams_layout->addLayout(discovery_row);

    ui->stream_table = new QTableWidget(0, 13);
    ui->stream_table->setHorizontalHeaderLabels({
        "Record", "Required", "Role", "Name", "Type", "Source ID", "Host",
        "Session", "Channels", "Nominal", "Effective", "Frame",
        "Freshness / warning"});
    ui->stream_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->stream_table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->stream_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->stream_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->stream_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->stream_table->setMinimumHeight(220);
    ui->stream_table->setAccessibleName(
        "Visible streams with recording selection and identity metadata");
    streams_layout->addWidget(ui->stream_table, 1);

    auto* bindings_group = new QGroupBox("Preview Role Bindings");
    auto* bindings = new QGridLayout(bindings_group);
    ui->marker_binding_combo = new QComboBox();
    ui->segment_binding_combo = new QComboBox();
    ui->gaze_binding_combo = new QComboBox();
    ui->calibration_binding_combo = new QComboBox();
    ui->marker_follow_name_check = new QCheckBox("Follow by name");
    ui->segment_follow_name_check = new QCheckBox("Follow by name");
    ui->gaze_follow_name_check = new QCheckBox("Follow by name");
    ui->calibration_follow_name_check = new QCheckBox("Follow by name");
    QComboBox* const binding_combos[] = {
        ui->marker_binding_combo, ui->segment_binding_combo,
        ui->gaze_binding_combo, ui->calibration_binding_combo,
    };
    for (QComboBox* combo : binding_combos) {
        combo->setMinimumContentsLength(22);
        combo->setToolTip(
            "Bind this preview role to a visible source identity. Duplicate names are never chosen silently.");
    }
    QCheckBox* const follow_checks[] = {
        ui->marker_follow_name_check, ui->segment_follow_name_check,
        ui->gaze_follow_name_check, ui->calibration_follow_name_check,
    };
    for (QCheckBox* check : follow_checks) {
        check->setToolTip(
            "Permit deterministic name-based reconnection when source IDs are intentionally unstable.");
    }
    bindings->addWidget(makeTooltipLabel("Markers:", ui->marker_binding_combo,
                                          ui->marker_binding_combo->toolTip()), 0, 0);
    bindings->addWidget(ui->marker_binding_combo, 0, 1);
    bindings->addWidget(ui->marker_follow_name_check, 0, 2);
    bindings->addWidget(makeTooltipLabel("Segments:", ui->segment_binding_combo,
                                          ui->segment_binding_combo->toolTip()), 1, 0);
    bindings->addWidget(ui->segment_binding_combo, 1, 1);
    bindings->addWidget(ui->segment_follow_name_check, 1, 2);
    bindings->addWidget(makeTooltipLabel("Gaze:", ui->gaze_binding_combo,
                                          ui->gaze_binding_combo->toolTip()), 2, 0);
    bindings->addWidget(ui->gaze_binding_combo, 2, 1);
    bindings->addWidget(ui->gaze_follow_name_check, 2, 2);
    bindings->addWidget(makeTooltipLabel("Calibration:", ui->calibration_binding_combo,
                                          ui->calibration_binding_combo->toolTip()), 3, 0);
    bindings->addWidget(ui->calibration_binding_combo, 3, 1);
    bindings->addWidget(ui->calibration_follow_name_check, 3, 2);
    bindings->setColumnStretch(1, 1);
    streams_layout->addWidget(bindings_group);
    ui->controls_tabs->addTab(scrollable(streams_page), "Streams");

    // Bounded event history and diagnostic export.
    auto* events_page = new QWidget();
    auto* events_layout = new QVBoxLayout(events_page);
    events_layout->setContentsMargins(6, 6, 6, 6);
    auto* filter_row = new QHBoxLayout();
    ui->event_severity_filter = new QComboBox();
    ui->event_severity_filter->addItems({"Information and above", "Warnings and errors", "Errors only"});
    ui->event_severity_filter->setToolTip("Minimum event severity displayed in this view.");
    ui->event_component_filter = new QComboBox();
    ui->event_component_filter->addItems({
        "All components", "Application", "Bridge", "Recorder", "Preview",
        "Calibration", "File", "Path", "Streams", "Verification"});
    ui->event_component_filter->setToolTip("Component filter for the bounded event view.");
    filter_row->addWidget(makeTooltipLabel("Severity:", ui->event_severity_filter,
                                           ui->event_severity_filter->toolTip()));
    filter_row->addWidget(ui->event_severity_filter);
    filter_row->addWidget(makeTooltipLabel("Component:", ui->event_component_filter,
                                           ui->event_component_filter->toolTip()));
    filter_row->addWidget(ui->event_component_filter);
    filter_row->addStretch(1);
    events_layout->addLayout(filter_row);
    ui->event_log = new QPlainTextEdit();
    ui->event_log->setReadOnly(true);
    ui->event_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    ui->event_log->setAccessibleName("Bounded timestamped session event log");
    events_layout->addWidget(ui->event_log, 1);
    auto* diagnostic_buttons = new QHBoxLayout();
    ui->copy_diagnostics_button = new QPushButton("Copy Diagnostics");
    ui->export_diagnostics_button = new QPushButton("Export Diagnostic Bundle");
    ui->verification_details_button = new QPushButton("Verification Details");
    ui->open_verified_recording_button = new QPushButton("Open Recording in Preview");
    ui->copy_diagnostics_button->setToolTip("Copy configuration, states, inventory, rates, and recent events.");
    ui->export_diagnostics_button->setToolTip(
        "Export diagnostics without recording sample data unless separately requested.");
    ui->verification_details_button->setToolTip("Show stream-by-stream post-recording findings.");
    ui->open_verified_recording_button->setToolTip("Open the verified recording in background playback loading.");
    diagnostic_buttons->addWidget(ui->copy_diagnostics_button);
    diagnostic_buttons->addWidget(ui->export_diagnostics_button);
    diagnostic_buttons->addWidget(ui->verification_details_button);
    diagnostic_buttons->addWidget(ui->open_verified_recording_button);
    diagnostic_buttons->addStretch(1);
    events_layout->addLayout(diagnostic_buttons);
    ui->controls_tabs->addTab(scrollable(events_page), "Events");

    ui->main_splitter->addWidget(ui->controls_tabs);
    if (enable_preview) {
        ui->preview_panel = new vicon_lsl::PreviewPanel(nullptr, services);
        ui->main_splitter->addWidget(ui->preview_panel);
    } else {
        auto* placeholder = new QLabel("Preview disabled");
        placeholder->setAlignment(Qt::AlignCenter);
        ui->main_splitter->addWidget(placeholder);
    }
    ui->main_splitter->setStretchFactor(0, 0);
    ui->main_splitter->setStretchFactor(1, 1);
    ui->main_splitter->setSizes({520, 980});
    main_layout->addWidget(ui->main_splitter, 1);
    return ui;
}

void BridgeWindowUi::applySettings(const BridgeWindowSettings& values) const {
    server_edit->setText(values.server);
    marker_stream_edit->setText(values.marker_stream);
    segment_stream_edit->setText(values.segment_stream);
    study_root_edit->setText(values.recording_root);
    filename_template_edit->setText(values.recording_template);
    participant_edit->setText(values.participant);
    session_edit->setText(values.session);
    task_edit->setText(values.task);
    run_spin->setValue(values.run);
    acquisition_edit->setText(values.acquisition);
    modality_edit->setText(values.modality);
    labrecorder_executable_edit->setText(values.labrecorder_executable);
    labrecorder_host_edit->setText(values.labrecorder_host);
    labrecorder_port_spin->setValue(values.labrecorder_port);
}

BridgeWindowSettings BridgeWindowUi::settings() const {
    BridgeWindowSettings values;
    values.server = server_edit->text();
    values.marker_stream = marker_stream_edit->text();
    values.segment_stream = segment_stream_edit->text();
    values.recording_root = study_root_edit->text();
    values.recording_template = filename_template_edit->text();
    values.participant = participant_edit->text();
    values.session = session_edit->text();
    values.task = task_edit->text();
    values.run = run_spin->value();
    values.acquisition = acquisition_edit->text();
    values.modality = modality_edit->text();
    values.labrecorder_executable = labrecorder_executable_edit->text();
    values.labrecorder_host = labrecorder_host_edit->text();
    values.labrecorder_port = labrecorder_port_spin->value();
    return values;
}

LabRecorderFilenameFields BridgeWindowUi::filenameFields() const {
    LabRecorderFilenameFields fields;
    fields.root = study_root_edit->text();
    fields.templ = filename_template_edit->text();
    fields.participant = participant_edit->text();
    fields.session = session_edit->text();
    fields.task = task_edit->text();
    fields.run = QString::number(run_spin->value());
    fields.acquisition = acquisition_edit->text();
    fields.modality = modality_edit->text();
    return fields;
}

void BridgeWindowUi::setBridgeInputsEnabled(bool enabled) const {
    server_edit->setEnabled(enabled);
    marker_stream_edit->setEnabled(enabled);
    segment_stream_edit->setEnabled(enabled);
}

bool BridgeWindowUi::configurableTooltipsPresent() const {
    const QWidget* const controls[] = {
        server_edit, marker_stream_edit, segment_stream_edit, study_root_edit,
        filename_template_edit, participant_edit, session_edit, task_edit,
        run_spin, acquisition_edit, modality_edit, storage_warning_spin,
        labrecorder_executable_edit, labrecorder_host_edit, labrecorder_port_spin,
        preset_combo, recorder_only_check, preflight_override_reason_edit,
        automatic_launch_check, record_every_visible_check, allow_overwrite_check,
        allow_outside_root_check, automatic_run_increment_check,
        preview_external_streams_check, marker_binding_combo, segment_binding_combo,
        gaze_binding_combo, calibration_binding_combo, event_severity_filter,
        event_component_filter,
    };
    for (const QWidget* control : controls) {
        if (!control || control->toolTip().trimmed().isEmpty()) return false;
    }
    return !preview_panel || preview_panel->configurableTooltipsPresent();
}

bool BridgeWindowUi::accessibilityContractSatisfied() const {
    const QPushButton* const shortcut_buttons[] = {
        start_session_button, stop_session_button, run_preflight_button,
        emergency_stop_button, start_button, stop_button,
        start_recording_button, stop_recording_button, discover_streams_button,
    };
    for (const QPushButton* button : shortcut_buttons) {
        if (!button || button->shortcut().isEmpty() ||
            button->accessibleName().trimmed().isEmpty() ||
            button->focusPolicy() == Qt::NoFocus) {
            return false;
        }
    }
    const QWidget* const named_views[] = {
        main_splitter, controls_tabs, preflight_tree, stream_table, event_log,
        filename_preview_label, labrecorder_operation_progress,
    };
    for (const QWidget* view : named_views) {
        if (!view || view->accessibleName().trimmed().isEmpty()) return false;
    }
    return configurableTooltipsPresent() &&
           (!preview_panel || preview_panel->accessibilityContractSatisfied());
}

} // namespace vicon_lsl::gui_detail
