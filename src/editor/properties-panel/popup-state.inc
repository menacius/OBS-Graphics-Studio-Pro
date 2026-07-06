static QString auto_style_marker_label(const std::string &marker, size_t offset, const std::string &custom_chars)
{
    QString name;
    if (marker == "text_start") name = "start";
    else if (marker == "text_end") name = "end";
    else if (marker == "character_index" || marker == "character_count") name = "chars";
    else if (marker == "word_count") name = "words";
    else if (marker == "space") name = "space";
    else if (marker == "line_break") name = "line break";
    else if (marker == "newline") name = "new line";
    else if (marker == "paragraph_start") name = "paragraph start";
    else if (marker == "paragraph_end") name = "paragraph end";
    else if (marker == "custom_char") name = QStringLiteral("'%1'").arg(QString::fromStdString(custom_chars));
    else name = QString::fromStdString(marker);
    if (marker == "text_start" || marker == "text_end")
        return name;
    return QStringLiteral("%1[%2]").arg(name).arg((int)offset);
}


static QString auto_style_visible_token(QString token)
{
    token.replace(QStringLiteral("\\r\\n"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\n"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\r"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\t"), QStringLiteral("tab"));
    token.replace(QStringLiteral("\\x20"), QStringLiteral("space"));
    token.replace(QStringLiteral("\\u2028"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\u2029"), QStringLiteral("paragraph break"));
    token.replace(QRegularExpression(QStringLiteral("\\\\([\\[\\]{}().+*?|^$:-])")), QStringLiteral("\\1"));
    return token.trimmed();
}

static QString auto_style_rule_descriptive_name(const RichTextAutoStyleRule &rule)
{
    if (!rule.display_name.empty())
        return QString::fromStdString(rule.display_name);

    if (rule.condition_type == "regex") {
        const QString pattern = QString::fromStdString(rule.regex_pattern);
        QString beginning = (pattern.startsWith('^') || pattern.contains(QStringLiteral("(?:^|")))
            ? QStringLiteral("Paragraph beginning") : QStringLiteral("Regex match");
        const int lookahead = pattern.lastIndexOf(QStringLiteral("(?="));
        if (lookahead >= 0) {
            const int end = pattern.indexOf(')', lookahead + 3);
            if (end > lookahead) {
                const QString token = auto_style_visible_token(pattern.mid(lookahead + 3, end - lookahead - 3));
                if (!token.isEmpty())
                    return QStringLiteral("%1 to %2").arg(beginning, token);
            }
        }
        return beginning;
    }

    auto start_name = [](const RichTextAutoStyleRule &r) {
        if (r.start_condition == "paragraph_start") return QStringLiteral("Paragraph beginning");
        if (r.start_condition == "text_start") return QStringLiteral("Text beginning");
        if (r.start_condition == "newline" || r.start_condition == "line_break") return QStringLiteral("Line beginning");
        if (r.start_condition == "space") return QStringLiteral("After space");
        if (r.start_condition == "custom_char") return QStringLiteral("After %1").arg(auto_style_visible_token(QString::fromStdString(r.start_custom_chars)));
        if (r.start_condition == "character_index") return QStringLiteral("Character %1").arg((int)r.start_offset);
        if (r.start_condition == "word_count") return QStringLiteral("Word %1").arg((int)r.start_offset);
        return auto_style_marker_label(r.start_condition, r.start_offset, r.start_custom_chars);
    };
    auto end_name = [](const RichTextAutoStyleRule &r) {
        if (r.end_condition == "paragraph_end") return QStringLiteral("paragraph end");
        if (r.end_condition == "text_end") return QStringLiteral("text end");
        if (r.end_condition == "newline" || r.end_condition == "line_break") return QStringLiteral("line break");
        if (r.end_condition == "space") return QStringLiteral("space");
        if (r.end_condition == "custom_char") return auto_style_visible_token(QString::fromStdString(r.end_custom_chars));
        if (r.end_condition == "character_index" || r.end_condition == "character_count") return QStringLiteral("character %1").arg((int)(r.end_offset ? r.end_offset : r.length));
        if (r.end_condition == "word_count") return QStringLiteral("word %1").arg((int)(r.end_offset ? r.end_offset : r.length));
        return auto_style_marker_label(r.end_condition, r.end_offset ? r.end_offset : r.length, r.end_custom_chars);
    };
    return QStringLiteral("%1 to %2").arg(start_name(rule), end_name(rule));
}

static std::vector<std::string> auto_style_split_ids(const QString &text)
{
    std::vector<std::string> ids;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (const QString &part : parts)
        ids.push_back(part.trimmed().toStdString());
    return ids;
}

struct PropertiesColorLibraryColor {
    QString name;
    QColor color;
};

struct PropertiesColorLibrary {
    QString name;
    std::vector<PropertiesColorLibraryColor> colors;
};

static QString properties_color_libraries_path()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/Broadcast Graphics Live");
    QDir dir(base);
    dir.mkpath(QStringLiteral("palettes"));
    return dir.filePath(QStringLiteral("palettes/user-color-libraries.palette.json"));
}

static bool properties_parse_color_hex(QString text, QColor &color)
{
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("#")))
        text.remove(0, 1);
    if (text.size() != 6 && text.size() != 8)
        return false;
    bool ok = false;
    const uint value = text.toUInt(&ok, 16);
    if (!ok)
        return false;
    color = text.size() == 6
        ? QColor((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 255)
        : QColor((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return color.isValid();
}

static void properties_limit_gradient_stop_color_dialog(color_widgets::ColorDialog *dialog)
{
    if (!dialog)
        return;

    dialog->setAlphaEnabled(true);
    dialog->setButtonMode(color_widgets::ColorDialog::Close);
    const QStringList hidden_names = {
        QStringLiteral("wheel"), QStringLiteral("preview"), QStringLiteral("line"),
        QStringLiteral("label"), QStringLiteral("slide_red"), QStringLiteral("spin_red"),
        QStringLiteral("label_2"), QStringLiteral("slide_green"), QStringLiteral("spin_green"),
        QStringLiteral("label_3"), QStringLiteral("slide_blue"), QStringLiteral("spin_blue")
    };
    for (const QString &name : hidden_names) {
        if (auto *widget = dialog->findChild<QWidget *>(name))
            widget->hide();
    }
    if (auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"))) {
        buttons->show();
        for (auto *button : buttons->buttons()) {
            const bool is_picker = buttons->buttonRole(button) == QDialogButtonBox::ActionRole;
            button->setVisible(is_picker);
            if (is_picker)
                button->setToolTip(bgl_tr("OBSTitles.PickColor"));
        }
    }
    dialog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

static std::vector<PropertiesColorLibrary> properties_load_color_libraries()
{
    std::vector<PropertiesColorLibrary> libraries;
    PropertiesColorLibrary open_color;
    open_color.name = QStringLiteral("Open Color");
    for (const auto &entry : bgl_open_color_palette())
        open_color.colors.push_back({entry.first, entry.second});
    libraries.push_back(open_color);

    QFile file(properties_color_libraries_path());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        for (const QJsonValue &value : doc.object().value(QStringLiteral("palettes")).toArray()) {
            const QJsonObject object = value.toObject();
            PropertiesColorLibrary library;
            library.name = object.value(QStringLiteral("name")).toString().trimmed();
            if (library.name.isEmpty())
                continue;
            for (const QJsonValue &color_value : object.value(QStringLiteral("colors")).toArray()) {
                const QJsonObject color_object = color_value.toObject();
                QColor color;
                if (!properties_parse_color_hex(color_object.value(QStringLiteral("hex")).toString(), color))
                    continue;
                library.colors.push_back({color_object.value(QStringLiteral("name")).toString(color.name(QColor::HexRgb)), color});
            }
            libraries.push_back(library);
        }
    }
    return libraries;
}

static QString auto_style_join_ids(const std::vector<std::string> &ids)
{
    QStringList parts;
    for (const auto &id : ids)
        if (!id.empty()) parts << QString::fromStdString(id);
    return parts.join(QStringLiteral(", "));
}


PropertiesPanel::PropertiesPanel(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QPalette pal = qApp->palette();
    const QColor panel_bg = pal.color(QPalette::Window);
    const QColor panel_text = pal.color(QPalette::WindowText);
    const QColor control_bg = pal.color(QPalette::Base);
    const QColor control_text = pal.color(QPalette::Text);
    const QColor button_bg = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor subtle_text = panel_text.lightness() < 128 ? panel_text.lighter(135) : panel_text.darker(135);
    const QColor section_bg = panel_bg.lightness() < 128 ? panel_bg.lighter(112) : panel_bg.darker(104);
    const QColor hover_bg = button_bg.lightness() < 128 ? button_bg.lighter(122) : button_bg.darker(108);
    const QString panel_bg_name = panel_bg.name(QColor::HexRgb);
    const QString panel_text_name = panel_text.name(QColor::HexRgb);
    const QString control_bg_name = control_bg.name(QColor::HexRgb);
    const QString control_text_name = control_text.name(QColor::HexRgb);
    const QString button_bg_name = button_bg.name(QColor::HexRgb);
    const QString button_text_name = button_text.name(QColor::HexRgb);
    const QString border_name = border.name(QColor::HexRgb);
    const QString highlight_name = highlight.name(QColor::HexRgb);
    const QString highlighted_text_name = highlighted_text.name(QColor::HexRgb);
    const QString subtle_text_name = subtle_text.name(QColor::HexRgb);
    const QString section_bg_name = section_bg.name(QColor::HexRgb);
    const QString hover_bg_name = hover_bg.name(QColor::HexRgb);
    setStyleSheet(QStringLiteral("QScrollArea{background:%1;border:none;}").arg(panel_bg_name));

    auto *inner = new QWidget(this);
    inner->setStyleSheet(QStringLiteral("background:%1;").arg(panel_bg_name));
    auto *vl = new QVBoxLayout(inner);
    vl->setContentsMargins(2, 2, 2, 2);
    vl->setSpacing(3);

    const QString section_style =
        QStringLiteral("QGroupBox{color:%1;background:%2;border:none;"
        "border-radius:0;margin-top:14px;font-size:10px;font-weight:bold;}"
        "QGroupBox::title{subcontrol-origin:margin;left:6px;top:2px;padding:0 4px;}"
        "QGroupBox::indicator{width:10px;height:10px;margin-left:2px;}"
        "QLabel{color:%4;font-size:10px;}")
            .arg(panel_text_name)
            .arg(section_bg_name)
            .arg(border_name)
            .arg(subtle_text_name);
    const QString control_style =
        QStringLiteral("QDoubleSpinBox,QSpinBox,QComboBox,QLineEdit,QTextEdit{color:%1;background:%2;"
        "border:1px solid %3;border-radius:2px;padding:1px 3px;selection-background-color:%4;}"
        "QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus,QLineEdit:focus,QTextEdit:focus{border-color:%4;}")
            .arg(control_text_name, control_bg_name, border_name, highlight_name);
    const QString menu_style =
        QStringLiteral("QMenu{color:%1;background:%2;border:1px solid %3;}"
        "QMenu::item{padding:5px 22px;}"
        "QMenu::item:selected{background:%4;color:%5;}"
        "QMenu::item:disabled{color:%6;}")
            .arg(panel_text_name, panel_bg_name, border_name, highlight_name, highlighted_text_name, subtle_text_name);
    const QString themed_dialog_style =
        QStringLiteral("QDialog{background:%1;border:1px solid %2;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}"
        "QToolButton{color:%4;background:%5;border:1px solid %2;border-radius:2px;padding:2px 6px;font-size:10px;}"
        "QToolButton:hover{background:%6;}"
        "QToolButton:checked{background:%7;border-color:%7;color:%8;}"
        "QToolButton:disabled{color:%9;background:%5;border-color:%2;}")
            .arg(panel_bg_name, border_name, panel_text_name, button_text_name, button_bg_name,
                 hover_bg_name, highlight_name, highlighted_text_name, subtle_text_name);

    auto style_form = [](QFormLayout *form) {
        form->setContentsMargins(4, 3, 4, 4);
        form->setHorizontalSpacing(4);
        form->setVerticalSpacing(2);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    };

    auto add_form_row = [this, panel_text_name](QFormLayout *form, const QString &label_text, QWidget *field) {
        if (!form || label_text.isEmpty()) {
            if (form) form->addRow(label_text, field);
            return;
        }

        auto *label = new NumericDragLabel(label_text, field, form->parentWidget(),
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit property_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit property_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(panel_text_name));
        form->addRow(label, field);
    };

    const QString checkbox_style =
        QStringLiteral("QCheckBox{color:%1;font-size:10px;spacing:5px;}"
        "QCheckBox::indicator{width:13px;height:13px;border:1px solid %2;"
        "background:%3;border-radius:2px;}"
        "QCheckBox::indicator:hover{border-color:%2;background:%4;}"
        "QCheckBox::indicator:checked{background:%5;border-color:%5;}")
            .arg(panel_text_name, border_name, button_bg_name, hover_bg_name, highlight_name);
    const QString push_button_style =
        QStringLiteral("QPushButton{color:%1;background:%2;border:1px solid %3;"
        "border-radius:2px;font-size:10px;padding:2px 8px;}"
        "QPushButton:hover{background:%4;border-color:%3;}"
        "QPushButton:pressed{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name, highlight_name, highlighted_text_name);

    auto style_checkbox = [&](QCheckBox *box) {
        box->setFixedHeight(20);
        box->setStyleSheet(checkbox_style);
    };
    auto style_push_button = [&](QPushButton *button) {
        button->setFixedHeight(20);
        button->setStyleSheet(push_button_style);
    };

    auto mk_dspin = [&](double lo, double hi, double step) {
        auto *s = new QDoubleSpinBox(inner);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        s->setFixedHeight(20);
        s->setStyleSheet(control_style);
        return s;
    };

    auto mk_kf_button = [&](const QString &tip) {
        auto *b = new QPushButton(inner);
        b->setFixedSize(20, 20);
        b->setIconSize(QSize(14, 14));
        b->setIcon(keyframe_diamond_icon(false));
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setProperty("active", false);
        b->setProperty("outlined", false);
        const QColor kf_hover = panel_bg.lightness() < 128 ? panel_bg.lighter(128) : panel_bg.darker(108);
        const QColor kf_mark = highlight;
        QColor kf_mark_bg = kf_mark;
        kf_mark_bg.setAlpha(44);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:none;border-radius:2px;padding:0;}"
            "QPushButton:hover{background:%1;}"
            "QPushButton[outlined=\"true\"]{background:%2;}"
            "QPushButton[active=\"true\"]{background:%2;}")
            .arg(kf_hover.name(QColor::HexRgb), kf_mark_bg.name(QColor::HexArgb)));
        return b;
    };

    auto with_kf = [&](QWidget *field, QPushButton *button) {
        auto *row = new QWidget(inner);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(3);
        field->setSizePolicy(QSizePolicy::Expanding, field->sizePolicy().verticalPolicy());
        hl->addWidget(button);
        hl->addWidget(field, 1);
        return row;
    };


    spn_px_      = mk_dspin(-9999, 9999, 1.0);
    spn_py_      = mk_dspin(-9999, 9999, 1.0);
    spn_scale_x_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_y_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_x_->setSuffix("%");
    spn_scale_y_->setSuffix("%");
    spn_transform_size_w_ = mk_dspin(0.0, 9999.0, 10.0);
    spn_transform_size_h_ = mk_dspin(0.0, 9999.0, 10.0);
    chk_scale_lock_ = new TransformLockCheckBox(inner);
    chk_scale_lock_->setChecked(true);
    chk_transform_size_lock_ = new TransformLockCheckBox(inner);
    chk_transform_size_lock_->setChecked(true);
    spn_rot_     = mk_dspin(-9999,  9999,  0.5);
    spn_opacity_ = mk_dspin(0.0,   1.0,  0.01);
    spn_pz_ = mk_dspin(-1000000.0, 1000000.0, 1.0);
    spn_rot_x_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_rot_y_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_scale_z_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_z_->setSuffix("%");
    spn_anchor_z_ = mk_dspin(-1000000.0, 1000000.0, 1.0);
    spn_orientation_x_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_orientation_y_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_orientation_z_ = mk_dspin(-99999.0, 99999.0, 0.5);
    chk_scene_mask_ = new BglSwitch(bgl_tr("OBSTitles.UseAsSceneMask"), inner);
    chk_scene_mask_->setToolTip(bgl_tr("OBSTitles.SceneMaskRenderTooltip"));
    style_checkbox(chk_scene_mask_);
    chk_shape_scale_stroke_ = new BglSwitch(bgl_tr("OBSTitles.ScaleStroke"), inner);
    chk_shape_scale_stroke_->setToolTip(bgl_tr("OBSTitles.ScaleStrokeTooltip"));
    style_checkbox(chk_shape_scale_stroke_);
    chk_shape_scale_corners_ = new BglSwitch(bgl_tr("OBSTitles.ScaleCorners"), inner);
    chk_shape_scale_corners_->setToolTip(bgl_tr("OBSTitles.ScaleCornersTooltip"));
    style_checkbox(chk_shape_scale_corners_);
    spn_origin_x_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_y_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_x_->setDecimals(2);
    spn_origin_y_->setDecimals(2);
    spn_origin_x_->setToolTip(bgl_tr("OBSTitles.OriginXTooltip"));
    spn_origin_y_->setToolTip(bgl_tr("OBSTitles.OriginYTooltip"));
    cmb_anchor_ = new QComboBox(inner);
    for (const QString &label : QStringList{bgl_tr("OBSTitles.TopLeft"), bgl_tr("OBSTitles.TopCenter"), bgl_tr("OBSTitles.TopRight"), bgl_tr("OBSTitles.CenterLeft"), bgl_tr("OBSTitles.Center"), bgl_tr("OBSTitles.CenterRight"), bgl_tr("OBSTitles.BottomLeft"), bgl_tr("OBSTitles.BottomCenter"), bgl_tr("OBSTitles.BottomRight")})
        cmb_anchor_->addItem(label);
    cmb_anchor_->setToolTip(bgl_tr("OBSTitles.AnchorChangeTooltip"));
    cmb_anchor_->setFixedHeight(20);
    cmb_anchor_->setStyleSheet(control_style);

    btn_kf_pos_x_ = mk_kf_button(bgl_tr("OBSTitles.ToggleXKeyframe"));
    btn_kf_pos_y_ = mk_kf_button(bgl_tr("OBSTitles.ToggleYKeyframe"));
    btn_kf_scale_x_ = mk_kf_button(bgl_tr("OBSTitles.ToggleScaleXKeyframe"));
    btn_kf_scale_y_ = mk_kf_button(bgl_tr("OBSTitles.ToggleScaleYKeyframe"));
    btn_kf_transform_size_ = mk_kf_button(bgl_tr("OBSTitles.ToggleWidthKeyframe"));
    btn_kf_transform_size_->setToolTip(bgl_tr("OBSTitles.ToggleSizeKeyframe"));
    btn_kf_rotation_ = mk_kf_button(bgl_tr("OBSTitles.ToggleRotationKeyframe"));
    btn_kf_opacity_ = mk_kf_button(bgl_tr("OBSTitles.ToggleOpacityKeyframe"));
    btn_kf_origin_x_ = mk_kf_button(bgl_tr("OBSTitles.ToggleOriginXKeyframe"));
    btn_kf_origin_y_ = mk_kf_button(bgl_tr("OBSTitles.ToggleOriginYKeyframe"));
    btn_kf_position_z_ = mk_kf_button(QStringLiteral("Toggle Position Z keyframe"));
    btn_kf_rotation_x_ = mk_kf_button(QStringLiteral("Toggle Rotation X keyframe"));
    btn_kf_rotation_y_ = mk_kf_button(QStringLiteral("Toggle Rotation Y keyframe"));
    btn_kf_scale_z_ = mk_kf_button(QStringLiteral("Toggle Scale Z keyframe"));
    btn_kf_anchor_z_ = mk_kf_button(QStringLiteral("Toggle Anchor Z keyframe"));
    btn_kf_orientation_x_ = mk_kf_button(QStringLiteral("Toggle Orientation X keyframe"));
    btn_kf_orientation_y_ = mk_kf_button(QStringLiteral("Toggle Orientation Y keyframe"));
    btn_kf_orientation_z_ = mk_kf_button(QStringLiteral("Toggle Orientation Z keyframe"));
    btn_kf_pos_y_->hide();
    btn_kf_scale_y_->hide();
    btn_kf_opacity_->hide();
    btn_kf_origin_y_->hide();
    btn_kf_position_z_->hide();
    btn_kf_rotation_x_->hide();
    btn_kf_rotation_y_->hide();
    btn_kf_scale_z_->hide();
    btn_kf_anchor_z_->hide();
    btn_kf_orientation_y_->hide();
    btn_kf_orientation_z_->hide();
    spn_opacity_->hide();
    chk_scene_mask_->hide();

    /* ── Transform ── */
    transform_box_ = new QWidget(inner);
    transform_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *tform_layout = new QVBoxLayout(transform_box_);
    tform_layout->setContentsMargins(7, 5, 7, 6);
    tform_layout->setSpacing(8);

    btn_transform_defaults_ = new QPushButton(bgl_tr("OBSTitles.Defaults"), transform_box_);
    btn_transform_defaults_->setFixedHeight(20);
    btn_transform_defaults_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    btn_transform_defaults_->setIconSize(QSize(12, 12));
    btn_transform_defaults_->setToolTip(bgl_tr("OBSTitles.RestoreTransformDefaults"));
    btn_transform_defaults_->setStyleSheet(push_button_style);

    transform_grid_ = new QGridLayout();
    auto *transform_grid = transform_grid_;
    transform_grid->setContentsMargins(0, 2, 0, 0);
    transform_grid->setHorizontalSpacing(5);
    transform_grid->setVerticalSpacing(4);
    transform_grid->setColumnMinimumWidth(0, 20);
    transform_grid->setColumnMinimumWidth(1, 68);
    transform_grid->setColumnMinimumWidth(2, 90);
    transform_grid->setColumnMinimumWidth(3, 90);
    transform_grid->setColumnMinimumWidth(4, 90);
    transform_grid->setColumnMinimumWidth(5, 20);
    transform_grid->setColumnMinimumWidth(6, 0);
    transform_grid->setColumnStretch(7, 1);

    auto style_transform_spin = [&](QDoubleSpinBox *spin) {
        spin->setFixedSize(70, 20);
        spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        spin->setPrefix(QString());
        spin->setStyleSheet(QStringLiteral("QDoubleSpinBox{color:%1;background:transparent;border:none;"
                            "padding:1px 2px;font-size:10px;selection-background-color:%2;}"
                            "QDoubleSpinBox::up-button,QDoubleSpinBox::down-button{width:12px;background:%3;"
                            "border-left:1px solid %4;}"
                            "QDoubleSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover{background:%5;}")
                            .arg(control_text_name, highlight_name, button_bg_name, border_name, hover_bg_name));
    };
    style_transform_spin(spn_px_);
    style_transform_spin(spn_py_);
    style_transform_spin(spn_scale_x_);
    style_transform_spin(spn_scale_y_);
    style_transform_spin(spn_transform_size_w_);
    style_transform_spin(spn_transform_size_h_);
    style_transform_spin(spn_origin_x_);
    style_transform_spin(spn_origin_y_);
    style_transform_spin(spn_rot_);
    style_transform_spin(spn_pz_);
    style_transform_spin(spn_rot_x_);
    style_transform_spin(spn_rot_y_);
    style_transform_spin(spn_scale_z_);
    style_transform_spin(spn_anchor_z_);
    style_transform_spin(spn_orientation_x_);
    style_transform_spin(spn_orientation_y_);
    style_transform_spin(spn_orientation_z_);
    spn_rot_->setPrefix(QString());

    auto make_transform_field = [&](const QString &label, QDoubleSpinBox *spin, const char *icon_name = nullptr) {
        auto *field = new QWidget(transform_box_);
        field->setObjectName(QStringLiteral("OBSTitlesTransformNumericField"));
        field->setStyleSheet(QStringLiteral("QWidget#OBSTitlesTransformNumericField{background:%1;border:1px solid %2;"
                             "border-radius:2px;}").arg(control_bg_name, border_name));
        auto *field_layout = new QHBoxLayout(field);
        field_layout->setContentsMargins(5, 0, 0, 0);
        field_layout->setSpacing(2);
        auto *field_label = new NumericDragLabel(label, spin, field,
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = true;
                                                     emit property_changed(true);
                                                 },
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = false;
                                                     emit property_changed(true);
                                                 });
        field_label->setFixedWidth(14);
        field_label->setAlignment(Qt::AlignCenter);
        if (icon_name && *icon_name) {
            field_label->setPixmap(obs_icon(icon_name).pixmap(14, 14));
            field_label->setText(QString());
        }
        field_label->setStyleSheet(QStringLiteral("QLabel{color:%1;background:transparent;font-size:11px;padding:0;}").arg(subtle_text_name));
        field_layout->addWidget(field_label);
        field_layout->addWidget(spin);
        field->setFixedSize(90, 20);
        return field;
    };

    QWidget *field_pos_x = make_transform_field(bgl_tr("OBSTitles.X"), spn_px_);
    QWidget *field_pos_y = make_transform_field(bgl_tr("OBSTitles.Y"), spn_py_);
    transform_position_field_z_ = make_transform_field(QStringLiteral("Z"), spn_pz_);
    const QString local_position_tooltip = QStringLiteral(
        "Stored and keyframed relative to the effective parent. Root layers "
        "therefore have identical local and world coordinates.");
    spn_px_->setToolTip(local_position_tooltip);
    spn_py_->setToolTip(local_position_tooltip);
    spn_pz_->setToolTip(local_position_tooltip);
    QWidget *field_scale_x = make_transform_field(bgl_tr("OBSTitles.W"), spn_scale_x_);
    QWidget *field_scale_y = make_transform_field(bgl_tr("OBSTitles.H"), spn_scale_y_);
    transform_scale_axis_label_x_ = field_scale_x->findChild<QLabel *>();
    transform_scale_axis_label_y_ = field_scale_y->findChild<QLabel *>();
    transform_scale_field_z_ = make_transform_field(QStringLiteral("Z"), spn_scale_z_);
    QWidget *field_transform_size_w = make_transform_field(bgl_tr("OBSTitles.W"), spn_transform_size_w_);
    QWidget *field_transform_size_h = make_transform_field(bgl_tr("OBSTitles.H"), spn_transform_size_h_);
    QWidget *field_origin_x = make_transform_field(bgl_tr("OBSTitles.X"), spn_origin_x_);
    QWidget *field_origin_y = make_transform_field(bgl_tr("OBSTitles.Y"), spn_origin_y_);
    transform_anchor_field_z_ = make_transform_field(QStringLiteral("Z"), spn_anchor_z_);
    transform_rotation_field_x_ = make_transform_field(QStringLiteral("X"), spn_rot_x_);
    transform_rotation_field_y_ = make_transform_field(QStringLiteral("Y"), spn_rot_y_);
    transform_rotation_field_z_ = make_transform_field(QStringLiteral("Z"), spn_rot_);
    transform_rotation_axis_label_ = transform_rotation_field_z_->findChild<QLabel *>();
    QWidget *field_orientation_x = make_transform_field(QStringLiteral("X"), spn_orientation_x_);
    QWidget *field_orientation_y = make_transform_field(QStringLiteral("Y"), spn_orientation_y_);
    QWidget *field_orientation_z = make_transform_field(QStringLiteral("Z"), spn_orientation_z_);

    /* Anchor preset control lives in the editor dynamic toolbar. */
    btn_anchor_grid_ = nullptr;

    chk_scale_lock_->setText(QString());
    chk_scale_lock_->setToolTip(bgl_tr("OBSTitles.ScaleLock"));
    chk_scale_lock_->setFixedSize(20, 20);
    chk_transform_size_lock_->setText(QString());
    chk_transform_size_lock_->setToolTip(bgl_tr("OBSTitles.LockAspectRatio"));
    chk_transform_size_lock_->setFixedSize(20, 20);

    const QString transform_label_style = QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(panel_text_name);
    auto add_transform_row = [&](int row, QPushButton *kf, const QString &label, QWidget *drag_field, QWidget *first,
                                 QWidget *middle, QWidget *second) {
        transform_grid->addWidget(kf, row, 0, Qt::AlignCenter);
        auto *text = new NumericDragLabel(label, drag_field, transform_box_,
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = true;
                                              emit property_changed(true);
                                          },
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = false;
                                              emit property_changed(true);
                                          });
        text->setStyleSheet(transform_label_style);
        transform_grid->addWidget(text, row, 1, Qt::AlignVCenter);
        transform_grid->addWidget(first, row, 2, Qt::AlignLeft);
        if (middle) transform_grid->addWidget(middle, row, 3, Qt::AlignCenter);
        if (second) transform_grid->addWidget(second, row, 4, Qt::AlignLeft);
        return text;
    };

    add_transform_row(0, btn_kf_pos_x_, QStringLiteral("Local Position"), field_pos_x,
                      field_pos_x, field_pos_y, nullptr);
    transform_grid->addWidget(transform_position_field_z_, 0, 4, Qt::AlignLeft);

    transform_scale_label_ = add_transform_row(1, btn_kf_scale_x_, bgl_tr("OBSTitles.Scale"),
                                               field_scale_x, field_scale_x,
                                               field_scale_y, nullptr);
    transform_scale_field_x_ = field_scale_x;
    transform_scale_field_y_ = field_scale_y;
    transform_grid->addWidget(transform_scale_field_z_, 1, 4, Qt::AlignLeft);
    transform_grid->addWidget(chk_scale_lock_, 1, 5, Qt::AlignCenter);

    transform_size_label_ = add_transform_row(1, btn_kf_transform_size_, bgl_tr("OBSTitles.Size"),
                                              field_transform_size_w, field_transform_size_w,
                                              field_transform_size_h, nullptr);
    transform_grid->addWidget(chk_transform_size_lock_, 1, 4, Qt::AlignCenter);
    transform_size_field_w_ = field_transform_size_w;
    transform_size_field_h_ = field_transform_size_h;

    auto *anchor_label = new NumericDragLabel(bgl_tr("OBSTitles.Anchor"), field_origin_x, transform_box_,
                                              [this]() {
                                                  if (loading_values_) return;
                                                  numeric_label_dragging_ = true;
                                                  emit property_changed(true);
                                              },
                                              [this]() {
                                                  if (loading_values_) return;
                                                  numeric_label_dragging_ = false;
                                                  emit property_changed(true);
                                              });
    anchor_label->setStyleSheet(transform_label_style);
    transform_grid->addWidget(anchor_label, 2, 1, Qt::AlignVCenter);
    transform_grid->addWidget(btn_kf_origin_x_, 2, 0, Qt::AlignCenter);
    transform_grid->addWidget(field_origin_x, 2, 2, Qt::AlignLeft);
    transform_grid->addWidget(field_origin_y, 2, 3, Qt::AlignLeft);
    transform_grid->addWidget(transform_anchor_field_z_, 2, 4, Qt::AlignLeft);

    add_transform_row(3, btn_kf_rotation_, bgl_tr("OBSTitles.Rotation"),
                      transform_rotation_field_z_, transform_rotation_field_x_,
                      transform_rotation_field_y_, nullptr);
    transform_grid->addWidget(transform_rotation_field_z_, 3, 4, Qt::AlignLeft);

    transform_orientation_row_ = new QWidget(transform_box_);
    auto *orientation_layout = new QHBoxLayout(transform_orientation_row_);
    orientation_layout->setContentsMargins(0, 0, 0, 0);
    orientation_layout->setSpacing(5);
    orientation_layout->addWidget(btn_kf_orientation_x_, 0, Qt::AlignCenter);
    auto *orientation_label = new NumericDragLabel(QStringLiteral("Orientation"), field_orientation_x,
                                                    transform_orientation_row_,
                                                    [this]() {
                                                        if (loading_values_) return;
                                                        numeric_label_dragging_ = true;
                                                        emit property_changed(true);
                                                    },
                                                    [this]() {
                                                        if (loading_values_) return;
                                                        numeric_label_dragging_ = false;
                                                        emit property_changed(true);
                                                    });
    orientation_label->setStyleSheet(transform_label_style);
    orientation_label->setFixedWidth(68);
    orientation_layout->addWidget(orientation_label);
    orientation_layout->addWidget(field_orientation_x);
    orientation_layout->addSpacing(0);
    orientation_layout->addWidget(field_orientation_y);
    orientation_layout->addWidget(field_orientation_z);
    orientation_layout->addStretch(1);

    row_shape_scale_options_ = new QWidget(transform_box_);
    row_shape_scale_options_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *shape_scale_options_layout = new QHBoxLayout(row_shape_scale_options_);
    shape_scale_options_layout->setContentsMargins(0, 0, 0, 0);
    shape_scale_options_layout->setSpacing(10);
    shape_scale_options_layout->addWidget(chk_shape_scale_stroke_);
    shape_scale_options_layout->addWidget(chk_shape_scale_corners_);
    shape_scale_options_layout->addStretch(1);

    cmb_anchor_->hide();

    /* The AE-style 2D/3D switch lives at the end of every layer-list row.
     * Keep the combo as an internal synchronization control so older code and
     * keyboard/accessibility paths continue to share one dimension-mode signal. */
    auto *dimension_row = new QWidget(transform_box_);
    auto *dimension_layout = new QHBoxLayout(dimension_row);
    dimension_layout->setContentsMargins(0, 0, 0, 0);
    dimension_layout->setSpacing(8);
    auto *dimension_label = new QLabel(QStringLiteral("Layer Mode"), dimension_row);
    dimension_label->setStyleSheet(transform_label_style);
    cmb_dimension_mode_ = new QComboBox(dimension_row);
    cmb_dimension_mode_->addItem(QStringLiteral("2D Layer"), static_cast<int>(LayerDimensionMode::TwoD));
    cmb_dimension_mode_->addItem(QStringLiteral("3D Layer"), static_cast<int>(LayerDimensionMode::ThreeD));
    cmb_dimension_mode_->setFixedHeight(20);
    cmb_dimension_mode_->setStyleSheet(control_style);
    dimension_layout->addWidget(dimension_label);
    dimension_layout->addWidget(cmb_dimension_mode_, 1);
    dimension_row->hide();

    tform_layout->addLayout(transform_grid);
    tform_layout->addWidget(transform_orientation_row_);
    tform_layout->addWidget(row_shape_scale_options_);

    three_d_controls_ = new QWidget(transform_box_);
    auto *three_d_layout = new QGridLayout(three_d_controls_);
    three_d_layout->setContentsMargins(0, 4, 0, 0);
    three_d_layout->setHorizontalSpacing(5);
    three_d_layout->setVerticalSpacing(6);
    three_d_layout->setColumnMinimumWidth(0, 106);
    three_d_layout->setColumnStretch(2, 1);

    cmb_transform_axis_space_ = new QComboBox(three_d_controls_);
    cmb_transform_axis_space_->addItem(QStringLiteral("Local Axes"), static_cast<int>(TransformAxisSpace::Local));
    cmb_transform_axis_space_->addItem(QStringLiteral("Parent Axes"), static_cast<int>(TransformAxisSpace::Parent));
    cmb_transform_axis_space_->addItem(QStringLiteral("World Axes"), static_cast<int>(TransformAxisSpace::World));
    cmb_transform_axis_space_->setFixedHeight(20);
    cmb_transform_axis_space_->setStyleSheet(control_style);
    cmb_transform_axis_space_->setToolTip(QStringLiteral(
        "Controls the 3D gizmo axis orientation only. Transform values above "
        "are always authored in the layer's local transform relative to its "
        "effective parent."));
    three_d_layout->addWidget(new QLabel(QStringLiteral("Axis Space"), three_d_controls_), 0, 0);
    three_d_layout->addWidget(cmb_transform_axis_space_, 0, 1, 1, 2);

    cmb_layer_camera_ = new QComboBox(three_d_controls_);
    cmb_layer_camera_->setFixedHeight(20);
    cmb_layer_camera_->setStyleSheet(control_style);
    three_d_layout->addWidget(new QLabel(QStringLiteral("Camera"), three_d_controls_), 1, 0);
    three_d_layout->addWidget(cmb_layer_camera_, 1, 1, 1, 2);

    auto *depth_row = new QWidget(three_d_controls_);
    auto *depth_layout = new QGridLayout(depth_row);
    depth_layout->setContentsMargins(0, 0, 0, 0);
    depth_layout->setHorizontalSpacing(10);
    depth_layout->setVerticalSpacing(4);
    chk_depth_test_ = new BglSwitch(QStringLiteral("Depth Test"), depth_row);
    chk_depth_test_->setToolTip(QStringLiteral(
        "Compare this layer against depth already written by compatible 3D layers."));
    chk_write_depth_ = new BglSwitch(QStringLiteral("Write Depth"), depth_row);
    chk_write_depth_->setToolTip(QStringLiteral(
        "Persist this layer's visible pixels in the 3D depth buffer."));
    chk_double_sided_ = new BglSwitch(QStringLiteral("Double-sided"), depth_row);
    chk_double_sided_->setToolTip(QStringLiteral(
        "Render both projected sides of the planar layer."));
    chk_backface_culling_ = new BglSwitch(QStringLiteral("Backface Culling"), depth_row);
    chk_backface_culling_->setToolTip(QStringLiteral(
        "Hide the projected back side using editor/OBS-identical winding."));
    for (auto *check : {chk_depth_test_, chk_write_depth_, chk_double_sided_, chk_backface_culling_})
        style_checkbox(check);
    depth_layout->addWidget(chk_depth_test_, 0, 0);
    depth_layout->addWidget(chk_write_depth_, 0, 1);
    depth_layout->addWidget(chk_double_sided_, 1, 0);
    depth_layout->addWidget(chk_backface_culling_, 1, 1);
    three_d_layout->addWidget(depth_row, 2, 0, 1, 3);
    tform_layout->addWidget(three_d_controls_);

    bgl_add_panel_section(vl, transform_box_, bgl_tr("OBSTitles.Transform"), btn_transform_defaults_);

    /* ── Video Layer ── */
    video_box_ = new QGroupBox(QStringLiteral("Video Layer"), inner);
    video_box_->setStyleSheet(section_style);
    auto *video_form = new QFormLayout(video_box_);
    style_form(video_form);
    /* Video Range is intentionally compact: source path, preview copy, set buttons
     * and feedback text are omitted from the inspector. The two spin boxes keep
     * their In/Out labels as in-field prefixes and stay on one row, matching the
     * compact Transform-property style. */
    spn_video_in_ = mk_dspin(0.0, 86400.0, 0.01);
    spn_video_out_ = mk_dspin(0.0, 86400.0, 0.01);
    spn_video_in_->setDecimals(2);
    spn_video_out_->setDecimals(2);
    spn_video_in_->setPrefix(QStringLiteral("In "));
    spn_video_out_->setPrefix(QStringLiteral("Out "));
    spn_video_in_->setSuffix(QStringLiteral(" s"));
    spn_video_out_->setSuffix(QStringLiteral(" s"));
    auto *video_range_row = new QWidget(video_box_);
    auto *video_range_layout = new QHBoxLayout(video_range_row);
    video_range_layout->setContentsMargins(0, 0, 0, 0);
    video_range_layout->setSpacing(5);
    spn_video_in_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    spn_video_out_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    video_range_layout->addWidget(spn_video_in_, 1);
    video_range_layout->addWidget(spn_video_out_, 1);
    add_form_row(video_form, QStringLiteral("Range"), video_range_row);
    video_box_->setVisible(false);
    bgl_add_panel_section(vl, video_box_);

    auto video_changed = [this](bool commit = false) {
        if (loading_values_ || !title_ || !layer_ || layer_->type != LayerType::Video)
            return;
        synchronize_video_audio_streams(*title_, layer_->id);
        emit audio_property_changed(commit);
        emit property_changed(commit);
    };
    connect(spn_video_in_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, video_changed](double v) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                set_layer_media_range_in_point(*layer_, v);
                {
                    QSignalBlocker blockIn(spn_video_in_);
                    QSignalBlocker blockOut(spn_video_out_);
                    spn_video_in_->setValue(layer_->video_in_point);
                    spn_video_out_->setValue(layer_->video_out_point);
                }
                video_changed(false);
            });
    connect(spn_video_out_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, video_changed](double v) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                set_layer_media_range_out_point(*layer_, v);
                {
                    QSignalBlocker blockIn(spn_video_in_);
                    QSignalBlocker blockOut(spn_video_out_);
                    spn_video_in_->setValue(layer_->video_in_point);
                    spn_video_out_->setValue(layer_->video_out_point);
                }
                video_changed(false);
            });
    /* ── Audio Layer ── */
    audio_box_ = new QGroupBox(bgl_tr("OBSTitles.AudioLayer"), inner);
    audio_box_->setStyleSheet(section_style);
    auto *audio_form = new QFormLayout(audio_box_);
    style_form(audio_form);
    edt_audio_source_ = new QLineEdit(audio_box_);
    edt_audio_source_->setReadOnly(true);
    edt_audio_source_->setFixedHeight(20);
    edt_audio_source_->setStyleSheet(control_style);
    btn_audio_browse_ = new QToolButton(audio_box_);
    btn_audio_browse_->setText(QStringLiteral("…"));
    btn_audio_browse_->setToolTip(QStringLiteral("Choose replacement audio file"));
    auto *source_row = new QWidget(audio_box_);
    auto *source_layout = new QHBoxLayout(source_row);
    source_layout->setContentsMargins(0,0,0,0); source_layout->setSpacing(4);
    source_layout->addWidget(edt_audio_source_, 1); source_layout->addWidget(btn_audio_browse_);

    spn_audio_in_ = mk_dspin(0.0, 86400.0, 0.01); spn_audio_in_->setSuffix(QStringLiteral(" s"));
    spn_audio_out_ = mk_dspin(0.0, 86400.0, 0.01); spn_audio_out_->setSuffix(QStringLiteral(" s"));
    btn_audio_set_in_ = new QPushButton(QStringLiteral("Set In"), audio_box_);
    btn_audio_set_out_ = new QPushButton(QStringLiteral("Set Out"), audio_box_);
    btn_audio_set_in_->setToolTip(QStringLiteral("Set the audio media In point from the current editor playhead."));
    btn_audio_set_out_->setToolTip(QStringLiteral("Set the audio media Out point from the current editor playhead."));
    lbl_audio_range_preview_ = new QLabel(audio_box_);
    lbl_audio_range_preview_->setWordWrap(true);
    lbl_audio_range_preview_->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(panel_text_name));
    lbl_audio_range_feedback_ = new QLabel(audio_box_);
    lbl_audio_range_feedback_->setWordWrap(true);
    lbl_audio_range_feedback_->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(subtle_text_name));
    spn_audio_volume_ = mk_dspin(0.0, 400.0, 1.0); spn_audio_volume_->setSuffix(QStringLiteral(" %"));
    spn_audio_pan_ = mk_dspin(-100.0, 100.0, 1.0); spn_audio_pan_->setSuffix(QStringLiteral(" %"));
    spn_audio_fade_in_ = mk_dspin(0.0, 60.0, 0.01); spn_audio_fade_in_->setSuffix(QStringLiteral(" s"));
    spn_audio_fade_out_ = mk_dspin(0.0, 60.0, 0.01); spn_audio_fade_out_->setSuffix(QStringLiteral(" s"));
    sld_audio_volume_ = new QSlider(Qt::Horizontal, audio_box_); sld_audio_volume_->setRange(0, 400);
    sld_audio_pan_ = new QSlider(Qt::Horizontal, audio_box_); sld_audio_pan_->setRange(-100, 100);
    btn_kf_audio_volume_ = new QToolButton(audio_box_); btn_kf_audio_volume_->setIcon(keyframe_diamond_icon(false));
    btn_kf_audio_pan_ = new QToolButton(audio_box_); btn_kf_audio_pan_->setIcon(keyframe_diamond_icon(false));
    for (auto *b : {btn_kf_audio_volume_, btn_kf_audio_pan_}) {
        b->setAutoRaise(true); b->setFixedSize(24,22); b->setToolTip(QStringLiteral("Toggle keyframe at current time"));
    }
    auto make_audio_pair_row = [this, panel_text_name](const QString &first_label, QDoubleSpinBox *first,
                                                               const QString &second_label, QDoubleSpinBox *second) {
        auto *row = new QWidget(audio_box_);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        auto make_drag_label = [this, panel_text_name, row](const QString &text, QDoubleSpinBox *spin) {
            auto *label = new NumericDragLabel(text, spin, row,
                [this]() {
                    if (loading_values_) return;
                    numeric_label_dragging_ = true;
                    emit audio_property_changed(false);
                },
                [this]() {
                    if (loading_values_) return;
                    numeric_label_dragging_ = false;
                    emit audio_property_changed(true);
                });
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            label->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(panel_text_name));
            return label;
        };
        layout->addWidget(make_drag_label(first_label, first));
        layout->addWidget(first, 1);
        layout->addWidget(make_drag_label(second_label, second));
        layout->addWidget(second, 1);
        return row;
    };
    auto make_slider_row = [this](QWidget *numeric, QWidget *slider, QWidget *kf) {
        auto *row = new QWidget(audio_box_); auto *l = new QHBoxLayout(row);
        l->setContentsMargins(0,0,0,0); l->setSpacing(5); l->addWidget(numeric); l->addWidget(slider,1); l->addWidget(kf); return row;
    };

    cmb_audio_fade_curve_ = new QComboBox(audio_box_);
    cmb_audio_fade_curve_->addItem(QStringLiteral("Linear"), (int)AudioFadeCurve::Linear);
    cmb_audio_fade_curve_->addItem(QStringLiteral("Smooth"), (int)AudioFadeCurve::Smooth);
    cmb_audio_fade_curve_->addItem(QStringLiteral("Equal Power"), (int)AudioFadeCurve::EqualPower);
    cmb_audio_fade_curve_->setFixedHeight(20);
    cmb_audio_fade_curve_->setStyleSheet(control_style);
    cmb_audio_playback_ = new QComboBox(audio_box_);
    cmb_audio_playback_->addItem(QStringLiteral("Play once"), (int)AudioPlaybackMode::PlayOnce);
    cmb_audio_playback_->addItem(QStringLiteral("Loop"), (int)AudioPlaybackMode::Loop);
    cmb_audio_playback_->addItem(QStringLiteral("Pause at end"), (int)AudioPlaybackMode::PauseAtOut);
    cmb_audio_playback_->setFixedHeight(20);
    cmb_audio_playback_->setStyleSheet(control_style);
    chk_audio_mute_ = new BglSwitch(QString(), audio_box_);
    chk_audio_solo_ = new BglSwitch(QString(), audio_box_);
    chk_audio_loop_ = new BglSwitch(QString(), audio_box_);
    chk_audio_independent_ = new BglSwitch(QString(), audio_box_);
    for (auto *box : {chk_audio_mute_, chk_audio_solo_, chk_audio_loop_, chk_audio_independent_}) style_checkbox(box);
    auto *audio_range_tools = new QWidget(audio_box_);
    auto *audio_range_tools_layout = new QHBoxLayout(audio_range_tools);
    audio_range_tools_layout->setContentsMargins(0, 0, 0, 0);
    audio_range_tools_layout->setSpacing(5);
    audio_range_tools_layout->addWidget(btn_audio_set_in_);
    audio_range_tools_layout->addWidget(btn_audio_set_out_);
    audio_range_tools_layout->addWidget(lbl_audio_range_feedback_, 1);
    add_form_row(audio_form, QStringLiteral("Source"), source_row);
    add_form_row(audio_form, QStringLiteral("Preview"), lbl_audio_range_preview_);
    add_form_row(audio_form, QStringLiteral("Range"), make_audio_pair_row(QStringLiteral("In"), spn_audio_in_, QStringLiteral("Out"), spn_audio_out_));
    add_form_row(audio_form, QStringLiteral("Range tools"), audio_range_tools);
    add_form_row(audio_form, QStringLiteral("Volume"), make_slider_row(spn_audio_volume_, sld_audio_volume_, btn_kf_audio_volume_));
    add_form_row(audio_form, QStringLiteral("Pan"), make_slider_row(spn_audio_pan_, sld_audio_pan_, btn_kf_audio_pan_));
    add_form_row(audio_form, QStringLiteral("Fade"), make_audio_pair_row(QStringLiteral("In"), spn_audio_fade_in_, QStringLiteral("Out"), spn_audio_fade_out_));
    add_form_row(audio_form, QStringLiteral("Fade curve"), cmb_audio_fade_curve_);
    add_form_row(audio_form, QStringLiteral("Playback mode"), cmb_audio_playback_);
    add_form_row(audio_form, QStringLiteral("Mute"), chk_audio_mute_);
    add_form_row(audio_form, QStringLiteral("Solo"), chk_audio_solo_);
    add_form_row(audio_form, QStringLiteral("Loop"), chk_audio_loop_);
    add_form_row(audio_form, QStringLiteral("Independent playback"), chk_audio_independent_);
    audio_box_->setVisible(false);
    bgl_add_panel_section(vl, audio_box_);

    auto audio_changed = [this](bool commit = false) {
        if (!loading_values_ && layer_ && layer_->type == LayerType::Audio)
            emit audio_property_changed(commit);
    };
    connect(btn_audio_browse_, &QToolButton::clicked, this, [this, audio_changed]() {
        if (!layer_ || layer_->type != LayerType::Audio) return;
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose audio file"),
            QFileInfo(QString::fromStdString(layer_->audio_source)).absolutePath(),
            QStringLiteral("Audio and video (*.wav *.flac *.mp3 *.aac *.m4a *.mp4 *.mov *.mkv *.webm);;All files (*.*)"));
        if (path.isEmpty()) return;
        layer_->audio_source = path.toStdString();
        layer_->audio_waveform.clear();
        layer_->audio_waveform_duration = 0.0;
        layer_->audio_in_point = 0.0;
        layer_->audio_out_point = std::max(0.0, layer_->out_time - layer_->in_time);
        edt_audio_source_->setText(path);
        {
            QSignalBlocker block_in(spn_audio_in_);
            QSignalBlocker block_out(spn_audio_out_);
            spn_audio_in_->setValue(layer_->audio_in_point);
            spn_audio_out_->setValue(layer_->audio_out_point);
        }
        /* A file replacement changes asset identity, so publish it immediately.
         * The runtime worker will retire the old decode/waveform cache and queue
         * the new asset without waiting for focus loss or editor save. */
        audio_changed(true);
    });
    connect(spn_audio_in_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){
        if (layer_ && !loading_values_ && layer_->type == LayerType::Audio) {
            set_layer_media_range_in_point(*layer_, v);
            QSignalBlocker blockIn(spn_audio_in_);
            QSignalBlocker blockOut(spn_audio_out_);
            spn_audio_in_->setValue(layer_->audio_in_point);
            spn_audio_out_->setValue(layer_->audio_out_point);
            audio_changed(false);
        }
    });
    connect(spn_audio_out_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){
        if (layer_ && !loading_values_ && layer_->type == LayerType::Audio) {
            set_layer_media_range_out_point(*layer_, v);
            QSignalBlocker blockIn(spn_audio_in_);
            QSignalBlocker blockOut(spn_audio_out_);
            spn_audio_in_->setValue(layer_->audio_in_point);
            spn_audio_out_->setValue(layer_->audio_out_point);
            audio_changed(false);
        }
    });
    auto set_audio_range_from_playhead = [this, audio_changed](bool set_in) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Audio) return;
        const double clip_local = std::clamp(playhead_ - layer_->in_time, 0.0,
            std::max(0.0, layer_->out_time - layer_->in_time));
        const double media_time = std::max(0.0, layer_->audio_in_point + clip_local);
        if (set_in)
            set_layer_media_range_in_point(*layer_, media_time);
        else
            set_layer_media_range_out_point(*layer_, media_time);
        {
            QSignalBlocker blockIn(spn_audio_in_);
            QSignalBlocker blockOut(spn_audio_out_);
            if (spn_audio_in_) spn_audio_in_->setValue(layer_->audio_in_point);
            if (spn_audio_out_) spn_audio_out_->setValue(layer_->audio_out_point);
        }
        audio_changed(true);
    };
    connect(btn_audio_set_in_, &QPushButton::clicked, this,
            [set_audio_range_from_playhead]() mutable { set_audio_range_from_playhead(true); });
    connect(btn_audio_set_out_, &QPushButton::clicked, this,
            [set_audio_range_from_playhead]() mutable { set_audio_range_from_playhead(false); });
    connect(spn_audio_volume_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){ if (layer_ && !loading_values_) { layer_->audio_volume=float(v/100.0); set_animated_value(layer_->audio_volume_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), v/100.0); QSignalBlocker b(sld_audio_volume_); sld_audio_volume_->setValue(qRound(v)); audio_changed(false); }});
    connect(sld_audio_volume_, &QSlider::valueChanged, this, [this, audio_changed](int v){ if (layer_ && !loading_values_) { layer_->audio_volume=float(v/100.0); set_animated_value(layer_->audio_volume_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), v/100.0); QSignalBlocker b(spn_audio_volume_); spn_audio_volume_->setValue(v); audio_changed(false); }});
    connect(spn_audio_pan_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){ if (layer_ && !loading_values_) { layer_->audio_pan=float(v/100.0); set_animated_value(layer_->audio_pan_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), v/100.0); QSignalBlocker b(sld_audio_pan_); sld_audio_pan_->setValue(qRound(v)); audio_changed(false); }});
    connect(sld_audio_pan_, &QSlider::valueChanged, this, [this, audio_changed](int v){ if (layer_ && !loading_values_) { layer_->audio_pan=float(v/100.0); set_animated_value(layer_->audio_pan_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), v/100.0); QSignalBlocker b(spn_audio_pan_); spn_audio_pan_->setValue(v); audio_changed(false); }});
    connect(btn_kf_audio_volume_, &QToolButton::clicked, this, [this, audio_changed]() { if (!layer_) return; const double t=std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)); toggle_keyframe(layer_->audio_volume_prop,t,layer_->audio_volume_prop.evaluate(t)); audio_changed(true); });
    connect(btn_kf_audio_pan_, &QToolButton::clicked, this, [this, audio_changed]() { if (!layer_) return; const double t=std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)); toggle_keyframe(layer_->audio_pan_prop,t,layer_->audio_pan_prop.evaluate(t)); audio_changed(true); });
    connect(spn_audio_fade_in_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){ if (layer_ && !loading_values_) { layer_->audio_fade_in=v; audio_changed(false); }});
    connect(spn_audio_fade_out_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){ if (layer_ && !loading_values_) { layer_->audio_fade_out=v; audio_changed(false); }});
    connect(cmb_audio_fade_curve_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, audio_changed](int){ if (layer_ && !loading_values_) { layer_->audio_fade_curve=(AudioFadeCurve)cmb_audio_fade_curve_->currentData().toInt(); audio_changed(true); }});
    connect(cmb_audio_playback_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, audio_changed](int){ if (layer_ && !loading_values_) { layer_->audio_playback_mode=(AudioPlaybackMode)cmb_audio_playback_->currentData().toInt(); audio_changed(true); }});
    connect(chk_audio_mute_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_muted=v; audio_changed(true); }});
    connect(chk_audio_solo_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_solo=v; audio_changed(true); }});
    connect(chk_audio_loop_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_loop=v; audio_changed(true); }});
    connect(chk_audio_independent_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_independent=v; audio_changed(true); }});
    for (auto *spin : {spn_audio_in_, spn_audio_out_, spn_audio_volume_, spn_audio_pan_, spn_audio_fade_in_, spn_audio_fade_out_})
        connect(spin, &QDoubleSpinBox::editingFinished, this, [audio_changed]() { audio_changed(true); });

    /* ── Asset Layer ── */
    asset_box_ = new QGroupBox(bgl_tr("OBSTitles.AssetLayer"), inner);
    asset_box_->setStyleSheet(section_style);
    auto *asset_form = new QFormLayout(asset_box_);
    style_form(asset_form);
    lbl_asset_name_ = new QLabel(asset_box_);
    lbl_asset_name_->setWordWrap(true);
    cmb_asset_playback_ = new QComboBox(asset_box_);
    cmb_asset_playback_->addItem(bgl_tr("OBSTitles.AssetPlaybackSynchronized"), 0);
    cmb_asset_playback_->addItem(bgl_tr("OBSTitles.AssetPlaybackIndependent"), 1);
    cmb_asset_playback_->setFixedHeight(20);
    cmb_asset_playback_->setStyleSheet(control_style);
    spn_asset_offset_ = new QDoubleSpinBox(asset_box_);
    spn_asset_offset_->setRange(-86400.0, 86400.0);
    spn_asset_offset_->setDecimals(3);
    spn_asset_offset_->setSuffix(QStringLiteral(" s"));
    spn_asset_offset_->setFixedHeight(20);
    spn_asset_offset_->setStyleSheet(control_style);
    spn_asset_pause_duration_ = new TimecodeSpinBox(asset_box_);
    spn_asset_pause_duration_->setRange(0.0, 86400.0);
    spn_asset_pause_duration_->setFixedHeight(20);
    spn_asset_pause_duration_->setStyleSheet(control_style);
    spn_asset_loop_count_ = new QSpinBox(asset_box_);
    spn_asset_loop_count_->setRange(1, 1000000);
    spn_asset_loop_count_->setSuffix(bgl_tr("OBSTitles.AssetLoopTimesSuffix"));
    spn_asset_loop_count_->setFixedHeight(20);
    spn_asset_loop_count_->setStyleSheet(control_style);
    chk_asset_loop_ = new BglSwitch(bgl_tr("OBSTitles.AssetLoop"), asset_box_);
    chk_asset_loop_->setStyleSheet(checkbox_style);
    btn_asset_overrides_ = new QPushButton(bgl_tr("OBSTitles.EditAssetOverrides"), asset_box_);
    add_form_row(asset_form, bgl_tr("OBSTitles.AssetSource"), lbl_asset_name_);
    add_form_row(asset_form, bgl_tr("OBSTitles.AssetPlayback"), cmb_asset_playback_);
    add_form_row(asset_form, bgl_tr("OBSTitles.AssetPlaybackOffset"), spn_asset_offset_);
    add_form_row(asset_form, bgl_tr("OBSTitles.AssetPauseFor"), spn_asset_pause_duration_);
    add_form_row(asset_form, bgl_tr("OBSTitles.AssetLoopCount"), spn_asset_loop_count_);
    add_form_row(asset_form, QString(), chk_asset_loop_);
    add_form_row(asset_form, QString(), btn_asset_overrides_);
    asset_box_->setVisible(false);
    bgl_add_panel_section(vl, asset_box_);

    connect(cmb_asset_playback_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_playback_mode = cmb_asset_playback_->currentData().toInt();
                bgs::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                update_asset_playback_controls_visibility();
                emit property_changed(true);
            });
    connect(spn_asset_offset_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_playback_offset = value;
                bgs::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                emit property_changed(true);
            });
    connect(spn_asset_pause_duration_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_pause_duration = value;
                bgs::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                emit property_changed(true);
            });
    connect(spn_asset_loop_count_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_loop_count = value;
                bgs::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                emit property_changed(true);
            });
    connect(chk_asset_loop_, &QCheckBox::toggled, this, [this](bool checked) {
        if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
        layer_->asset_loop = checked;
        bgs::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
        emit property_changed(true);
    });
    connect(btn_asset_overrides_, &QPushButton::clicked, this, [this]() {
        if (layer_ && layer_->type == LayerType::Asset)
            emit asset_overrides_requested(layer_->id);
    });

    /* ── Appearance ── */
    appearance_box_ = new QWidget(inner);
    appearance_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *appearance_layout = new QVBoxLayout(appearance_box_);
    appearance_layout->setContentsMargins(7, 5, 7, 6);
    appearance_layout->setSpacing(8);

    auto *appearance_grid = new QGridLayout();
    appearance_grid->setContentsMargins(0, 4, 0, 0);
    appearance_grid->setHorizontalSpacing(5);
    appearance_grid->setVerticalSpacing(4);
    appearance_grid->setColumnMinimumWidth(0, 24);
    appearance_grid->setColumnMinimumWidth(1, 82);
    appearance_grid->setColumnMinimumWidth(2, 48);
    appearance_grid->setColumnMinimumWidth(3, 32);
    appearance_grid->setColumnMinimumWidth(4, 86);
    appearance_grid->setColumnStretch(5, 1);

    auto make_swatch = [&](const QString &tip) {
        auto *button = new QPushButton(appearance_box_);
        button->setFixedSize(32, 32);
        button->setToolTip(tip);
        button->setText(QString());
        return button;
    };
    btn_appearance_fill_color_ = make_swatch(bgl_tr("OBSTitles.FillColorTooltip"));
    btn_appearance_stroke_color_ = make_swatch(bgl_tr("OBSTitles.OutlineColorLabel"));
    spn_appearance_stroke_width_ = mk_dspin(0.0, 200.0, 1.0);
    spn_appearance_stroke_width_->setFixedSize(86, 24);
    spn_appearance_stroke_width_->setSuffix(QStringLiteral("px"));
    spn_appearance_stroke_width_->setDecimals(0);
    spn_appearance_stroke_width_->setFixedHeight(20);
    spn_appearance_stroke_width_->setStyleSheet(control_style);
    spn_appearance_opacity_ = mk_dspin(0.0, 100.0, 1.0);
    spn_appearance_opacity_->setFixedSize(86, 24);
    spn_appearance_opacity_->setSuffix(QStringLiteral("%"));
    spn_appearance_opacity_->setDecimals(0);
    spn_appearance_opacity_->setFixedHeight(20);
    spn_appearance_opacity_->setStyleSheet(control_style);
    btn_kf_appearance_fill_ = mk_kf_button(bgl_tr("OBSTitles.ToggleFillColorKeyframe"));
    btn_kf_appearance_stroke_ = mk_kf_button(bgl_tr("OBSTitles.OutlineColorLabel"));
    btn_kf_appearance_stroke_->setEnabled(false);
    btn_kf_appearance_opacity_ = btn_kf_opacity_;
    btn_kf_appearance_opacity_->show();
    auto *stroke_options_trigger = new QPushButton(appearance_box_);
    stroke_options_trigger->hide();
    btn_appearance_stroke_label_ = new StrokeOptionsLabel(
        bgl_tr("OBSTitles.Stroke"), spn_appearance_stroke_width_, appearance_box_,
        [stroke_options_trigger]() { stroke_options_trigger->click(); },
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = true;
            emit property_changed(true);
        },
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = false;
            emit property_changed(true);
        });
    btn_appearance_stroke_label_->setStyleSheet(
        QStringLiteral("QLabel{color:%1;background:transparent;text-decoration:underline;font-size:10px;padding:0;}"
        "QLabel:hover{color:%2;}").arg(panel_text_name, highlighted_text_name));

    auto add_appearance_row = [&](int row, QPushButton *kf, const QString &label, QWidget *primary,
                                  QWidget *secondary = nullptr, QWidget *label_widget = nullptr) {
        appearance_grid->addWidget(kf, row, 0, Qt::AlignCenter);
        QWidget *text = label_widget;
        if (!text) {
            text = new NumericDragLabel(label, secondary ? secondary : primary, appearance_box_,
                                        [this]() {
                                            if (loading_values_) return;
                                            numeric_label_dragging_ = true;
                                            emit property_changed(true);
                                        },
                                        [this]() {
                                            if (loading_values_) return;
                                            numeric_label_dragging_ = false;
                                            emit property_changed(true);
                                        });
            text->setStyleSheet(transform_label_style);
        }
        appearance_grid->addWidget(text, row, 1, Qt::AlignVCenter);
        if (primary)
            appearance_grid->addWidget(primary, row, 2, Qt::AlignLeft | Qt::AlignVCenter);
        if (secondary)
            appearance_grid->addWidget(secondary, row, 4, Qt::AlignLeft | Qt::AlignVCenter);
    };

    add_appearance_row(0, btn_kf_appearance_fill_, bgl_tr("OBSTitles.Fill"), btn_appearance_fill_color_);
    add_appearance_row(1, btn_kf_appearance_stroke_, bgl_tr("OBSTitles.Stroke"), btn_appearance_stroke_color_,
                       spn_appearance_stroke_width_, btn_appearance_stroke_label_);
    add_appearance_row(2, btn_kf_appearance_opacity_, bgl_tr("OBSTitles.Opacity"), nullptr,
                       spn_appearance_opacity_);
    appearance_layout->addLayout(appearance_grid);
    bgl_add_panel_section(vl, appearance_box_, bgl_tr("OBSTitles.Appearance"));

    auto make_property_grid = [&](QWidget *parent_widget) {
        auto *grid = new QGridLayout(parent_widget);
        grid->setContentsMargins(6, 5, 6, 6);
        grid->setHorizontalSpacing(5);
        grid->setVerticalSpacing(3);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        return grid;
    };
    auto grid_label = [&](const QString &text, QWidget *parent_widget, QWidget *field = nullptr) {
        auto *label = new NumericDragLabel(text, field, parent_widget,
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit property_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit property_changed(true);
                                           });
        label->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return label;
    };
    auto add_grid_field = [&](QGridLayout *grid, int row, int col, const QString &label_text, QWidget *field) {
        QWidget *parent_widget = grid->parentWidget();
        grid->addWidget(grid_label(label_text, parent_widget, field), row, col * 2);
        grid->addWidget(field, row, col * 2 + 1);
    };
    auto add_full_width_field = [&](QGridLayout *grid, int row, const QString &label_text, QWidget *field) {
        QWidget *parent_widget = grid->parentWidget();
        grid->addWidget(grid_label(label_text, parent_widget, field), row, 0);
        grid->addWidget(field, row, 1, 1, 3);
    };
    auto mk_combo = [&](const QStringList &labels, const QList<int> &values) {
        auto *combo = new QComboBox(inner);
        for (int i = 0; i < labels.size(); ++i)
            combo->addItem(labels[i], i < values.size() ? values[i] : i);
        combo->setFixedHeight(20);
        combo->setStyleSheet(control_style);
        return combo;
    };
    auto mk_type_button = [&](const QString &label, const QString &tip) {
        auto *button = new QToolButton(inner);
        button->setText(label);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFixedSize(28, 22);
        button->setAutoRaise(false);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;"
            "font-size:10px;font-weight:bold;padding:0;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name,
                 highlight_name, highlighted_text_name));
        return button;
    };
    auto mk_type_icon_button = [&](const char *icon_name, const QString &tip) {
        auto *button = mk_type_button(QString(), tip);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(16, 16));
        button->setAccessibleName(tip);
        return button;
    };
    auto mk_paragraph_alignment_button = [&](const char *icon_name, const QString &tip) {
        auto *button = new QToolButton(inner);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(14, 14));
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setCheckable(true);
        button->setFixedSize(30, 24);
        button->setAutoRaise(false);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;padding:2px;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name,
                 highlight_name, highlighted_text_name));
        return button;
    };

    /* ── Character ── */
    text_box_ = new QGroupBox("Character", inner);
    text_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *char_grid = make_property_grid(text_box_);
    char_grid->setContentsMargins(14, 28, 14, 12);
    char_grid->setHorizontalSpacing(5);
    char_grid->setVerticalSpacing(6);

    txt_content_ = new QTextEdit(inner);
    txt_content_->setAcceptRichText(false);
    txt_content_->setMinimumHeight(72);
    txt_content_->setMaximumHeight(92);
    txt_content_->setPlaceholderText(bgl_tr("OBSTitles.EnterTextPlaceholder"));
    txt_content_->setMinimumHeight(42);
    txt_content_->setStyleSheet(control_style);
    btn_text_external_binding_ = new QToolButton(inner);
    btn_text_external_binding_->setText(QStringLiteral("⛓"));
    btn_text_external_binding_->setCheckable(true);
    btn_text_external_binding_->setAutoRaise(false);
    btn_text_external_binding_->setFixedSize(28, 28);
    btn_text_external_binding_->setToolTip(QStringLiteral("Bind text content to external data"));
    btn_text_external_binding_->setStyleSheet(QStringLiteral(
        "QToolButton{border:1px solid palette(mid);border-radius:3px;}"
        "QToolButton:checked{border:2px solid #f0a000;background:rgba(240,160,0,40);}"));

    cmb_font_ = new QComboBox(inner);
    cmb_font_->setFixedHeight(20);
    cmb_font_->setEditable(true);
    cmb_font_->setInsertPolicy(QComboBox::NoInsert);
    cmb_font_->setMaxVisibleItems(24);
    cmb_font_->setStyleSheet(control_style);
    QFontDatabase fdb;
    for (auto &fam : fdb.families())
        cmb_font_->addItem(fam, fam);

    cmb_font_style_ = new QComboBox(inner);
    cmb_font_style_->setFixedHeight(20);
    cmb_font_style_->setStyleSheet(control_style);

    spn_size_ = new QSpinBox(inner);
    spn_size_->setRange(6, 500);
    spn_size_->setFixedHeight(20);
    spn_size_->setStyleSheet(control_style);

