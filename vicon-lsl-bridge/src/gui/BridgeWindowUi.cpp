#include "gui/BridgeWindowUi.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
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
        control->setToolTip(tooltip);
    }
    return label;
}

} // namespace

std::unique_ptr<BridgeWindowUi> buildBridgeWindowUi(QWidget* window, bool enable_preview) {
    auto ui = std::make_unique<BridgeWindowUi>();

    window->setWindowTitle("Vicon LSL Bridge");
    window->setMinimumWidth(860);

    auto* main_layout = new QVBoxLayout(window);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);
    auto* main_splitter = new QSplitter(Qt::Horizontal);
    main_splitter->setChildrenCollapsible(false);

    auto* controls_tabs = new QTabWidget();
    controls_tabs->setMinimumWidth(440);
    auto* bridge_page = new QWidget();
    auto* left_layout = new QVBoxLayout(bridge_page);
    left_layout->setContentsMargins(6, 6, 6, 6);
    left_layout->setSpacing(8);

    auto* settings_group = new QGroupBox("Connection Settings");
    auto* form = new QFormLayout(settings_group);
    form->setContentsMargins(8, 8, 8, 8);
    form->setVerticalSpacing(4);

    ui->server_edit = new QLineEdit("localhost:801");
    ui->marker_stream_edit = new QLineEdit(vicon_lsl::stream_defaults::ViconMarkers);
    ui->segment_stream_edit = new QLineEdit(vicon_lsl::stream_defaults::ViconSegments);

    form->addRow(makeTooltipLabel(
                     "Vicon server:", ui->server_edit,
                     "Vicon DataStream endpoint, for example localhost:801."),
                 ui->server_edit);
    form->addRow(makeTooltipLabel(
                     "Marker stream:", ui->marker_stream_edit,
                     "LSL stream name for Vicon marker samples."),
                 ui->marker_stream_edit);
    form->addRow(makeTooltipLabel(
                     "Segment stream:", ui->segment_stream_edit,
                     "LSL stream name for Vicon segment samples."),
                 ui->segment_stream_edit);
    left_layout->addWidget(settings_group);

    auto* button_layout = new QHBoxLayout();
    ui->start_button = new QPushButton("Start Streaming");
    ui->stop_button = new QPushButton("Stop");
    ui->stop_button->setEnabled(false);
    button_layout->addWidget(ui->start_button);
    button_layout->addWidget(ui->stop_button);
    left_layout->addLayout(button_layout);

    auto* status_group = new QGroupBox("Status");
    auto* status_layout = new QGridLayout(status_group);
    status_layout->setContentsMargins(8, 8, 8, 8);
    status_layout->setHorizontalSpacing(10);
    status_layout->setVerticalSpacing(4);

    ui->status_label = new QLabel("Disconnected");
    ui->markers_label = new QLabel("0");
    ui->segments_label = new QLabel("0");
    ui->frames_label = new QLabel("0");
    ui->frame_rate_label = new QLabel("0.0 Hz");
    ui->last_error_label = new QLabel("-");
    ui->last_error_label->setWordWrap(true);

    int status_row = 0;
    status_layout->addWidget(new QLabel("Bridge state:"), status_row, 0);
    status_layout->addWidget(ui->status_label, status_row, 1, 1, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Vicon frames:"), status_row, 0);
    status_layout->addWidget(ui->frames_label, status_row, 1);
    status_layout->addWidget(new QLabel("Vicon rate:"), status_row, 2);
    status_layout->addWidget(ui->frame_rate_label, status_row, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Markers:"), status_row, 0);
    status_layout->addWidget(ui->markers_label, status_row, 1);
    status_layout->addWidget(new QLabel("Segments:"), status_row, 2);
    status_layout->addWidget(ui->segments_label, status_row, 3);
    ++status_row;
    status_layout->addWidget(new QLabel("Last error:"), status_row, 0);
    status_layout->addWidget(ui->last_error_label, status_row, 1, 1, 3);
    status_layout->setColumnStretch(1, 1);
    status_layout->setColumnStretch(3, 1);
    left_layout->addWidget(status_group);
    left_layout->addStretch();

    auto* recording_page = new QWidget();
    auto* recording_layout = new QVBoxLayout(recording_page);
    recording_layout->setContentsMargins(6, 6, 6, 6);
    recording_layout->setSpacing(6);
    auto* recording_form = new QFormLayout();
    recording_form->setVerticalSpacing(4);

    auto* root_layout = new QHBoxLayout();
    ui->study_root_edit = new QLineEdit();
    ui->browse_root_button = new QPushButton("Browse");
    root_layout->addWidget(ui->study_root_edit);
    root_layout->addWidget(ui->browse_root_button);

    ui->filename_template_edit = new QLineEdit(
        "sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf");
    ui->participant_edit = new QLineEdit("P001");
    ui->session_edit = new QLineEdit("S001");
    ui->task_edit = new QLineEdit("Task");
    ui->run_spin = new QSpinBox();
    ui->run_spin->setRange(1, 9999);
    ui->run_spin->setValue(1);
    ui->acquisition_edit = new QLineEdit("vicon");
    ui->modality_edit = new QLineEdit("beh");
    ui->filename_preview_label = new QLineEdit();
    ui->filename_preview_label->setReadOnly(true);
    ui->filename_preview_label->setPlaceholderText(
        "Complete the recording fields to preview the output path");

    recording_form->addRow(makeTooltipLabel(
                               "Study root:", ui->study_root_edit,
                               "Directory where LabRecorder stores recordings."),
                           root_layout);
    recording_form->addRow(makeTooltipLabel(
                               "File template:", ui->filename_template_edit,
                               "LabRecorder path template. Tokens: %p participant, %s session, "
                               "%b task/block, %r or %n run, %a acquisition, %m modality."),
                           ui->filename_template_edit);

    auto* metadata_grid = new QGridLayout();
    metadata_grid->setHorizontalSpacing(8);
    metadata_grid->setVerticalSpacing(4);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Participant:", ui->participant_edit,
                                 "Participant identifier substituted for %p."),
                             0, 0);
    metadata_grid->addWidget(ui->participant_edit, 0, 1);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Session:", ui->session_edit,
                                 "Session identifier substituted for %s."),
                             0, 2);
    metadata_grid->addWidget(ui->session_edit, 0, 3);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Task/block:", ui->task_edit,
                                 "Task or block identifier substituted for %b."),
                             1, 0);
    metadata_grid->addWidget(ui->task_edit, 1, 1);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Run:", ui->run_spin,
                                 "Run number substituted for %r."),
                             1, 2);
    metadata_grid->addWidget(ui->run_spin, 1, 3);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Acquisition:", ui->acquisition_edit,
                                 "Acquisition label substituted for %a."),
                             2, 0);
    metadata_grid->addWidget(ui->acquisition_edit, 2, 1);
    metadata_grid->addWidget(makeTooltipLabel(
                                 "Modality:", ui->modality_edit,
                                 "Modality label substituted for %m."),
                             2, 2);
    metadata_grid->addWidget(ui->modality_edit, 2, 3);
    metadata_grid->setColumnStretch(1, 1);
    metadata_grid->setColumnStretch(3, 1);
    auto* metadata_label = new QLabel("Metadata:");
    metadata_label->setToolTip("Values used to expand the filename template tokens.");
    recording_form->addRow(metadata_label, metadata_grid);
    recording_form->addRow("Filename preview:", ui->filename_preview_label);
    recording_layout->addLayout(recording_form);

    auto* labrecorder_form = new QFormLayout();
    labrecorder_form->setVerticalSpacing(4);
    auto* executable_layout = new QHBoxLayout();
    ui->labrecorder_executable_edit = new QLineEdit();
    ui->browse_labrecorder_button = new QPushButton("Browse");
    executable_layout->addWidget(ui->labrecorder_executable_edit);
    executable_layout->addWidget(ui->browse_labrecorder_button);

    ui->labrecorder_host_edit = new QLineEdit("localhost");
    ui->labrecorder_port_spin = new QSpinBox();
    ui->labrecorder_port_spin->setRange(1, 65535);
    ui->labrecorder_port_spin->setValue(22345);

    labrecorder_form->addRow(makeTooltipLabel(
                                  "LabRecorder executable:", ui->labrecorder_executable_edit,
                                  "Optional LabRecorder executable path. Leave blank for the automatic bundled "
                                  "startup; set a path before using Launch LabRecorder."),
                              executable_layout);
    auto* rcs_layout = new QHBoxLayout();
    auto* rcs_host_label = makeTooltipLabel(
        "Host:", ui->labrecorder_host_edit, "LabRecorder remote-control server host.");
    rcs_layout->addWidget(rcs_host_label);
    rcs_layout->addWidget(ui->labrecorder_host_edit, 1);
    auto* rcs_port_label = makeTooltipLabel(
        "Port:", ui->labrecorder_port_spin, "LabRecorder remote-control server TCP port.");
    rcs_layout->addWidget(rcs_port_label);
    rcs_layout->addWidget(ui->labrecorder_port_spin);
    auto* rcs_label = new QLabel("RCS:");
    rcs_label->setToolTip("Host and TCP port for LabRecorder's remote-control server.");
    labrecorder_form->addRow(rcs_label, rcs_layout);
    recording_layout->addLayout(labrecorder_form);

    auto* recording_buttons = new QGridLayout();
    recording_buttons->setHorizontalSpacing(6);
    recording_buttons->setVerticalSpacing(4);
    ui->launch_labrecorder_button = new QPushButton("Launch LabRecorder");
    ui->connect_labrecorder_button = new QPushButton("Connect");
    ui->refresh_streams_button = new QPushButton("Refresh Streams");
    ui->start_recording_button = new QPushButton("Start Recording");
    ui->stop_recording_button = new QPushButton("Stop Recording");
    recording_buttons->addWidget(ui->launch_labrecorder_button, 0, 0);
    recording_buttons->addWidget(ui->connect_labrecorder_button, 0, 1);
    recording_buttons->addWidget(ui->refresh_streams_button, 0, 2);
    recording_buttons->addWidget(ui->start_recording_button, 1, 0, 1, 2);
    recording_buttons->addWidget(ui->stop_recording_button, 1, 2);
    recording_layout->addLayout(recording_buttons);

    ui->labrecorder_status_label = new QLabel("Disconnected");
    ui->labrecorder_status_label->setWordWrap(true);
    recording_layout->addWidget(ui->labrecorder_status_label);
    ui->readiness_label = new QLabel();
    ui->readiness_label->setWordWrap(true);
    recording_layout->addWidget(ui->readiness_label);
    recording_layout->addStretch(1);

    controls_tabs->addTab(bridge_page, "Bridge");
    controls_tabs->addTab(recording_page, "Recording");
    main_splitter->addWidget(controls_tabs);
    if (enable_preview) {
        ui->preview_panel = new vicon_lsl::PreviewPanel();
        main_splitter->addWidget(ui->preview_panel);
    } else {
        main_splitter->addWidget(new QWidget());
    }
    main_splitter->setStretchFactor(0, 0);
    main_splitter->setStretchFactor(1, 1);
    main_splitter->setSizes({500, 1000});
    main_layout->addWidget(main_splitter, 1);

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
        server_edit,
        marker_stream_edit,
        segment_stream_edit,
        study_root_edit,
        filename_template_edit,
        participant_edit,
        session_edit,
        task_edit,
        run_spin,
        acquisition_edit,
        modality_edit,
        labrecorder_executable_edit,
        labrecorder_host_edit,
        labrecorder_port_spin,
    };
    for (const QWidget* control : controls) {
        if (!control || control->toolTip().trimmed().isEmpty()) {
            return false;
        }
    }
    return !preview_panel || preview_panel->configurableTooltipsPresent();
}

} // namespace vicon_lsl::gui_detail
