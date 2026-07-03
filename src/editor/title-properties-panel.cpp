#include "title-editor-internal.h"

TitlePropertiesPanel::TitlePropertiesPanel(QWidget *parent)
    : QGroupBox(parent)
{
    apply_theme_style();

    auto *fl = new QFormLayout(this);
    fl->setContentsMargins(8, 8, 8, 9);
    fl->setHorizontalSpacing(6);
    fl->setVerticalSpacing(4);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fl->setFormAlignment(Qt::AlignTop);
    fl->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto add_form_row = [this](QFormLayout *form, const QString &label_text, QWidget *field) {
        auto *label = new NumericDragLabel(label_text, field, form->parentWidget(),
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit title_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit title_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->addRow(label, field);
    };

    spn_duration_ = new TimecodeSpinBox(this);
    spn_duration_->setRange(0.1, 3600.0);
    spn_duration_->setFixedHeight(22);
    add_form_row(fl, bgl_tr("OBSTitles.LengthLabel"), spn_duration_);

    cmb_cue_end_behavior_ = new QComboBox(this);
    cmb_cue_end_behavior_->addItem(bgl_tr("OBSTitles.CueEndShowLastFrame"), 0);
    cmb_cue_end_behavior_->addItem(bgl_tr("OBSTitles.CueEndShowNothing"), 1);
    cmb_cue_end_behavior_->addItem(bgl_tr("OBSTitles.CueEndShowFirstFrame"), 2);
    cmb_cue_end_behavior_->setToolTip(bgl_tr("OBSTitles.CueEndBehaviorTooltip"));
    cmb_cue_end_behavior_->setFixedHeight(22);
    add_form_row(fl, bgl_tr("OBSTitles.CueEndBehaviorLabel"), cmb_cue_end_behavior_);

    auto *playback_row = new QWidget(this);
    auto *playback_layout = new QHBoxLayout(playback_row);
    playback_layout->setContentsMargins(0, 0, 0, 0);
    playback_layout->setSpacing(3);
    grp_playback_mode_ = new QButtonGroup(playback_row);
    grp_playback_mode_->setExclusive(true);
    auto add_playback_button = [this, playback_row, playback_layout](int id, const char *icon_name,
                                                                     const QString &text) {
        auto *button = new QToolButton(playback_row);
        button->setCheckable(true);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setFixedSize(30, 24);
        grp_playback_mode_->addButton(button, id);
        playback_layout->addWidget(button);
    };
    add_playback_button(0, "play-once.svg", bgl_tr("OBSTitles.PlayOnce"));
    add_playback_button(1, "restart-loop.svg", bgl_tr("OBSTitles.RestartLoop"));
    add_playback_button(2, "ping-pong-loop.svg", bgl_tr("OBSTitles.PingPongLoop"));
    add_playback_button(3, "pause-at-timeline-position.svg", bgl_tr("OBSTitles.PauseAtTimelinePosition"));
    add_playback_button(4, "stinger.svg", bgl_tr("OBSTitles.GraphicTypeStinger"));
    playback_layout->addStretch(1);
    add_form_row(fl, bgl_tr("OBSTitles.PlaybackModeLabel"), playback_row);

    spn_pause_frame_ = new TimecodeSpinBox(this);
    spn_pause_frame_->setRange(0.0, 3600.0);
    spn_pause_frame_->setToolTip(bgl_tr("OBSTitles.PauseFrameTooltip"));
    spn_pause_frame_->setFixedHeight(22);
    add_form_row(fl, bgl_tr("OBSTitles.PauseFrameLabel"), spn_pause_frame_);


    spn_loop_start_ = new TimecodeSpinBox(this);
    spn_loop_start_->setRange(0.0, 3600.0);
    spn_loop_start_->setToolTip(bgl_tr("OBSTitles.LoopStartTooltip"));
    spn_loop_start_->setFixedHeight(22);

    spn_loop_end_ = new TimecodeSpinBox(this);
    spn_loop_end_->setRange(0.0, 3600.0);
    spn_loop_end_->setToolTip(bgl_tr("OBSTitles.LoopEndTooltip"));
    spn_loop_end_->setFixedHeight(22);

    loop_area_row_ = new QWidget(this);
    auto *loop_area_layout = new QHBoxLayout(loop_area_row_);
    loop_area_layout->setContentsMargins(0, 0, 0, 0);
    loop_area_layout->setSpacing(4);
    auto *loop_start_label = new QLabel(bgl_tr("OBSTitles.StartLabel"), loop_area_row_);
    auto *loop_end_label = new QLabel(bgl_tr("OBSTitles.EndLabel"), loop_area_row_);
    spn_loop_start_->setMinimumWidth(78);
    spn_loop_end_->setMinimumWidth(78);
    loop_area_layout->addWidget(loop_start_label);
    loop_area_layout->addWidget(spn_loop_start_, 1);
    loop_area_layout->addWidget(loop_end_label);
    loop_area_layout->addWidget(spn_loop_end_, 1);
    add_form_row(fl, bgl_tr("OBSTitles.LoopAreaLabel"), loop_area_row_);

    cmb_stinger_switch_mode_ = new QComboBox(this);
    cmb_stinger_switch_mode_->addItem(
        bgl_tr("OBSTitles.StingerSwitchAtPoint"),
        static_cast<int>(StingerSwitchMode::SwitchAtPoint));
    cmb_stinger_switch_mode_->addItem(
        bgl_tr("OBSTitles.StingerManualSceneAnimation"),
        static_cast<int>(StingerSwitchMode::ManualSceneAnimation));
    cmb_stinger_switch_mode_->setFixedHeight(22);
    add_form_row(fl, bgl_tr("OBSTitles.StingerSwitchMode"), cmb_stinger_switch_mode_);

    spn_stinger_transition_timecode_ = new TimecodeSpinBox(this);
    spn_stinger_transition_timecode_->setRange(0.0, 3600.0);
    spn_stinger_transition_timecode_->setFixedHeight(22);
    add_form_row(fl, bgl_tr("OBSTitles.StingerTransitionPoint"), spn_stinger_transition_timecode_);

    chk_stinger_audio_ = new QCheckBox(bgl_tr("OBSTitles.StingerOptionalAudio"), this);
    chk_stinger_alpha_ = new QCheckBox(bgl_tr("OBSTitles.StingerAlphaOutput"), this);
    fl->addRow(QString(), chk_stinger_audio_);
    fl->addRow(QString(), chk_stinger_alpha_);

    spn_stinger_pre_roll_ = new TimecodeSpinBox(this);
    spn_stinger_post_roll_ = new TimecodeSpinBox(this);
    spn_stinger_pre_roll_->setRange(0.0, 3600.0);
    spn_stinger_post_roll_->setRange(0.0, 3600.0);
    spn_stinger_pre_roll_->setFixedHeight(22);
    spn_stinger_post_roll_->setFixedHeight(22);
    add_form_row(fl, bgl_tr("OBSTitles.StingerPreRoll"), spn_stinger_pre_roll_);
    add_form_row(fl, bgl_tr("OBSTitles.StingerPostRoll"), spn_stinger_post_roll_);

    cmb_stinger_render_mode_ = new QComboBox(this);
    cmb_stinger_render_mode_->addItem(bgl_tr("OBSTitles.StingerProceduralLive"),
                                      static_cast<int>(StingerRenderMode::ProceduralLive));
    cmb_stinger_render_mode_->addItem(bgl_tr("OBSTitles.StingerPrerenderedProxy"),
                                      static_cast<int>(StingerRenderMode::PrerenderedProxy));
    cmb_stinger_render_mode_->setFixedHeight(22);
    add_form_row(fl, bgl_tr("OBSTitles.StingerRenderMode"), cmb_stinger_render_mode_);

    lbl_stinger_validation_ = new QLabel(this);
    lbl_stinger_validation_->setWordWrap(true);
    lbl_stinger_validation_->setTextFormat(Qt::RichText);
    fl->addRow(bgl_tr("OBSTitles.StingerValidation"), lbl_stinger_validation_);

    connect(grp_playback_mode_, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton *button) {
                if (!title_ || loading_values_ || !button) return;
                const int selection = grp_playback_mode_->id(button);
                if (selection == 4) {
                    const bool entering_stinger = title_->graphic_type != TitleGraphicType::Stinger;
                    if (entering_stinger)
                        previous_non_stinger_graphic_type_ = title_->graphic_type;
                    title_->graphic_type = TitleGraphicType::Stinger;
                    title_->playback_mode = 0;
                    if (entering_stinger) {
                        set_stinger_transition_point_seconds(*title_, title_->duration * 0.5);
                        title_->stinger_alpha_output = true;
                        title_->stinger_render_mode = StingerRenderMode::ProceduralLive;
                        title_->stinger_switch_mode = StingerSwitchMode::SwitchAtPoint;
                        title_->stinger_editor_background = StingerEditorBackground::FollowSwitchPoint;
                        title_->cue_end_behavior = 1;
                    }
                } else {
                    if (title_->graphic_type == TitleGraphicType::Stinger) {
                        title_->graphic_type = previous_non_stinger_graphic_type_ == TitleGraphicType::Stinger
                                                   ? TitleGraphicType::Title
                                                   : previous_non_stinger_graphic_type_;
                    }
                    if (selection == 1 || selection == 2) {
                        title_->playback_mode = 1;
                        title_->loop_type = selection == 2 ? 1 : 0;
                    } else {
                        title_->playback_mode = selection == 3 ? 2 : 0;
                    }
                    if (title_->playback_mode == 2 && title_->pause_time <= 0.0)
                        title_->pause_time = title_->duration;
                }
                load_values();
                emit title_changed(!numeric_label_dragging_);
                emit stinger_structure_changed();
            });

    connect(spn_pause_frame_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->pause_time = std::clamp(v, 0.0, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(cmb_cue_end_behavior_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (!title_ || loading_values_ || !cmb_cue_end_behavior_) return;
                title_->cue_end_behavior = std::clamp(cmb_cue_end_behavior_->currentData().toInt(), 0, 2);
                emit title_changed(!numeric_label_dragging_);
            });


    connect(spn_duration_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                double old_duration = title_->duration;
                title_->duration = v;
                for (auto &layer : title_->layers) {
                    if (std::abs(layer->out_time - old_duration) < 0.001 || layer->out_time > v)
                        layer->out_time = v;
                }
                title_->loop_start = std::clamp(title_->loop_start, 0.0, title_->duration);
                title_->loop_end = std::clamp(title_->loop_end, title_->loop_start, title_->duration);
                title_->pause_time = std::clamp(title_->pause_time, 0.0, title_->duration);
                set_stinger_transition_point_seconds(*title_, title_->stinger_transition_point);
                if (title_->graphic_type == TitleGraphicType::Stinger &&
                    title_->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation)
                    ensure_stinger_transition_input_layers(*title_);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(spn_loop_start_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->loop_start = std::clamp(v, 0.0, title_->duration);
                title_->loop_end = std::clamp(title_->loop_end, title_->loop_start, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(spn_loop_end_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->loop_end = std::clamp(v, title_->loop_start, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(cmb_stinger_switch_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (!title_ || loading_values_ ||
                    title_->graphic_type != TitleGraphicType::Stinger)
                    return;
                title_->stinger_switch_mode = static_cast<StingerSwitchMode>(
                    std::clamp(cmb_stinger_switch_mode_->itemData(index).toInt(), 0, 1));
                if (title_->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation)
                    ensure_stinger_transition_input_layers(*title_);
                load_values();
                emit title_changed(true);
                emit stinger_structure_changed();
            });
    connect(spn_stinger_transition_timecode_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double seconds) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                set_stinger_transition_point_seconds(*title_, seconds);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });
    connect(chk_stinger_audio_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
        title_->stinger_audio_enabled = enabled;
        update_stinger_validation();
        emit title_changed(true);
    });
    connect(chk_stinger_alpha_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
        title_->stinger_alpha_output = enabled;
        update_stinger_validation();
        emit title_changed(true);
    });
    connect(spn_stinger_pre_roll_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double seconds) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                title_->stinger_pre_roll = std::max(0.0, seconds);
                update_stinger_validation();
                emit title_changed(!numeric_label_dragging_);
            });
    connect(spn_stinger_post_roll_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double seconds) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                title_->stinger_post_roll = std::max(0.0, seconds);
                update_stinger_validation();
                emit title_changed(!numeric_label_dragging_);
            });
    connect(cmb_stinger_render_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                title_->stinger_render_mode = static_cast<StingerRenderMode>(
                    std::clamp(cmb_stinger_render_mode_->itemData(index).toInt(), 0, 1));
                update_stinger_validation();
                emit title_changed(true);
            });
}

