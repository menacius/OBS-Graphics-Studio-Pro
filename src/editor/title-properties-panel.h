#pragma once

#include "title-data.h"
#include "title-rich-text.h"

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QListWidget>
#include <QToolButton>
#include <QActionGroup>
#include <QButtonGroup>
#include <QMenu>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QTextEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <QColor>
#include <QPixmap>
#include <QElapsedTimer>
#include <memory>
#include <string>
#include <vector>
#include <set>

class QEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QContextMenuEvent;
class QResizeEvent;
class QPaintEvent;
class QPainter;
class QScrollBar;
class BglCollapsiblePanel;
/* ══════════════════════════════════════════════════════════════════
 *  TitlePropertiesPanel – global title inspector
 * ══════════════════════════════════════════════════════════════════ */
class TitlePropertiesPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit TitlePropertiesPanel(QWidget *parent = nullptr);
    void set_title(std::shared_ptr<Title> t);
    void set_playhead(double timeline_time);

signals:
    void title_changed(bool push_undo_snapshot = true);
    void stinger_structure_changed();
    void stinger_editor_preview_changed();

protected:
    bool event(QEvent *event) override;

private:
    void apply_theme_style();
    void load_values();
    void update_stinger_validation();
    TitleCamera *current_camera();
    const TitleCamera *current_camera() const;
    std::string unique_camera_name(const std::string &base) const;
    void insert_camera_copy(const TitleCamera &source);

    std::shared_ptr<Title> title_;
    bool loading_values_ = false;
    bool numeric_label_dragging_ = false;
    bool applying_theme_style_ = false;
    double playhead_ = 0.0;
    TitleGraphicType previous_non_stinger_graphic_type_ = TitleGraphicType::Title;
    QButtonGroup   *grp_playback_mode_ = nullptr;
    QWidget        *loop_area_row_ = nullptr;
    QComboBox      *cmb_cue_end_behavior_ = nullptr;
    QDoubleSpinBox *spn_pause_frame_ = nullptr;
    QDoubleSpinBox *spn_duration_ = nullptr;
    QDoubleSpinBox *spn_loop_start_ = nullptr;
    QDoubleSpinBox *spn_loop_end_ = nullptr;

    QWidget        *camera_box_ = nullptr;
    BglCollapsiblePanel *camera_panel_ = nullptr;
    QComboBox      *cmb_camera_ = nullptr;
    QPushButton    *btn_camera_add_ = nullptr;
    QPushButton    *btn_camera_delete_ = nullptr;
    QToolButton    *btn_camera_actions_ = nullptr;
    QAction        *act_camera_duplicate_ = nullptr;
    QAction        *act_camera_copy_ = nullptr;
    QAction        *act_camera_paste_ = nullptr;
    std::unique_ptr<TitleCamera> camera_clipboard_;
    QCheckBox      *chk_camera_canvas_default_ = nullptr;
    QComboBox      *cmb_camera_projection_ = nullptr;
    QDoubleSpinBox *spn_camera_pos_x_ = nullptr;
    QDoubleSpinBox *spn_camera_pos_y_ = nullptr;
    QDoubleSpinBox *spn_camera_pos_z_ = nullptr;
    QDoubleSpinBox *spn_camera_target_x_ = nullptr;
    QDoubleSpinBox *spn_camera_target_y_ = nullptr;
    QDoubleSpinBox *spn_camera_target_z_ = nullptr;
    QDoubleSpinBox *spn_camera_orientation_x_ = nullptr;
    QDoubleSpinBox *spn_camera_orientation_y_ = nullptr;
    QDoubleSpinBox *spn_camera_orientation_z_ = nullptr;
    QDoubleSpinBox *spn_camera_rot_x_ = nullptr;
    QDoubleSpinBox *spn_camera_rot_y_ = nullptr;
    QDoubleSpinBox *spn_camera_rot_z_ = nullptr;
    QDoubleSpinBox *spn_camera_focal_ = nullptr;
    QDoubleSpinBox *spn_camera_fov_ = nullptr;
    QDoubleSpinBox *spn_camera_zoom_ = nullptr;
    QDoubleSpinBox *spn_camera_near_ = nullptr;
    QDoubleSpinBox *spn_camera_far_ = nullptr;

    QComboBox      *cmb_stinger_switch_mode_ = nullptr;
    QDoubleSpinBox *spn_stinger_transition_timecode_ = nullptr;
    QCheckBox      *chk_stinger_audio_ = nullptr;
    QCheckBox      *chk_stinger_alpha_ = nullptr;
    QDoubleSpinBox *spn_stinger_pre_roll_ = nullptr;
    QDoubleSpinBox *spn_stinger_post_roll_ = nullptr;
    QComboBox      *cmb_stinger_render_mode_ = nullptr;
    QLabel         *lbl_stinger_validation_ = nullptr;
};


