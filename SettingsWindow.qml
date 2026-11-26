pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Rask.Application
import Rask.Controls
import Rask.Platform
import Rask.Updater
import Rask.Window

import Kode

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
            },
            {
                title: qsTr("Files"),
                iconSource: Qt.resolvedUrl("assets/settings/files.svg")
            },
            {
                title: qsTr("Updates"),
                iconSource: Qt.resolvedUrl("assets/settings/updates.svg")
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
                FormRow {
                    label: qsTr("Transparent code area")
                    detail: qsTr("Off: the code on a solid background; the bars stay translucent")

                    Switch {
                        checked: Preferences.transparentEditor
                        onToggled: Preferences.transparentEditor = checked
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
                    detail: qsTr("Light or dark as the appearance")

                    ComboBox {
                        focusPolicy: Qt.TabFocus
                        Layout.preferredWidth: 180
                        model: Syntaxes.themes
                        currentIndex: Syntaxes.themes.indexOf(Syntaxes.themeFamily(Preferences.theme))
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
                FormRow {
                    label: qsTr("Show indent guides")
                    detail: qsTr("A line per indentation level; the block at the caret's stands out")

                    Switch {
                        checked: Preferences.showIndentGuides
                        onToggled: Preferences.showIndentGuides = checked
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
                FormRow {
                    label: qsTr("Re-indent on save")
                    detail: qsTr("By the brackets, as Edit > Re-Indent (not for Python and the like)")

                    Switch {
                        checked: Preferences.reindentOnSave
                        onToggled: Preferences.reindentOnSave = checked
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }

        // Files
        ColumnLayout {
            spacing: 6

            FormGroup {
                FormRow {
                    label: qsTr("Show hidden files")
                    detail: qsTr("In the sidebar's folder: dotfiles and those the Finder hides")
                    showSeparator: false

                    Switch {
                        checked: Preferences.showHiddenFiles
                        onToggled: Preferences.showHiddenFiles = checked
                    }
                }
                FormRow {
                    label: qsTr("Sidebar size")
                    detail: qsTr("The text and icons of the sidebar's files")

                    ComboBox {
                        focusPolicy: Qt.TabFocus
                        Layout.preferredWidth: 120
                        model: [qsTr("Small"), qsTr("Medium"), qsTr("Large")]
                        currentIndex: Preferences.sidebarSize
                        onActivated: index => Preferences.sidebarSize = index
                    }
                }
                FormRow {
                    label: qsTr("Synchronize with editor")
                    detail: qsTr("The sidebar opens the folders down to the document in view")

                    Switch {
                        checked: Preferences.syncWithEditor
                        onToggled: Preferences.syncWithEditor = checked
                    }
                }
            }

            // Read again each time the window shows: the Finder can change them too.
            FormGroup {
                id: integration

                property bool isDefault: false
                property bool commandInstalled: false

                function refresh() {
                    integration.isDefault = DefaultApplication.isDefault("Code");
                    integration.commandInstalled = CommandLineTool.isInstalled("kode");
                }

                Component.onCompleted: refresh()

                Connections {
                    target: root

                    function onVisibleChanged() {
                        if (root.visible)
                            integration.refresh();
                    }
                }

                FormRow {
                    label: qsTr("Default editor")
                    detail: integration.isDefault ? qsTr("Text and code files open with Kode") : qsTr("Open text and code files with Kode on a double click")
                    showSeparator: false

                    Button {
                        focusPolicy: Qt.TabFocus
                        enabled: !integration.isDefault
                        text: integration.isDefault ? qsTr("Default") : qsTr("Make Default")
                        // Launch Services applies it a few seconds later.
                        onClicked: integration.isDefault = DefaultApplication.makeDefault("Code")
                    }
                }
                FormRow {
                    label: qsTr("Command line tool")
                    detail: qsTr("“kode .” or “kode FILE” in the Terminal, as VS Code’s “code”")

                    Button {
                        focusPolicy: Qt.TabFocus
                        enabled: !integration.commandInstalled
                        text: integration.commandInstalled ? qsTr("Installed") : qsTr("Install")
                        onClicked: integration.commandInstalled = CommandLineTool.install("kode")
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }
        // Updates: Sparkle's schedule and the release channel.
        ColumnLayout {
            spacing: 6
            enabled: Updater.available

            FormGroup {
                FormRow {
                    label: qsTr("Check for updates automatically")
                    detail: Updater.lastCheck.getTime() ? qsTr("Last checked %1").arg(Updater.lastCheck.toLocaleString(Qt.locale(), Locale.ShortFormat)) : qsTr("Not checked yet")
                    showSeparator: false

                    Switch {
                        checked: Updater.automaticChecks
                        onToggled: Updater.automaticChecks = checked
                    }
                }
                FormRow {
                    label: qsTr("Download updates automatically")
                    detail: qsTr("Installed when you quit Kode")
                    enabled: Updater.automaticChecks
                    opacity: enabled ? 1 : 0.5

                    Switch {
                        checked: Updater.automaticDownloads
                        onToggled: Updater.automaticDownloads = checked
                    }
                }
                FormRow {
                    label: qsTr("Updates")
                    detail: qsTr("Beta versions bring new features earlier, for testing")

                    ComboBox {
                        focusPolicy: Qt.TabFocus
                        Layout.preferredWidth: 150
                        model: [qsTr("Stable only"), qsTr("Stable and beta")]
                        currentIndex: Updater.betaUpdates ? 1 : 0
                        onActivated: index => Updater.betaUpdates = index === 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                Layout.topMargin: 6

                Label {
                    Layout.fillWidth: true
                    text: Updater.available ? qsTr("Kode %1").arg(Qt.application.version) : qsTr("This build does not update itself.")
                    font.pointSize: 11
                    color: SystemColors.secondaryLabel
                    wrapMode: Text.Wrap
                }

                Button {
                    focusPolicy: Qt.TabFocus
                    text: qsTr("Check Now")
                    onClicked: Updater.checkForUpdates()
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