bool TitlePropertiesPanel::event(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        apply_theme_style();
    return QGroupBox::event(event);
}

void TitlePropertiesPanel::apply_theme_style()
{
    if (applying_theme_style_)
        return;

    const QPalette pal = palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor base = pal.color(QPalette::Base);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor subtle_text = text.lightness() < 128 ? text.lighter(135) : text.darker(135);
    const QColor section_bg = window.lightness() < 128 ? window.lighter(112) : window.darker(104);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(110);

    const QString theme_style = QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;border-top:2px solid %3;margin:0;padding-top:4px;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QDoubleSpinBox,QSpinBox,QComboBox{color:%4;background:%5;border:1px solid %3;"
        "border-radius:2px;padding:1px 3px;selection-background-color:%9;}"
        "QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus{border-color:%9;}"
        "QToolButton{color:%6;background:%7;border:1px solid %3;"
        "border-radius:2px;padding:2px;}"
        "QToolButton:hover{background:%8;border-color:%3;}"
        "QToolButton:checked{background:%9;color:%10;border-color:%9;}"
        "QLabel{color:%11;font-size:10px;background:transparent;}")
        .arg(text.name(QColor::HexRgb),
             section_bg.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             pal.color(QPalette::Text).name(QColor::HexRgb),
             base.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             button.name(QColor::HexRgb),
             hover.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb),
             subtle_text.name(QColor::HexRgb));

    if (styleSheet() == theme_style)
        return;

    applying_theme_style_ = true;
    setStyleSheet(theme_style);
    applying_theme_style_ = false;
}

