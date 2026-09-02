#include "gui/BridgeWindowUi.h"

#include "gui/FlowLayout.h"
#include "gui/WidgetHelpers.h"

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
#include <QSizePolicy>
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

std::unique_ptr<BridgeWindowUi> buildBridgeWindowUi(
    QWidget* window, bool enable_preview, const std::shared_ptr<QSettings>& settings) {
    auto ui = std::make_unique<BridgeWindowUi>();
    window->setWindowTitle("Vicon LSL Bridge");
    window->setMinimumSize(680, 540);

    auto* main_layout = new QVBoxLayout(window);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    // Dashboard
    auto [dashboard, db_layout] = makeGroup<QGridLayout>("Session Status");
    db_layout->setContentsMargins(8, 8, 8, 8);
    db_layout->setHorizontalSpacing(10);
    db_layout->setVerticalSpacing(4);

    ui->recording_indicator_label = makeStateValue("NOT RECORDING", "Recording indicator");
    QFont ind_font = ui->recording_indicator_label->font();
    ind_font.setBold(true);
    ind_font.setPointSize(ind_font.pointSize() + 2);
    ui->recording_indicator_label->setFont(ind_font);
    ui->recording_elapsed_label = makeStateValue("00:00:00", "Recording elapsed time");
    ui->run_identifier_label = makeStateValue("run 1", "Recording run number");
    ui->recording_path_label = makeStateValue("No checked destination", "Recording destination");
    // A destination path has no spaces to wrap on, so its size hint would set the
    // window's minimum width. The text is elided to the width it is given.
    ui->recording_path_label->setWordWrap(false);
    ui->recording_path_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui->recording_path_label->setToolTip("The exact file path used by the recorder.");

    db_layout->addWidget(ui->recording_indicator_label, 0, 0);
    addField(db_layout, 0, 1, "Elapsed:", ui->recording_elapsed_label);
    db_layout->addWidget(ui->run_identifier_label, 0, 3);
    addField(db_layout, 1, 0, "Destination:", ui->recording_path_label, {}, 5);

    ui->bridge_state_label = makeStateValue("Idle", "Bridge status");
    ui->recorder_state_label = makeStateValue("Disconnected", "Recorder state");
    ui->preview_state_label = makeStateValue("Idle", "Preview status");
    ui->calibration_state_label = makeStateValue("Not calibrated", "Calibration state");
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
                                          "Start guided session", QKeySequence("Ctrl+Shift+R"));
    ui->stop_session_button = makeButton("Stop Session", "Stop recording, preview, and the bridge, then close a recorder started here.",
                                         "Stop guided session", QKeySequence("Ctrl+Shift+T"));
    ui->run_setup_check_button = makeButton("Check Setup", "Check whether the session is ready to record.",
                                          "Check session setup", QKeySequence(Qt::Key_F5));
    addWidgets(db_buttons, {ui->start_session_button, ui->stop_session_button, ui->run_setup_check_button});
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
    auto [session_page, session_layout] = makePage();

    auto [preset_group, preset_layout] = makeGroup<QVBoxLayout>("Session Settings");
    ui->preset_combo = new QComboBox();
    ui->preset_combo->setEditable(true);
    ui->preset_combo->setToolTip("Saved session settings that can be used again.");
    ui->reset_configuration_button = makeButton("Reset", "Reset the session settings to safe defaults.");
    ui->save_preset_button = makeButton("Save Preset", "Save the current session settings under this name.");
    ui->load_preset_button = makeButton("Load Preset", "Load the selected session preset.");
    ui->import_configuration_button = makeButton("Import", "Load session settings from a file.");
    ui->export_configuration_button = makeButton("Export", "Save the current session settings to a file.");
    auto* preset_row = new QHBoxLayout();
    preset_row->addWidget(makeTooltipLabel("Preset:", ui->preset_combo, ui->preset_combo->toolTip()));
    preset_row->addWidget(ui->preset_combo, 1);
    preset_layout->addLayout(preset_row);
    // Five buttons in fixed grid cells could not shrink below their combined
    // width, which was the Session tab's sideways overflow.
    auto* preset_buttons = new FlowLayout();
    for (QWidget* control : {ui->reset_configuration_button, ui->save_preset_button,
                             ui->load_preset_button, ui->import_configuration_button,
                             ui->export_configuration_button}) {
        preset_buttons->addWidget(control);
    }
    preset_layout->addLayout(preset_buttons);
    session_layout->addWidget(preset_group);

    ui->recorder_only_check = makeCheck("Record without the Vicon bridge", "Allow recording when the Vicon bridge is not running.");
    session_layout->addWidget(ui->recorder_only_check);

    auto [setup_check_group, setup_check_layout] = makeGroup<QVBoxLayout>("Setup Check");
    ui->setup_check_tree = new QTreeWidget();
    ui->setup_check_tree->setColumnCount(4);
    ui->setup_check_tree->setHeaderLabels({"Importance", "Part", "Result", "Details"});
    ui->setup_check_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->setup_check_tree->setRootIsDecorated(false);
    ui->setup_check_tree->setAccessibleName("Setup checks and suggested fixes");
    setup_check_layout->addWidget(ui->setup_check_tree, 1);

    auto* override_row = new QHBoxLayout();
    ui->setup_check_override_reason_edit = new QLineEdit();
    ui->setup_check_override_reason_edit->setPlaceholderText("Required reason for Record anyway");
    ui->setup_check_override_reason_edit->setToolTip("Explain why recording should start even though a required check failed.");
    ui->setup_check_override_button = makeButton("Record Anyway", "Start recording after entering a reason for the failed check.");
    override_row->addWidget(makeTooltipLabel("Reason:", ui->setup_check_override_reason_edit, ui->setup_check_override_reason_edit->toolTip()));
    override_row->addWidget(ui->setup_check_override_reason_edit, 1);
    override_row->addWidget(ui->setup_check_override_button);
    setup_check_layout->addLayout(override_row);
    session_layout->addWidget(setup_check_group, 1);
    ui->controls_tabs->addTab(scrollable(session_page), "Session");

    // Bridge Tab
    auto [bridge_page, bridge_layout] = makePage();

    auto [settings_group, form] = makeGroup<QFormLayout>("Connection Settings");
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
    ui->start_button = makeButton("Start Streaming", "Connect to Vicon and publish the configured LSL streams.", "Start bridge streaming", QKeySequence("Ctrl+B"));
    ui->stop_button = makeButton("Stop Bridge", "Ask the bridge to stop without freezing the window.", "Stop bridge streaming", QKeySequence("Ctrl+Shift+B"));
    ui->stop_button->setEnabled(false);
    bridge_buttons->addWidget(ui->start_button);
    bridge_buttons->addWidget(ui->stop_button);
    bridge_buttons->addStretch(1);
    bridge_layout->addLayout(bridge_buttons);

    auto [bridge_status_group, bridge_status] = makeGroup<QGridLayout>("Bridge Status");
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
    auto [recording_page, recording_layout] = makePage();

    auto [destination_group, recording_form] = makeGroup<QFormLayout>("Recording Destination");
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
    ui->run_spin = makeSpin(1, 999999, 1);
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

    // Three long checkbox labels in one unbreakable row were most of the
    // Recording tab's sideways overflow.
    auto* path_policy = new FlowLayout();
    ui->allow_overwrite_check = makeCheck("Allow overwrite", "Advanced opt-in to overwrite an existing destination.");
    ui->allow_outside_root_check = makeCheck("Allow outside study root", "Allow a recording to be saved outside the study folder.");
    ui->automatic_run_increment_check = makeCheck("Increment run after a successful file check", "Increment only after the output exists and the selected file check succeeds.");
    for (QWidget* control : {ui->allow_overwrite_check, ui->allow_outside_root_check,
                             ui->automatic_run_increment_check}) {
        path_policy->addWidget(control);
    }
    recording_form->addRow("Saving options:", path_policy);
    ui->find_next_run_button = makeButton("Find Next Run", "Choose the first run number with an unused file path.");
    recording_form->addRow(QString(), ui->find_next_run_button);
    recording_layout->addWidget(destination_group);

    auto [recorder_group, recorder_layout] = makeGroup<QVBoxLayout>("Recorder");
    auto* labrecorder_form = new QFormLayout();
    auto* executable_layout = new QHBoxLayout();
    ui->labrecorder_executable_edit = new QLineEdit();
    ui->browse_labrecorder_button = makeButton("Browse", "Choose a custom graphical recorder program.");
    executable_layout->addWidget(ui->labrecorder_executable_edit, 1);
    executable_layout->addWidget(ui->browse_labrecorder_button);
    labrecorder_form->addRow(makeTooltipLabel("Recorder program:", ui->labrecorder_executable_edit,
        "Custom graphical recorder path; otherwise the bundled recorder is used."), executable_layout);

    ui->labrecorder_host_edit = new QLineEdit("localhost");
    ui->labrecorder_port_spin = makeSpin(1, 65535, 22345);
    auto* endpoint = new QHBoxLayout();
    endpoint->addWidget(makeTooltipLabel("Host:", ui->labrecorder_host_edit, "Computer running the recorder."));
    endpoint->addWidget(ui->labrecorder_host_edit, 1);
    endpoint->addWidget(makeTooltipLabel("Port:", ui->labrecorder_port_spin, "Port used to control the recorder."));
    endpoint->addWidget(ui->labrecorder_port_spin);
    labrecorder_form->addRow("Address:", endpoint);
    recorder_layout->addLayout(labrecorder_form);

    ui->automatic_launch_check = makeCheck("Start the recorder when it is not already running", "Try to connect first, then start the bundled or chosen recorder in the background.");
    ui->record_every_visible_check = makeCheck("Record every visible stream in a separate recorder window", "Control another recorder and save every stream it can see. Turn this off to choose streams here.");
    recorder_layout->addWidget(ui->automatic_launch_check);
    recorder_layout->addWidget(ui->record_every_visible_check);

    auto* recorder_buttons = new QGridLayout();
    ui->launch_labrecorder_button = makeButton("Launch Recorder", "Start the graphical recorder in the background.");
    ui->connect_labrecorder_button = makeButton("Connect", "Connect to the recorder at the host and port above.");
    ui->detach_labrecorder_button = makeButton("Disconnect / Detach", "Disconnect from the recorder without closing it.");
    ui->refresh_streams_button = makeButton("Refresh Recorder Streams", "Ask the graphical recorder to refresh its visible stream list.");
    ui->start_recording_button = makeButton("Start Recording", "Check the setup, then start recording.", "Start recording", QKeySequence("Ctrl+R"));
    ui->stop_recording_button = makeButton("Stop Recording", "Ask the recorder to stop.", "Stop recording", QKeySequence("Ctrl+S"));
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
    auto [streams_page, streams_layout] = makePage();

    auto* discovery_row = new QHBoxLayout();
    ui->discover_streams_button = makeButton("Find LSL Streams", "Find available streams and keep the details needed to reconnect.",
                                             "Discover visible LSL streams", QKeySequence("Ctrl+D"));
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

    auto [bindings_group, bindings] = makeGroup<QGridLayout>("Preview Streams");
    const struct { const char* label; QComboBox** combo; QCheckBox** follow; } bindings_rows[] = {
        {"Markers:", &ui->marker_binding_combo, &ui->marker_follow_name_check},
        {"Segments:", &ui->segment_binding_combo, &ui->segment_follow_name_check},
        {"Gaze:", &ui->gaze_binding_combo, &ui->gaze_follow_name_check},
        {"Calibration:", &ui->calibration_binding_combo, &ui->calibration_follow_name_check},
    };
    int binding_row = 0;
    for (const auto& row : bindings_rows) {
        *row.combo = new QComboBox();
        (*row.combo)->setMinimumContentsLength(22);
        (*row.combo)->setToolTip("Choose the visible stream source for this part of the preview. Duplicate names are never chosen silently.");
        *row.follow = makeCheck("Reconnect by name", "Use the stream name when the original stream restarts.");
        addField(bindings, binding_row, 0, row.label, *row.combo, (*row.combo)->toolTip());
        bindings->addWidget(*row.follow, binding_row, 2);
        ++binding_row;
    }
    bindings->setColumnStretch(1, 1);
    streams_layout->addWidget(bindings_group);
    ui->controls_tabs->addTab(scrollable(streams_page), "Streams");

    // Events Tab
    auto [events_page, events_layout] = makePage();

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
