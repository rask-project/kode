pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Rask.Controls
import Rask.Platform
import Rask.Window

import Tekst

// The Settings window (⌘,), like macOS System Settings: a blurred sidebar of
// sections and forms bound to Preferences, applied as they change.
Window {
    id: root

    width: 720
    height: 480
    minimumWidth: 720
    maximumWidth: 720
    minimumHeight: 480
    maximumHeight: 480
    // Feeds the Window menu and the window switcher; the section title is drawn by QML.
    title: settings.currentTitle
    // Close and minimize only (no zoom), content under a transparent title bar.
    flags: Qt.Window | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowCloseButtonHint | Qt.WindowMinimizeButtonHint | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
    color: "transparent"

    NativeWindow {
        window: root
        color: Appearance.dark ? "#1e1e1e" : "#e8e8e8"
        alpha: 0.9
    }

    SidebarLayout {
        id: settings

        anchors.fill: parent
        titleBarHeight: Math.max(root.SafeArea.margins.top, 28)
        sections: [
            {
                title: qsTr("Appearance"),
                iconSource: Qt.resolvedUrl("assets/settings/appearance.svg")
            },
            {
                title: qsTr("Editor"),
                iconSource: Qt.resolvedUrl("assets/settings/editor.svg")
            }
        ]

        // Appearance
        ColumnLayout {
            spacing: 6

            FormGroup {
                FormRow {
                    label: qsTr("Appearance")
                    detail: qsTr("Light, dark, or as the system")
                    showSeparator: false

                    ComboBox {
                        // Like an NSPopUpButton: no focus (and focus ring) from a click.
                        focusPolicy: Qt.TabFocus
                        Layout.preferredWidth: 150
                        model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                        currentIndex: Appearance.colorScheme
                        onActivated: index => Appearance.colorScheme = index
                    }
                }
            }

            FormGroup {
                FormRow {
                    label: qsTr("Background blur")
                    detail: qsTr("The desktop shows through the window")
                    showSeparator: false

                    Switch {
                        checked: Preferences.windowBlur
                        onToggled: Preferences.windowBlur = checked
                    }
                }
                FormRow {
                    label: qsTr("Background opacity")

                    Slider {
                        Layout.preferredWidth: 180
                        from: 0.5
                        to: 1.0
                        value: Preferences.windowOpacity
                        onMoved: Preferences.windowOpacity = value
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }

        // Editor
        ColumnLayout {
            spacing: 6

            FormGroup {
                FormRow {
                    label: qsTr("Font")
                    showSeparator: false

                    ComboBox {
                        focusPolicy: Qt.TabFocus
                        Layout.preferredWidth: 180
                        model: Preferences.monospacedFamilies
                        currentIndex: Preferences.monospacedFamilies.indexOf(Preferences.fontFamily)
                        onActivated: index => Preferences.fontFamily = Preferences.monospacedFamilies[index]
                    }
                    SpinBox {
                        from: Preferences.smallestFontSize
                        to: Preferences.largestFontSize
                        value: Preferences.fontSize
                        onValueModified: Preferences.fontSize = value
                    }
                }
                FormRow {
                    label: qsTr("Theme")
                    detail: qsTr("Colors for the syntax")

                    ComboBox {
                        focusPolicy: Qt.TabFocus
                        Layout.preferredWidth: 180
                        model: Syntaxes.themes
                        currentIndex: Syntaxes.themes.indexOf(Preferences.theme !== "" ? Preferences.theme : (Appearance.dark ? "Tekst Dark" : "Tekst Light"))
                        onActivated: index => Preferences.theme = Syntaxes.themes[index]
                    }
                }
                FormRow {
                    label: qsTr("Wrap lines")

                    Switch {
                        checked: Preferences.wrap
                        onToggled: Preferences.wrap = checked
                    }
                }
                FormRow {
                    label: qsTr("Show invisibles")
                    detail: qsTr("Spaces and tabs")

                    Switch {
                        checked: Preferences.showInvisibles
                        onToggled: Preferences.showInvisibles = checked
                    }
                }
            }

            FormGroup {
                FormRow {
                    label: qsTr("Indent using")
                    detail: qsTr("For new documents and files without indentation")
                    showSeparator: false

                    ComboBox {
                        focusPolicy: Qt.TabFocus
                        Layout.preferredWidth: 120
                        model: [qsTr("Spaces"), qsTr("Tabs")]
                        currentIndex: Preferences.indentWithTabs ? 1 : 0
                        onActivated: index => Preferences.indentWithTabs = index === 1
                    }
                }
                FormRow {
                    label: qsTr("Indent width")

                    SpinBox {
                        from: 1
                        to: 16
                        value: Preferences.indentWidth
                        onValueModified: Preferences.indentWidth = value
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