void TitlePropertiesPanel::set_title(std::shared_ptr<Title> t)
{
    const bool same_title = title_ && t && title_.get() == t.get();
    title_ = t;
    if (!same_title) {
        previous_non_stinger_graphic_type_ =
            title_ && title_->graphic_type != TitleGraphicType::Stinger
                ? title_->graphic_type
                : TitleGraphicType::Title;
    }
    setTitle(QString());

    if (same_title && !loading_values_ &&
        (!title_ || title_->graphic_type != TitleGraphicType::Stinger)) {
        const double duration = title_ ? title_->duration : 5.0;
        const double loop_start = title_ ? title_->loop_start : 1.0;
        const double loop_end = title_ ? title_->loop_end : 4.0;
        const int playback_mode = title_ ? std::clamp(title_->playback_mode, 0, 2) : 0;
        const int loop_type = title_ ? std::clamp(title_->loop_type, 0, 1) : 0;
        const int cue_end_behavior = title_ ? std::clamp(title_->cue_end_behavior, 0, 2) : 0;
        const int playback_selection = playback_mode == 1 ? (loop_type == 1 ? 2 : 1)
                                                           : (playback_mode == 2 ? 3 : 0);
        const double clamped_loop_start = std::clamp(loop_start, 0.0, duration);
        const double clamped_loop_end = std::clamp(loop_end, clamped_loop_start, duration);
        const double pause_time = title_ ? std::clamp(title_->pause_time, 0.0, duration) : 0.0;
        const int cue_index = cmb_cue_end_behavior_ ? cmb_cue_end_behavior_->findData(cue_end_behavior) : -1;
        auto same_value = [](QDoubleSpinBox *spin, double value) {
            return spin && std::abs(spin->value() - value) < 0.000001;
        };

        if (same_value(spn_duration_, duration) &&
            same_value(spn_loop_start_, clamped_loop_start) &&
            same_value(spn_loop_end_, clamped_loop_end) &&
            same_value(spn_pause_frame_, pause_time) &&
            (!grp_playback_mode_ || grp_playback_mode_->checkedId() == playback_selection) &&
            (!cmb_cue_end_behavior_ ||
             cmb_cue_end_behavior_->currentIndex() == (cue_index >= 0 ? cue_index : 0))) {
            return;
        }
    }

    load_values();
}

