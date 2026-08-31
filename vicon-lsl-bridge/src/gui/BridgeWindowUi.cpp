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

QLabel* makeTooltipLabel(const QString& text, QWidget* control, const QString& tooltip) {
    auto* label = new QLabel(text);
    label->setToolTip(tooltip);
    if (control) {
        label->setBuddy(control);
        control->setToolTip(tooltip);
        if (control->accessibleName().isEmpty()) {
            QString acc = text;
            acc.remove('&');
            acc.remove(':');
            control->setAccessibleName(acc.trimmed());
        }
    }
    return label;
}

QLabel* makeStateValue(const QString& text, const QString& accessible_name) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    label->setAccessibleName(accessible_name);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& tooltip, const QKeySequence& shortcut = {}, const QString& acc_name = {}) {
    auto* button = new QPushButton(text);
    button->setToolTip(tooltip);
    if (!shortcut.isEmpty()) button->setShortcut(shortcut);
    if (!acc_name.isEmpty()) button->setAccessibleName(acc_name);
    return button;
}

void addField(QGridLayout* layout, int row, int col, const QString& label, QWidget* control, const QString& tooltip = {}, int span = 1) {
    layout->addWidget(tooltip.isEmpty() ? new QLabel(label) : makeTooltipLabel(label, control, tooltip), row, col);
    layout->addWidget(control, row, col + 1, 1, span);
}

void addWidgets(QBoxLayout* layout, std::initializer_list<QWidget*> widgets) {
    for (QWidget* w : widgets) layout->addWidget(w);
}

QScrollArea* scrollable(QWidget* page) {
    auto* area = new QScrollArea();
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(page);
    return area;
}

} // namespace

