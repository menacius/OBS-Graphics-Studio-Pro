from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
stack_h = read("src/layers/layer-stack-widget.h")
stack = read("src/layers/layer-stack-widget.cpp")
editor_h = read("src/editor/title-editor.h")
editor_connect = read("src/editor/title-editor/commands-docks.inc")
editor_drop = read("src/editor/title-editor/layout-template-tools.inc")
locale = read("data/locale/en-US.ini")
manifest = json.loads(read("tests/test-suite-manifest.json"))

cmake_version = int(re.search(
    r'OBS_BGS_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_version = int(re.search(
    r'BGL_DEVELOPMENT_VERSION "(\d+)"', build).group(1))
assert cmake_version >= 370
assert build_version >= 370
assert manifest["development_version"] >= 370
assert (
    "tests/development_version_370_layer_drop_color_rows_contract_test.py"
    in manifest["areas"]["editor_gui"]["python"]
)

# Drop intent is structural, not a blind flat QListWidget move. Only Groups
# receive ItemIsDropEnabled and can therefore produce an on-row destination.
for token in (
    "enum class LayerListDropPlacement",
    "Before = 0",
    "After = 1",
    "IntoGroup = 2",
    "void dropEvent(QDropEvent *event) override",
    "dropIndicatorPosition() == QAbstractItemView::OnItem",
    "target->data(kLayerGroupDropTargetRole).toBool()",
    "QListWidget::dropEvent(event);",
    "LayerListDropPlacement::IntoGroup",
    "row_flags &= ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled)",
    "if (l->type == LayerType::Group && !l->locked)",
    "row_flags |= Qt::ItemIsDropEnabled",
):
    assert token in stack
assert "void layer_rows_dropped(" in stack_h
assert "emit layer_rows_dropped(layer_ids, target_id" in stack
assert "Drag between layers to reorder; drop onto an unlocked Group" in locale

# The editor accepts the explicit intent, preserves the visual transform while
# reparenting, and then restores canonical group/model order.
assert "void drop_layer_list_rows(" in editor_h
assert "&LayerStack::layer_rows_dropped" in editor_connect
assert "&TitleEditor::drop_layer_list_rows" in editor_connect
for token in (
    "void TitleEditor::drop_layer_list_rows(",
    "placement != kDropIntoGroup",
    "editor_parenting_would_cycle",
    "editor_reparent_layer_with_parent_bind(",
    "editor_canonical_group_order(title_, scopes)",
    "Children use bottom-to-top model order.",
):
    assert token in editor_drop

# Main layer rows paint their own layer color. Selection is opaque, while the
# unselected state retains the same color at alpha 72. Native OBS/Qt palette
# fills are also cleared from every embedded row control, except popup views,
# so the row surface is visually continuous.
for token in (
    "class LayerRowWidget final : public QWidget",
    'setObjectName(QStringLiteral("layerListColorRow"))',
    "background.setAlpha(item_ && item_->isSelected() ? 255 : 72);",
    "painter.fillRect(rect(), background);",
    "new LayerRowWidget(",
    "item, layer_color(*l, row), list_",
    "void LayerStack::refresh_layer_row_backgrounds()",
    "static void make_layer_row_children_transparent(QWidget *row_widget)",
    "QPalette::Window, QPalette::Base,",
    "QPalette::AlternateBase, QPalette::Button",
    "qobject_cast<QAbstractItemView *>(child)",
    "child->setAutoFillBackground(false);",
    "child_palette.setBrush(group, role, transparent);",
    "make_layer_row_children_transparent(row_widget);",
    'setObjectName(QStringLiteral("layerStack"));',
    'QWidget#layerStack{background:%1;color:%2;}',
    "QComboBox{color:%1;background-color:rgba(0,0,0,0);",
    "QComboBox::drop-down{background-color:rgba(0,0,0,0);",
    "QLineEdit:focus{background-color:rgba(0,0,0,0);",
):
    assert token in stack
assert 'setStyleSheet(QStringLiteral("background:%1;color:%2;")' not in stack

# Keep the requested compact square 2D/3D toggle. Its active state is the
# deliberate exception to row transparency and follows the OBS highlight.
assert "dimension_toggle->setFixedSize(24, 24);" in stack
assert "add_fixed_control_column(dimension_toggle, kLayerDimensionWidth, true);" in stack
assert 'QToolButton[threeD=\\"true\\"]{color:%4;background:%3;border-color:%3;}' in stack
assert "dimension_toggle->setFixedSize(kLayerDimensionWidth, 20);" not in stack

print("Development Version 370 layer drop/color rows contract: PASS")