void TitlePropertiesPanel::load_values()
{
    loading_values_ = true;
    double duration = title_ ? title_->duration : 5.0;
    double loop_start = title_ ? title_->loop_start : 1.0;
    double loop_end = title_ ? title_->loop_end : 4.0;
    int playback_mode = title_ ? std::clamp(title_->playback_mode, 0, 2) : 0;
    int loop_type = title_ ? std::clamp(title_->loop_type, 0, 1) : 0;
    int cue_end_behavior = title_ ? std::clamp(title_->cue_end_behavior, 0, 2) : 0;
    int playback_selection = title_ && title_->graphic_type == TitleGraphicType::Stinger
                                 ? 4
                                 : (playback_mode == 1 ? (loop_type == 1 ? 2 : 1)
                                                       : (playback_mode == 2 ? 3 : 0));
    double pause_time = title_ ? std::clamp(title_->pause_time, 0.0, duration) : 0.0;

    if (auto *button = grp_playback_mode_->button(playback_selection))
        button->setChecked(true);
    spn_duration_->setValue(duration);
    if (cmb_cue_end_behavior_) {
        int idx = cmb_cue_end_behavior_->findData(cue_end_behavior);
        cmb_cue_end_behavior_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    spn_loop_start_->setMaximum(duration);
    spn_loop_end_->setMaximum(duration);
    spn_loop_start_->setValue(std::clamp(loop_start, 0.0, duration));
    spn_loop_end_->setValue(std::clamp(loop_end, std::clamp(loop_start, 0.0, duration), duration));
    spn_pause_frame_->setMaximum(duration);
    spn_pause_frame_->setValue(pause_time);

    bool show_loop = playback_mode == 1;
    bool show_pause = playback_mode == 2;
    auto *form = qobject_cast<QFormLayout *>(layout());
    loop_area_row_->setVisible(show_loop);
    if (form) if (auto *label = qobject_cast<QWidget *>(form->labelForField(loop_area_row_))) label->setVisible(show_loop);
    spn_pause_frame_->setVisible(show_pause);
    if (form) if (auto *label = qobject_cast<QWidget *>(form->labelForField(spn_pause_frame_))) label->setVisible(show_pause);

    const bool is_stinger = title_ && title_->graphic_type == TitleGraphicType::Stinger;
    auto set_form_field_visible = [form](QWidget *field, bool visible) {
        if (!field)
            return;
        field->setVisible(visible);
        if (form) {
            if (auto *label = qobject_cast<QWidget *>(form->labelForField(field)))
                label->setVisible(visible);
        }
    };
    const bool point_switch = is_stinger &&
        title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint;
    set_form_field_visible(cmb_stinger_switch_mode_, is_stinger);
    set_form_field_visible(spn_stinger_transition_timecode_, point_switch);
    set_form_field_visible(chk_stinger_audio_, is_stinger);
    set_form_field_visible(chk_stinger_alpha_, is_stinger);
    set_form_field_visible(spn_stinger_pre_roll_, is_stinger);
    set_form_field_visible(spn_stinger_post_roll_, is_stinger);
    set_form_field_visible(cmb_stinger_render_mode_, is_stinger);
    set_form_field_visible(lbl_stinger_validation_, is_stinger);
    if (is_stinger) {
        const int switch_mode = std::clamp(static_cast<int>(title_->stinger_switch_mode), 0, 1);
        const int switch_index = cmb_stinger_switch_mode_->findData(switch_mode);
        cmb_stinger_switch_mode_->setCurrentIndex(switch_index >= 0 ? switch_index : 0);
        const double point = stinger_transition_point_seconds(*title_);
        spn_stinger_transition_timecode_->setMaximum(title_->duration);
        spn_stinger_transition_timecode_->setValue(point);
        chk_stinger_audio_->setChecked(title_->stinger_audio_enabled);
        chk_stinger_alpha_->setChecked(title_->stinger_alpha_output);
        spn_stinger_pre_roll_->setValue(std::max(0.0, title_->stinger_pre_roll));
        spn_stinger_post_roll_->setValue(std::max(0.0, title_->stinger_post_roll));
        const int render_mode = std::clamp(static_cast<int>(title_->stinger_render_mode), 0, 1);
        const int render_index = cmb_stinger_render_mode_->findData(render_mode);
        cmb_stinger_render_mode_->setCurrentIndex(render_index >= 0 ? render_index : 0);
    }
    loading_values_ = false;
    update_stinger_validation();
}

void TitlePropertiesPanel::update_stinger_validation()
{
    if (!lbl_stinger_validation_)
        return;
    if (!title_ || title_->graphic_type != TitleGraphicType::Stinger) {
        lbl_stinger_validation_->clear();
        return;
    }

    const StingerValidationResult validation = validate_stinger_title(*title_);
    QStringList lines;
    for (const auto &error : validation.errors)
        lines << QStringLiteral("<span style='color:#e35d6a'>● %1</span>")
                     .arg(QString::fromStdString(error).toHtmlEscaped());
    for (const auto &warning : validation.warnings)
        lines << QStringLiteral("<span style='color:#d6a94d'>▲ %1</span>")
                     .arg(QString::fromStdString(warning).toHtmlEscaped());
    if (lines.isEmpty())
        lines << QStringLiteral("<span style='color:#55b87a'>✓ %1</span>")
                     .arg(bgl_tr("OBSTitles.StingerValidationOk").toHtmlEscaped());
    lbl_stinger_validation_->setText(lines.join(QStringLiteral("<br/>")));
}

/* ══════════════════════════════════════════════════════════════════
 *  PropertiesPanel
 * ══════════════════════════════════════════════════════════════════ */