std::unique_ptr<BridgeWindowUi> buildBridgeWindowUi(
    QWidget* window, bool enable_preview, const std::shared_ptr<QSettings>& settings) {
    auto ui = std::make_unique<BridgeWindowUi>();
    window->setWindowTitle("Vicon LSL Bridge");
    window->setMinimumSize(680, 540);

    auto* main_layout = new QVBoxLayout(window);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    // Dashboard
    auto* dashboard = new QGroupBox("Session Status");
    auto* db_layout = new QGridLayout(dashboard);
    db_layout->setContentsMargins(8, 8, 8, 8);
    db_layout->setHorizontalSpacing(10);
    db_layout->setVerticalSpacing(4);

    ui->recording_indicator_label = makeStateValue("NOT RECORDING", "Recording indicator");
    QFont ind_font = ui->recording_indicator_label->font();
    ind_font.setBold(true);
    ind_font.setPointSize(ind_font.pointSize() + 2);
    ui->recording_indicator_label->setFont(ind_font);
    ui->workflow_state_label = makeStateValue("Idle", "Session status");
    ui->recording_elapsed_label = makeStateValue("00:00:00", "Recording elapsed time");
    ui->run_identifier_label = makeStateValue("run 1", "Recording run number");
    ui->recording_path_label = makeStateValue("No checked destination", "Recording destination");
    ui->recording_path_label->setToolTip("The exact file path used by the recorder.");

    db_layout->addWidget(ui->recording_indicator_label, 0, 0);
    addField(db_layout, 0, 1, "Session:", ui->workflow_state_label);
    addField(db_layout, 0, 3, "Elapsed:", ui->recording_elapsed_label);
    db_layout->addWidget(ui->run_identifier_label, 0, 5);
    addField(db_layout, 1, 0, "Destination:", ui->recording_path_label, {}, 5);

    ui->bridge_state_label = makeStateValue("Idle", "Bridge status");
    ui->recorder_state_label = makeStateValue("Disconnected", "Recorder state");
    ui->preview_state_label = makeStateValue("Idle", "Preview status");
    ui->calibration_state_label = makeStateValue("Manual", "Calibration state");
    ui->file_state_dashboard_label = makeStateValue("No file", "Preview file state");
    ui->verification_state_label = makeStateValue("Not checked", "Recording file check");
    addField(db_layout, 2, 0, "Bridge:", ui->bridge_state_label);
    addField(db_layout, 2, 2, "Recorder:", ui->recorder_state_label);
    addField(db_layout, 2, 4, "Preview:", ui->preview_state_label);
    addField(db_layout, 3, 0, "Calibration:", ui->calibration_state_label);
    addField(db_layout, 3, 2, "File:", ui->file_state_dashboard_label);
    addField(db_layout, 3, 4, "File check:", ui->verification_state_label);

    ui->recorder_owner_label = makeStateValue("Started elsewhere or unavailable", "Who started the recorder");
    ui->recorder_endpoint_label = makeStateValue("localhost:22345", "Recorder address");
    ui->storage_label = makeStateValue("Storage: unknown", "Available recording storage");
    ui->drop_label = makeStateValue("Skipped preview frames 0; combined updates 0; delay 0 ms", "Preview update status");
    addField(db_layout, 4, 0, "Started by:", ui->recorder_owner_label);
    addField(db_layout, 4, 2, "Address:", ui->recorder_endpoint_label);
    db_layout->addWidget(ui->storage_label, 4, 4);
    db_layout->addWidget(ui->drop_label, 4, 5);

    auto* db_buttons = new QHBoxLayout();
    ui->start_session_button = makeButton("Start Session", "Start the bridge and preview, check the setup, then start recording.",
                                          QKeySequence("Ctrl+Shift+R"), "Start guided session");
    ui->stop_session_button = makeButton("Stop Session", "Stop recording, preview, and the bridge, then close a recorder started here.",
                                         QKeySequence("Ctrl+Shift+T"), "Stop guided session");
    ui->run_preflight_button = makeButton("Check Setup", "Check whether the session is ready to record.",
                                          QKeySequence(Qt::Key_F5), "Check session setup");
    ui->emergency_stop_button = makeButton("Emergency Stop Recording", "Ask the recorder to stop immediately.",
                                           QKeySequence("Ctrl+Shift+S"), "Emergency stop recording");
    addWidgets(db_buttons, {ui->start_session_button, ui->stop_session_button, ui->run_preflight_button, ui->emergency_stop_button});
    db_buttons->addStretch(1);
    ui->shutdown_label = makeStateValue(QString(), "Shutdown progress");
    db_buttons->addWidget(ui->shutdown_label, 1);
    db_layout->addLayout(db_buttons, 5, 0, 1, 6);
    db_layout->setColumnStretch(1, 1);
    db_layout->setColumnStretch(3, 1);
    db_layout->setColumnStretch(5, 1);
    main_layout->addWidget(dashboard);

    ui->main_splitter = new QSplitter(Qt::Horizontal);
    ui->main_splitter->setChildrenCollapsible(false);
    ui->main_splitter->setAccessibleName("Main session and preview splitter");
    ui->controls_tabs = new QTabWidget();
    ui->controls_tabs->setMinimumWidth(360);
    ui->controls_tabs->setDocumentMode(true);
    ui->controls_tabs->setAccessibleName("Session controls");

    // Session Tab
    auto* session_page = new QWidget();
    auto* session_layout = new QVBoxLayout(session_page);
    session_layout->setContentsMargins(6, 6, 6, 6);
    session_layout->setSpacing(8);

    auto* preset_group = new QGroupBox("Session Settings");
    auto* preset_layout = new QGridLayout(preset_group);
    ui->preset_combo = new QComboBox();
    ui->preset_combo->setEditable(true);
    ui->preset_combo->setToolTip("Saved session settings that can be used again.");
    ui->reset_configuration_button = makeButton("Reset", "Reset the session settings to safe defaults.");
    ui->save_preset_button = makeButton("Save Preset", "Save the current session settings under this name.");
    ui->load_preset_button = makeButton("Load Preset", "Load the selected session preset.");
    ui->import_configuration_button = makeButton("Import", "Load session settings from a file.");
    ui->export_configuration_button = makeButton("Export", "Save the current session settings to a file.");
    preset_layout->addWidget(makeTooltipLabel("Preset:", ui->preset_combo, ui->preset_combo->toolTip()), 0, 0);
    preset_layout->addWidget(ui->preset_combo, 0, 1, 1, 4);
    preset_layout->addWidget(ui->reset_configuration_button, 1, 0);
    preset_layout->addWidget(ui->save_preset_button, 1, 1);
    preset_layout->addWidget(ui->load_preset_button, 1, 2);
    preset_layout->addWidget(ui->import_configuration_button, 1, 3);
    preset_layout->addWidget(ui->export_configuration_button, 1, 4);
    session_layout->addWidget(preset_group);

    ui->recorder_only_check = new QCheckBox("Record without the Vicon bridge");
    ui->recorder_only_check->setToolTip("Allow recording when the Vicon bridge is not running.");
    session_layout->addWidget(ui->recorder_only_check);

    auto* preflight_group = new QGroupBox("Setup Check");
    auto* preflight_layout = new QVBoxLayout(preflight_group);
    ui->preflight_tree = new QTreeWidget();
    ui->preflight_tree->setColumnCount(4);
    ui->preflight_tree->setHeaderLabels({"Importance", "Part", "Result", "Details"});
    ui->preflight_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->preflight_tree->setRootIsDecorated(false);
    ui->preflight_tree->setAccessibleName("Setup checks and suggested fixes");
    preflight_layout->addWidget(ui->preflight_tree, 1);

    auto* override_row = new QHBoxLayout();
    ui->preflight_override_reason_edit = new QLineEdit();
    ui->preflight_override_reason_edit->setPlaceholderText("Required reason for Record anyway");
    ui->preflight_override_reason_edit->setToolTip("Explain why recording should start even though a required check failed.");
    ui->preflight_override_button = makeButton("Record Anyway", "Start recording after entering a reason for the failed check.");
    override_row->addWidget(makeTooltipLabel("Reason:", ui->preflight_override_reason_edit, ui->preflight_override_reason_edit->toolTip()));
    override_row->addWidget(ui->preflight_override_reason_edit, 1);
    override_row->addWidget(ui->preflight_override_button);
    preflight_layout->addLayout(override_row);
    session_layout->addWidget(preflight_group, 1);
    ui->controls_tabs->addTab(scrollable(session_page), "Session");

    // Bridge Tab
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
    form->addRow(makeTooltipLabel("&Vicon server:", ui->server_edit, "Address of the Vicon computer, such as localhost:801."), ui->server_edit);
    form->addRow(makeTooltipLabel("&Marker stream:", ui->marker_stream_edit, "LSL stream name for Vicon marker samples."), ui->marker_stream_edit);
    form->addRow(makeTooltipLabel("&Segment stream:", ui->segment_stream_edit, "LSL stream name for Vicon segment samples."), ui->segment_stream_edit);
    bridge_layout->addWidget(settings_group);

    auto* bridge_buttons = new QHBoxLayout();
    ui->start_button = makeButton("Start Streaming", "Connect to Vicon and publish the configured LSL streams.", QKeySequence("Ctrl+B"), "Start bridge streaming");
    ui->stop_button = makeButton("Stop Bridge", "Ask the bridge to stop without freezing the window.", QKeySequence("Ctrl+Shift+B"), "Stop bridge streaming");
    ui->stop_button->setEnabled(false);
    bridge_buttons->addWidget(ui->start_button);
    bridge_buttons->addWidget(ui->stop_button);
    bridge_buttons->addStretch(1);
    bridge_layout->addLayout(bridge_buttons);

    auto* bridge_status_group = new QGroupBox("Bridge Status");
    auto* bridge_status = new QGridLayout(bridge_status_group);
    ui->status_label = makeStateValue("Disconnected", "Detailed bridge state");
    ui->markers_label = makeStateValue("0", "Discovered Vicon markers");
    ui->segments_label = makeStateValue("0", "Discovered Vicon segments");
    ui->frames_label = makeStateValue("0", "Vicon frame number");
    ui->frame_rate_label = makeStateValue("0.0 Hz", "Measured Vicon frame rate");
    ui->last_error_label = makeStateValue("-", "Last application error");
    ui->acknowledge_error_button = makeButton("Clear Error", "Clear the Last error display without deleting the event history.");
    addField(bridge_status, 0, 0, "Bridge state:", ui->status_label, {}, 3);
    addField(bridge_status, 1, 0, "Vicon frames:", ui->frames_label);
    addField(bridge_status, 1, 2, "Vicon rate:", ui->frame_rate_label);
    addField(bridge_status, 2, 0, "Markers:", ui->markers_label);
    addField(bridge_status, 2, 2, "Segments:", ui->segments_label);
    addField(bridge_status, 3, 0, "Last error:", ui->last_error_label, {}, 2);
    bridge_status->addWidget(ui->acknowledge_error_button, 3, 3);
    bridge_status->setColumnStretch(1, 1);
    bridge_status->setColumnStretch(3, 1);
    bridge_layout->addWidget(bridge_status_group);
    bridge_layout->addStretch(1);
    ui->controls_tabs->addTab(scrollable(bridge_page), "Bridge");

    // Recording Tab
    auto* recording_page = new QWidget();
    auto* recording_layout = new QVBoxLayout(recording_page);
    recording_layout->setContentsMargins(6, 6, 6, 6);
    recording_layout->setSpacing(8);

    auto* destination_group = new QGroupBox("Recording Destination");
    auto* recording_form = new QFormLayout(destination_group);
    recording_form->setVerticalSpacing(4);

    auto* root_layout = new QHBoxLayout();
    ui->study_root_edit = new QLineEdit();
    ui->browse_root_button = makeButton("Browse", "Choose the recording study root directory.");
    root_layout->addWidget(ui->study_root_edit, 1);
    root_layout->addWidget(ui->browse_root_button);
    ui->filename_template_edit = new QLineEdit("sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf");
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
    ui->filename_preview_label->setAccessibleName("Exact recording file path");
    recording_form->addRow(makeTooltipLabel("Study &root:", ui->study_root_edit, "Main folder where recordings are saved."), root_layout);
    recording_form->addRow(makeTooltipLabel("File &template:", ui->filename_template_edit,
        "Use %p for participant, %s for session, %b for task, %r for run, %a for source, and %m for data type."), ui->filename_template_edit);

    auto* metadata_grid = new QGridLayout();
    addField(metadata_grid, 0, 0, "Participant:", ui->participant_edit, "Participant value used for %p.");
    addField(metadata_grid, 0, 2, "Session:", ui->session_edit, "Session value used for %s.");
    addField(metadata_grid, 1, 0, "Task/block:", ui->task_edit, "Task or block value used for %b.");
    addField(metadata_grid, 1, 2, "Run:", ui->run_spin, "Run number substituted for %r.");
    addField(metadata_grid, 2, 0, "Source label:", ui->acquisition_edit, "Short recording-source label used for %a.");
    addField(metadata_grid, 2, 2, "Data type:", ui->modality_edit, "Short data-type label used for %m.");
    metadata_grid->setColumnStretch(1, 1);
    metadata_grid->setColumnStretch(3, 1);
    recording_form->addRow("Recording details:", metadata_grid);
    recording_form->addRow("Exact destination:", ui->filename_preview_label);
    ui->path_validation_label = makeStateValue("Not checked", "Recording path check");
    recording_form->addRow("Path check:", ui->path_validation_label);

    ui->storage_warning_spin = new QDoubleSpinBox();
    ui->storage_warning_spin->setRange(0.0, 10000.0);
    ui->storage_warning_spin->setDecimals(1);
    ui->storage_warning_spin->setSuffix(" GiB");
    ui->storage_warning_spin->setValue(10.0);
    recording_form->addRow(makeTooltipLabel("Storage warning:", ui->storage_warning_spin, "Warn when available storage falls below this threshold."), ui->storage_warning_spin);

    auto* path_policy = new QHBoxLayout();
    ui->allow_overwrite_check = new QCheckBox("Allow overwrite");
    ui->allow_outside_root_check = new QCheckBox("Allow outside study root");
    ui->automatic_run_increment_check = new QCheckBox("Increment run after a successful file check");
    ui->allow_overwrite_check->setToolTip("Advanced opt-in to overwrite an existing destination.");
    ui->allow_outside_root_check->setToolTip("Allow a recording to be saved outside the study folder.");
    ui->automatic_run_increment_check->setToolTip("Increment only after the output exists and the selected file check succeeds.");
    addWidgets(path_policy, {ui->allow_overwrite_check, ui->allow_outside_root_check, ui->automatic_run_increment_check});
    recording_form->addRow("Saving options:", path_policy);
    ui->find_next_run_button = makeButton("Find Next Run", "Choose the first run number with an unused file path.");
    recording_form->addRow(QString(), ui->find_next_run_button);
    recording_layout->addWidget(destination_group);

    auto* recorder_group = new QGroupBox("Recorder");
    auto* recorder_layout = new QVBoxLayout(recorder_group);
    auto* labrecorder_form = new QFormLayout();
    auto* executable_layout = new QHBoxLayout();
    ui->labrecorder_executable_edit = new QLineEdit();
    ui->browse_labrecorder_button = makeButton("Browse", "Choose a custom graphical recorder program.");
    executable_layout->addWidget(ui->labrecorder_executable_edit, 1);
    executable_layout->addWidget(ui->browse_labrecorder_button);
    labrecorder_form->addRow(makeTooltipLabel("Recorder program:", ui->labrecorder_executable_edit,
        "Custom graphical recorder path; otherwise the bundled recorder is used."), executable_layout);

    ui->labrecorder_host_edit = new QLineEdit("localhost");
    ui->labrecorder_port_spin = new QSpinBox();
    ui->labrecorder_port_spin->setRange(1, 65535);
    ui->labrecorder_port_spin->setValue(22345);
    auto* endpoint = new QHBoxLayout();
    endpoint->addWidget(makeTooltipLabel("Host:", ui->labrecorder_host_edit, "Computer running the recorder."));
    endpoint->addWidget(ui->labrecorder_host_edit, 1);
    endpoint->addWidget(makeTooltipLabel("Port:", ui->labrecorder_port_spin, "Port used to control the recorder."));
    endpoint->addWidget(ui->labrecorder_port_spin);
    labrecorder_form->addRow("Address:", endpoint);
    recorder_layout->addLayout(labrecorder_form);

    ui->automatic_launch_check = new QCheckBox("Start the recorder when it is not already running");
    ui->record_every_visible_check = new QCheckBox("Record every visible stream in a separate recorder window");
    ui->automatic_launch_check->setToolTip("Try to connect first, then start the bundled or chosen recorder in the background.");
    ui->record_every_visible_check->setToolTip("Control another recorder and save every stream it can see. Turn this off to choose streams here.");
    recorder_layout->addWidget(ui->automatic_launch_check);
    recorder_layout->addWidget(ui->record_every_visible_check);

    auto* recorder_buttons = new QGridLayout();
    ui->launch_labrecorder_button = makeButton("Launch Recorder", "Start the graphical recorder in the background.");
    ui->connect_labrecorder_button = makeButton("Connect", "Connect to the recorder at the host and port above.");
    ui->detach_labrecorder_button = makeButton("Disconnect / Detach", "Disconnect from the recorder without closing it.");
    ui->refresh_streams_button = makeButton("Refresh Recorder Streams", "Ask the graphical recorder to refresh its visible stream list.");
    ui->start_recording_button = makeButton("Start Recording", "Check the setup, then start recording.", QKeySequence("Ctrl+R"), "Start recording");
    ui->stop_recording_button = makeButton("Stop Recording", "Ask the recorder to stop.", QKeySequence("Ctrl+S"), "Stop recording");
    recorder_buttons->addWidget(ui->launch_labrecorder_button, 0, 0);
    recorder_buttons->addWidget(ui->connect_labrecorder_button, 0, 1);
    recorder_buttons->addWidget(ui->detach_labrecorder_button, 0, 2);
    recorder_buttons->addWidget(ui->refresh_streams_button, 1, 0);
    recorder_buttons->addWidget(ui->start_recording_button, 1, 1);
    recorder_buttons->addWidget(ui->stop_recording_button, 1, 2);
    recorder_layout->addLayout(recorder_buttons);

    ui->labrecorder_status_label = makeStateValue("Disconnected", "Detailed recorder status");
    ui->labrecorder_operation_label = makeStateValue("Idle", "Recorder progress");
    ui->labrecorder_operation_progress = new QProgressBar();
    ui->labrecorder_operation_progress->setRange(0, 1);
    ui->labrecorder_operation_progress->setValue(0);
    ui->labrecorder_operation_progress->setTextVisible(true);
    ui->labrecorder_operation_progress->setAccessibleName("Recorder command progress");
    ui->readiness_label = makeStateValue("Setup not checked", "Recording readiness");
    recorder_layout->addWidget(ui->labrecorder_status_label);
    recorder_layout->addWidget(ui->labrecorder_operation_label);
    recorder_layout->addWidget(ui->labrecorder_operation_progress);
    recorder_layout->addWidget(ui->readiness_label);
    recording_layout->addWidget(recorder_group);
    recording_layout->addStretch(1);
    ui->controls_tabs->addTab(scrollable(recording_page), "Recording");

    // Streams Tab
    auto* streams_page = new QWidget();
    auto* streams_layout = new QVBoxLayout(streams_page);
    streams_layout->setContentsMargins(6, 6, 6, 6);

    auto* discovery_row = new QHBoxLayout();
    ui->discover_streams_button = makeButton("Find LSL Streams", "Find available streams and keep the details needed to reconnect.",
                                             QKeySequence("Ctrl+D"), "Discover visible LSL streams");
    ui->preview_external_streams_check = new QCheckBox("Choose preview streams separately");
    ui->preview_external_streams_check->setObjectName("previewExternalStreams");
    ui->preview_external_streams_check->setToolTip("Let the preview use streams other than the bridge outputs.");
    ui->stream_discovery_status_label = makeStateValue("Not searched", "Stream search status");
    addWidgets(discovery_row, {ui->discover_streams_button, ui->preview_external_streams_check});
    discovery_row->addWidget(ui->stream_discovery_status_label, 1);
    streams_layout->addLayout(discovery_row);

    ui->stream_table = new QTableWidget(0, 13);
    ui->stream_table->setHorizontalHeaderLabels({
        "Record", "Required", "Use", "Name", "Type", "Unique source", "Computer",
        "Session", "Channels", "Expected rate", "Measured rate", "Coordinate system",
        "Last update / warning"});
    ui->stream_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->stream_table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->stream_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->stream_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->stream_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->stream_table->setMinimumHeight(220);
    ui->stream_table->setAccessibleName("Available streams and recording choices");
    streams_layout->addWidget(ui->stream_table, 1);

    auto* bindings_group = new QGroupBox("Preview Streams");
    auto* bindings = new QGridLayout(bindings_group);
    ui->marker_binding_combo = new QComboBox();
    ui->segment_binding_combo = new QComboBox();
    ui->gaze_binding_combo = new QComboBox();
    ui->calibration_binding_combo = new QComboBox();
    ui->marker_follow_name_check = new QCheckBox("Reconnect by name");
    ui->segment_follow_name_check = new QCheckBox("Reconnect by name");
    ui->gaze_follow_name_check = new QCheckBox("Reconnect by name");
    ui->calibration_follow_name_check = new QCheckBox("Reconnect by name");

    QComboBox* const binding_combos[] = {ui->marker_binding_combo, ui->segment_binding_combo, ui->gaze_binding_combo, ui->calibration_binding_combo};
    for (QComboBox* combo : binding_combos) {
        combo->setMinimumContentsLength(22);
        combo->setToolTip("Choose the visible stream source for this part of the preview. Duplicate names are never chosen silently.");
    }
    QCheckBox* const follow_checks[] = {ui->marker_follow_name_check, ui->segment_follow_name_check, ui->gaze_follow_name_check, ui->calibration_follow_name_check};
    for (QCheckBox* check : follow_checks) check->setToolTip("Use the stream name when the original stream restarts.");

    addField(bindings, 0, 0, "Markers:", ui->marker_binding_combo, ui->marker_binding_combo->toolTip());
    bindings->addWidget(ui->marker_follow_name_check, 0, 2);
    addField(bindings, 1, 0, "Segments:", ui->segment_binding_combo, ui->segment_binding_combo->toolTip());
    bindings->addWidget(ui->segment_follow_name_check, 1, 2);
    addField(bindings, 2, 0, "Gaze:", ui->gaze_binding_combo, ui->gaze_binding_combo->toolTip());
    bindings->addWidget(ui->gaze_follow_name_check, 2, 2);
    addField(bindings, 3, 0, "Calibration:", ui->calibration_binding_combo, ui->calibration_binding_combo->toolTip());
    bindings->addWidget(ui->calibration_follow_name_check, 3, 2);
    bindings->setColumnStretch(1, 1);
    streams_layout->addWidget(bindings_group);
    ui->controls_tabs->addTab(scrollable(streams_page), "Streams");

    // Events Tab
    auto* events_page = new QWidget();
    auto* events_layout = new QVBoxLayout(events_page);
    events_layout->setContentsMargins(6, 6, 6, 6);

    auto* filter_row = new QHBoxLayout();
    ui->event_severity_filter = new QComboBox();
    ui->event_severity_filter->addItems({"Information and above", "Warnings and errors", "Errors only"});
    ui->event_severity_filter->setToolTip("Choose which messages to show.");
    ui->event_component_filter = new QComboBox();
    ui->event_component_filter->addItems({"All parts", "Application", "Bridge", "Recorder", "Preview", "Calibration", "File", "Path", "Streams", "File check"});
    ui->event_component_filter->setToolTip("Show events from one part of the app.");
    addWidgets(filter_row, {
        makeTooltipLabel("Show:", ui->event_severity_filter, ui->event_severity_filter->toolTip()), ui->event_severity_filter,
        makeTooltipLabel("Part:", ui->event_component_filter, ui->event_component_filter->toolTip()), ui->event_component_filter
    });
    filter_row->addStretch(1);
    events_layout->addLayout(filter_row);

    ui->event_log = new QPlainTextEdit();
    ui->event_log->setReadOnly(true);
    ui->event_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    ui->event_log->setAccessibleName("Recent session events with times");
    events_layout->addWidget(ui->event_log, 1);

    auto* diag_buttons = new QHBoxLayout();
    ui->copy_diagnostics_button = makeButton("Copy Session Details", "Copy settings, status, stream details, rates, and recent events.");
    ui->export_diagnostics_button = makeButton("Export Session Details", "Save session details without including recorded samples.");
    ui->verification_details_button = makeButton("File Check Details", "Show the result for each recorded stream.");
    ui->open_verified_recording_button = makeButton("Open Recording in Preview", "Open the checked recording without freezing the window.");
    addWidgets(diag_buttons, {ui->copy_diagnostics_button, ui->export_diagnostics_button, ui->verification_details_button, ui->open_verified_recording_button});
    diag_buttons->addStretch(1);
    events_layout->addLayout(diag_buttons);
    ui->controls_tabs->addTab(scrollable(events_page), "Events");

    ui->main_splitter->addWidget(ui->controls_tabs);
    if (enable_preview) {
        ui->preview_panel = new vicon_lsl::PreviewPanel(nullptr, settings);
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

LabRecorderFilenameFields BridgeWindowUi::filenameFields() const {
    return {
        study_root_edit->text(),
        filename_template_edit->text(),
        participant_edit->text(),
        session_edit->text(),
        task_edit->text(),
        QString::number(run_spin->value()),
        acquisition_edit->text(),
        modality_edit->text()
    };
}

void BridgeWindowUi::setBridgeInputsEnabled(bool enabled) const {
    server_edit->setEnabled(enabled);
    marker_stream_edit->setEnabled(enabled);
    segment_stream_edit->setEnabled(enabled);
}

} // namespace vicon_lsl::gui_detail
