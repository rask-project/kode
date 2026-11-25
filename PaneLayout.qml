pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Tekst

// A tab's split panes, from its tree of PaneNodes: a SplitView per split, and
// per leaf a slot the leaf's EditorPane is placed in. The panes are not made
// here (TabManager owns them), so rebuilding the layout keeps every document.
Item {
    id: layout

    required property TabManager tabs
    required property Tab tab

    // The leaves shown, for hit-testing a pane drag.
    property list<NodeView> leaves: []

    signal reviewRequested(EditorPane pane, var action)
    signal closeRequested(EditorPane pane)
    // Dragging a pane by its header; positions are global.
    signal paneDragStarted(EditorPane pane, real grabX, real grabY, real headerWidth)
    signal paneDragMoved(real globalX, real globalY)
    signal paneDragFinished(EditorPane pane, real globalX, real globalY)
    signal paneDragCanceled

    // The leaf slot under a global point, or null.
    function leafAtGlobal(globalX: real, globalY: real): NodeView {
        return layout.leaves.find(leaf => {
            if (!leaf.visible)
                return false;
            const point = leaf.mapFromGlobal(globalX, globalY);
            return point.x >= 0 && point.y >= 0 && point.x < leaf.width && point.y < leaf.height;
        }) ?? null;
    }

    Loader {
        property PaneNode node: layout.tab ? layout.tab.root : null

        anchors.fill: parent
        sourceComponent: node ? (node.leaf ? leafComponent : splitComponent) : null
        onLoaded: (item as NodeView).node = node
        // A change can put a node of the same kind under a loaded item.
        onNodeChanged: {
            if (item)
                (item as NodeView).node = node;
        }
    }

    component NodeView: Item {
        property PaneNode node: null
        readonly property EditorPane pane: node && node.leaf ? node.pane as EditorPane : null
    }

    Component {
        id: leafComponent

        NodeView {
            id: leafItem

            property EditorPane placed: null

            // Puts the leaf's pane in this slot (taking back the one shown before).
            function place() {
                if (leafItem.placed && leafItem.placed.parent === leafItem)
                    leafItem.placed.parent = null;
                leafItem.placed = leafItem.pane;
                if (!leafItem.pane)
                    return;
                leafItem.pane.parent = leafItem;
                leafItem.pane.anchors.fill = leafItem;
                leafItem.pane.split = Qt.binding(() => layout.tab !== null && layout.tab.paneCount > 1);
                leafItem.pane.active = Qt.binding(() => layout.tab !== null && layout.tab.activePane === leafItem.pane);
            }

            onPaneChanged: place()
            Component.onCompleted: layout.leaves.push(leafItem)
            Component.onDestruction: {
                const index = layout.leaves.indexOf(leafItem);
                if (index >= 0)
                    layout.leaves.splice(index, 1);
                if (leafItem.placed && leafItem.placed.parent === leafItem)
                    leafItem.placed.parent = null;
                // The pane went away mid-drag.
                if (leafItem.placed && leafItem.placed.dragging)
                    layout.paneDragCanceled();
            }

            Connections {
                target: leafItem.pane

                function onActivated() {
                    layout.tabs.activatePane(leafItem.pane);
                }
                function onReviewRequested(action) {
                    layout.reviewRequested(leafItem.pane, action);
                }
                function onCloseRequested() {
                    layout.closeRequested(leafItem.pane);
                }
                function onSplitRequested(orientation: int) {
                    layout.tabs.split(leafItem.pane, orientation);
                }
                function onDragStarted(grabX: real, grabY: real, headerWidth: real) {
                    layout.paneDragStarted(leafItem.pane, grabX, grabY, headerWidth);
                }
                function onDragMoved(globalX: real, globalY: real) {
                    layout.paneDragMoved(globalX, globalY);
                }
                function onDragFinished(globalX: real, globalY: real) {
                    layout.paneDragFinished(leafItem.pane, globalX, globalY);
                }
                function onDragCanceled() {
                    layout.paneDragCanceled();
                }
            }
        }
    }

    Component {
        id: splitComponent

        NodeView {
            id: splitItem

            SplitView {
                id: splitView

                anchors.fill: parent
                orientation: splitItem.node ? splitItem.node.orientation : Qt.Horizontal

                handle: Item {
                    implicitWidth: 1
                    implicitHeight: 1

                    Rectangle {
                        anchors.fill: parent
                        color: Qt.alpha(dividerPalette.windowText, 0.15)
                    }
                }

                SystemPalette {
                    id: dividerPalette

                    colorGroup: SystemPalette.Active
                }

                Loader {
                    property PaneNode node: splitItem.node ? splitItem.node.first : null

                    sourceComponent: node ? (node.leaf ? leafComponent : splitComponent) : null
                    onLoaded: (item as NodeView).node = node
                    onNodeChanged: {
                        if (item)
                            (item as NodeView).node = node;
                    }

                    SplitView.minimumWidth: 160
                    SplitView.minimumHeight: 100
                    SplitView.preferredWidth: splitView.width / 2
                    SplitView.preferredHeight: splitView.height / 2
                }

                Loader {
                    property PaneNode node: splitItem.node ? splitItem.node.second : null

                    sourceComponent: node ? (node.leaf ? leafComponent : splitComponent) : null
                    onLoaded: (item as NodeView).node = node
                    onNodeChanged: {
                        if (item)
                            (item as NodeView).node = node;
                    }

                    SplitView.minimumWidth: 160
                    SplitView.minimumHeight: 100
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                }
            }
        }
    }
}
