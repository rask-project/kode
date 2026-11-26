pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Rask.Window

import Kode

// A window's splits, from its tree of SplitNodes: a SplitView per split, and
// per leaf a slot its EditorPane is placed in. The views are not made here
// (Splits owns them), so rebuilding the layout keeps each caret and scroll.
Item {
    id: layout

    required property Splits splits
    // The title bar's height and the window buttons' right edge: the
    // splits at the top (and left) put their bar there.
    property real topInset: 0
    property real leadingInset: 0
    property NativeWindow nativeWindow: null

    signal documentChosen(EditorPane view, Document document)
    signal reviewRequested(Document document, var action)
    signal closeDocumentRequested(Document document)
    signal newDocumentRequested
    signal openRequested
    signal openFolderRequested
    signal contextMenuRequested

    NodeLoader {
        anchors.fill: parent
        node: layout.splits.root
        atTop: true
        atLeft: true
    }

    // Loads the item for `node`; atTop/atLeft: it touches the window's top (left) edge.
    component NodeLoader: Loader {
        id: nodeLoader

        property SplitNode node: null
        property bool atTop: false
        property bool atLeft: false

        sourceComponent: node ? (node.leaf ? leafComponent : splitComponent) : null
        onLoaded: (item as NodeView).node = node
        // A change can put a node of the same kind under a loaded item.
        onNodeChanged: {
            if (item)
                (item as NodeView).node = node;
        }
    }

    component NodeView: Item {
        property SplitNode node: null
        readonly property NodeLoader slot: parent as NodeLoader
        readonly property bool atTop: slot ? slot.atTop : false
        readonly property bool atLeft: slot ? slot.atLeft : false
    }

    Component {
        id: leafComponent

        NodeView {
            id: leafItem

            readonly property EditorPane view: node && node.leaf ? node.view as EditorPane : null
            property EditorPane placed: null

            // Puts the leaf's view in this slot (taking back the one shown before).
            function place() {
                if (leafItem.placed && leafItem.placed.parent === leafItem)
                    leafItem.placed.parent = null;
                leafItem.placed = leafItem.view;
                if (!leafItem.view)
                    return;
                leafItem.view.parent = leafItem;
                leafItem.view.anchors.fill = leafItem;
                leafItem.view.active = Qt.binding(() => layout.splits.activeView === leafItem.view);
                leafItem.view.removable = Qt.binding(() => layout.splits.count > 1);
                leafItem.view.topInset = Qt.binding(() => leafItem.atTop ? layout.topInset : 0);
                leafItem.view.leadingInset = Qt.binding(() => leafItem.atTop && leafItem.atLeft ? layout.leadingInset : 0);
                leafItem.view.nativeWindow = Qt.binding(() => layout.nativeWindow);
            }

            onViewChanged: place()
            Component.onDestruction: {
                if (leafItem.placed && leafItem.placed.parent === leafItem)
                    leafItem.placed.parent = null;
            }

            Connections {
                target: leafItem.view

                function onActivated() {
                    layout.splits.activate(leafItem.view);
                }
                function onDocumentChosen(document: Document) {
                    layout.documentChosen(leafItem.view, document);
                }
                function onReviewRequested(action) {
                    layout.reviewRequested(leafItem.view.document, action);
                }
                function onCloseDocumentRequested() {
                    layout.closeDocumentRequested(leafItem.view.document);
                }
                function onSplitRequested(orientation: int) {
                    layout.splits.split(leafItem.view, orientation, {
                        document: leafItem.view.document
                    });
                }
                function onRemoveRequested() {
                    layout.splits.remove(leafItem.view);
                }
                function onNewDocumentRequested() {
                    layout.splits.activate(leafItem.view);
                    layout.newDocumentRequested();
                }
                function onOpenRequested() {
                    layout.splits.activate(leafItem.view);
                    layout.openRequested();
                }
                function onOpenFolderRequested() {
                    layout.openFolderRequested();
                }
                function onContextMenuRequested() {
                    layout.contextMenuRequested();
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

                handle: DividerHandle {
                    color: Qt.alpha(dividerPalette.windowText, 0.2)
                }

                SystemPalette {
                    id: dividerPalette

                    colorGroup: SystemPalette.Active
                }

                NodeLoader {
                    node: splitItem.node ? splitItem.node.first : null
                    atTop: splitItem.atTop
                    atLeft: splitItem.atLeft

                    SplitView.minimumWidth: 200
                    SplitView.minimumHeight: 120
                    SplitView.preferredWidth: splitView.width / 2
                    SplitView.preferredHeight: splitView.height / 2
                }

                // Beside the first half it keeps the top; below it, the left.
                NodeLoader {
                    node: splitItem.node ? splitItem.node.second : null
                    atTop: splitItem.atTop && splitView.orientation === Qt.Horizontal
                    atLeft: splitItem.atLeft && splitView.orientation === Qt.Vertical

                    SplitView.minimumWidth: 200
                    SplitView.minimumHeight: 120
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                }
            }
        }
    }
}
