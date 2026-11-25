pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Tekst

// A window's splits, from its tree of SplitNodes: a SplitView per split, and
// per leaf a slot its EditorPane is placed in. The views are not made here
// (Splits owns them), so rebuilding the layout keeps each caret and scroll.
Item {
    id: layout

    required property Splits splits

    signal documentChosen(EditorPane view, Document document)
    signal reviewRequested(Document document, var action)
    signal closeDocumentRequested(Document document)

    Loader {
        property SplitNode node: layout.splits.root

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
        property SplitNode node: null
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
                        color: Qt.alpha(dividerPalette.windowText, 0.2)
                    }
                }

                SystemPalette {
                    id: dividerPalette

                    colorGroup: SystemPalette.Active
                }

                Loader {
                    property SplitNode node: splitItem.node ? splitItem.node.first : null

                    sourceComponent: node ? (node.leaf ? leafComponent : splitComponent) : null
                    onLoaded: (item as NodeView).node = node
                    onNodeChanged: {
                        if (item)
                            (item as NodeView).node = node;
                    }

                    SplitView.minimumWidth: 200
                    SplitView.minimumHeight: 120
                    SplitView.preferredWidth: splitView.width / 2
                    SplitView.preferredHeight: splitView.height / 2
                }

                Loader {
                    property SplitNode node: splitItem.node ? splitItem.node.second : null

                    sourceComponent: node ? (node.leaf ? leafComponent : splitComponent) : null
                    onLoaded: (item as NodeView).node = node
                    onNodeChanged: {
                        if (item)
                            (item as NodeView).node = node;
                    }

                    SplitView.minimumWidth: 200
                    SplitView.minimumHeight: 120
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                }
            }
        }
    }
}
