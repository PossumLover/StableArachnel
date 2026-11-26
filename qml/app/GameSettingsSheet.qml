import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs

import Arachnel.Core 1.0
import Qcm.Material as MD

MD.BottomSheet {
    id: root

    sheetType: MD.Enum.BottomSheetModal
    dismissOnDragDown: false

    property string gameId: ""
    property int detailsRevision: 0

    readonly property var info: {
        const _rev = root.detailsRevision
        if (!gameId.length)
            return ({})
        const details = Core.entryDetails(gameId)
        return details || ({})
    }
    readonly property bool playable: Core.isEntryPlayable(gameId)
    readonly property bool installed: {
        if (root.playable)
            return true
        if (!gameId.length)
            return false
        const lib = Core.library.gameInfo(gameId)
        return ((lib.installPath ?? "")).length > 0
    }
    readonly property bool inLibrary: {
        if (!gameId.length)
            return false
        const lib = Core.library.gameInfo(gameId)
        return (lib.gameId ?? "").length > 0
    }
    readonly property bool onLinux: Qt.platform.os === "linux"
    // Hidden rows still bind, and a list can report length with a missing [0].
    readonly property var availableLaunchOptions: {
        if (!root.gameId.length)
            return []
        const raw = Core.gameLaunchOptions(root.gameId)
        if (!raw || raw.length === undefined)
            return []
        const out = []
        for (let i = 0; i < raw.length; ++i) {
            const opt = raw[i]
            if (opt)
                out.push(opt)
        }
        return out
    }
    readonly property var soleLaunchOption: {
        const opts = root.availableLaunchOptions
        if (!opts || opts.length !== 1)
            return null
        return opts[0] || null
    }
    readonly property string currentSelectedLaunchOption: root.info?.selectedLaunchOptionId ?? ""
    readonly property var installedComponents: {
        const _rev = root.detailsRevision
        const raw = root.info?.components
        if (raw === undefined || raw === null)
            return []
        const len = raw.length !== undefined ? raw.length : 0
        const out = []
        for (let i = 0; i < len; ++i) {
            const c = raw[i]
            if (c && c.installed)
                out.push(c)
        }
        return out
    }

    /** Installed Steam/plugin DLC components for this game. */
    readonly property int installedDlcCount: {
        const _rev = root.detailsRevision
        const raw = root.info?.components
        const len = raw && raw.length !== undefined ? raw.length : 0
        let n = 0
        for (let i = 0; i < len; ++i) {
            const c = raw[i]
            if (c && c.installed)
                ++n
        }
        return n
    }
    readonly property bool isInstalling: {
        const job = Core.jobs.jobForEntry(gameId)
        return job.status === "installing"
    }
    readonly property bool readyToInstall: !root.playable
        && (Core.jobs.jobForEntry(gameId).status === "completed")
        && Core.entryDownloadFilesExist(gameId)
    readonly property bool downloadFailed: {
        const job = Core.jobs.jobForEntry(gameId)
        return job.status === "failed" || job.status === "cancelled"
    }
    readonly property bool installFailed: {
        const job = Core.jobs.jobForEntry(gameId)
        return !root.downloadFailed && !!(job.installFailed)
    }
    readonly property string sourceLabel: {
        const sid = info.sourceId ?? ""
        if (!sid.length)
            return ""
        if (info.sourceName && info.sourceName.length && info.sourceName !== sid)
            return info.sourceName
        return Core.sources.nameForId(sid)
    }
    readonly property bool canVerifyFiles: {
        const _rev = root.detailsRevision
        return root.gameId.length > 0 && Core.canVerifyGameFiles(root.gameId)
    }
    readonly property bool verifyingFiles: {
        const _rev = root.detailsRevision
        return root.gameId.length > 0 && Core.isVerifyingGameFiles(root.gameId)
    }
    readonly property bool gameBusy: {
        const _rev = root.detailsRevision
        return !!(Core.jobs.jobForEntry(root.gameId).inProgress)
            || (Core.gameRunning && Core.runningGameId === root.gameId)
            || (Core.runtimeSetupInProgress && Core.runtimeSetupGameId === root.gameId)
    }

    function openForGame(id) {
        installationDetails.checked = false
        runtimePanel.expanded = false
        gameId = id
        if (Core.healInstalledAddons)
            Core.healInstalledAddons(id)
        detailsRevision++
        open()
    }

    function confirmVerify(id, hasUpdate) {
        verifyDialog.entryId = id
        verifyDialog.withUpdate = hasUpdate
        verifyDialog.open()
    }

    Connections {
        target: Core.library
        function onLibraryChanged() { root.detailsRevision++ }
    }

    Connections {
        target: Core.jobs
        function onJobsChanged() { root.detailsRevision++ }
    }

    onOpened: {
        if (root.onLinux)
            Core.refreshAvailableProtons()
        launchArgsField.text = root.info?.launchArgs ?? ""
        exeField.text = root.info?.executableOverride ?? ""
    }

    component SectionTitle: MD.Label {
        Layout.fillWidth: true
        typescale: MD.Token.typescale.title_medium
        color: MD.Token.color.on_surface
    }

    component FieldLabel: MD.Label {
        Layout.fillWidth: true
        typescale: MD.Token.typescale.label_large
    }

    function launchCommand(option) {
        if (!option)
            return ""
        const args = option.arguments
        return [option.executable, Array.isArray(args) ? args.join(" ") : args].filter(Boolean).join(" ")
    }

    ColumnLayout {
        x: MD.Token.spacing.large
        width: root.sheetWidth - 2 * MD.Token.spacing.large
        spacing: MD.Token.spacing.large

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.extra_small
                MD.Label {
                    visible: !!root.info.title
                    text: qsTr("Game settings")
                    typescale: MD.Token.typescale.label_medium
                    color: MD.Token.color.on_surface_variant
                }
                MD.Label {
                    Layout.fillWidth: true
                    text: root.info.title || qsTr("Game settings")
                    typescale: MD.Token.typescale.headline_small
                    wrapMode: Text.WordWrap
                }
            }
            MD.IconButton {
                icon.name: MD.Token.icon.close
                Accessible.name: qsTr("Close")
                MD.ToolTip { visible: parent.hovered; text: qsTr("Close") }
                onClicked: root.close()
            }
        }

        ColumnLayout {
            visible: root.playable
            Layout.fillWidth: true
            spacing: MD.Token.spacing.small
            SectionTitle { text: qsTr("Launch") }
            FieldLabel {
                visible: root.availableLaunchOptions.length > 0
                text: qsTr("Launch mode")
            }
            MD.Label {
                visible: !!root.soleLaunchOption
                text: root.soleLaunchOption?.title || qsTr("Default")
                typescale: MD.Token.typescale.body_medium
            }
            MD.Label {
                Layout.fillWidth: true
                visible: !!root.soleLaunchOption && text.length > 0
                text: root.launchCommand(root.soleLaunchOption)
                wrapMode: Text.WrapAnywhere
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.body_small
            }
            MD.ButtonGroup { id: modeGroup }
            MD.RadioButton {
                Layout.fillWidth: true
                visible: root.availableLaunchOptions.length > 1
                MD.ButtonGroup.group: modeGroup
                text: qsTr("Ask each time")
                checked: root.currentSelectedLaunchOption === ""
                onClicked: Core.setGameSelectedLaunchOption(root.gameId, "")
            }
            Repeater {
                model: root.availableLaunchOptions.length > 1 ? root.availableLaunchOptions : []
                ColumnLayout {
                    id: launchOption
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    MD.RadioButton {
                        Layout.fillWidth: true
                        MD.ButtonGroup.group: modeGroup
                        text: launchOption.modelData.title || qsTr("Option %1").arg(launchOption.modelData.id)
                        checked: root.currentSelectedLaunchOption === launchOption.modelData.id
                        onClicked: Core.setGameSelectedLaunchOption(root.gameId, launchOption.modelData.id)
                    }
                    MD.Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: MD.Token.spacing.extra_large
                        visible: text.length > 0
                        text: root.launchCommand(launchOption.modelData)
                        typescale: MD.Token.typescale.body_small
                        color: MD.Token.color.on_surface_variant
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
            FieldLabel { text: qsTr("Launch arguments") }
            AppTextField {
                id: launchArgsField
                Layout.fillWidth: true
                Accessible.name: qsTr("Launch arguments")
                onEditingFinished: Core.setGameLaunchArgs(root.gameId, text)
            }

            FieldLabel { text: qsTr("Executable") }
            RowLayout {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.small

                AppTextField {
                    id: exeField
                    Accessible.name: qsTr("Executable")
                    Layout.fillWidth: true
                    placeholderText: (root.info.defaultExecutableName ?? "").length > 0
                                     ? qsTr("Default: %1").arg(root.info.defaultExecutableName)
                                     : qsTr("Custom executable (optional)")
                    onEditingFinished: Core.setGameExecutableOverride(root.gameId, text)
                }

                MD.IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    mdState.type: MD.Enum.IBtStandard
                    icon.name: MD.Token.icon.folder_open
                    Accessible.name: qsTr("Browse")
                    onClicked: {
                        const path = Core.browseGameExecutable(
                            exeField.text, root.info.installPath || "")
                        if (path.length) {
                            exeField.text = path
                            Core.setGameExecutableOverride(root.gameId, path)
                        }
                    }
                }
            }

            MD.Label {
                Layout.fillWidth: true
                visible: (root.info.defaultExecutable ?? "").length > 0 && !exeField.text.length
                text: qsTr("Default executable: %1").arg(root.info.defaultExecutable)
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.body_small
                wrapMode: Text.WrapAnywhere
            }
        }

        MD.Divider { Layout.fillWidth: true; visible: root.playable && compatibility.visible }
        ColumnLayout {
            id: compatibility
            visible: root.installed || (root.onLinux && root.gameId.length > 0)
            Layout.fillWidth: true
            spacing: MD.Token.spacing.small
            SectionTitle { text: qsTr("Compatibility") }
            RowLayout {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.medium
                visible: root.installed && !!(root.info.onlineFixCanToggle)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.extra_small

                    MD.Label {
                        Layout.fillWidth: true
                        text: qsTr("Online Fix for this game")
                        typescale: MD.Token.typescale.body_large
                    }

                    MD.Label {
                        Layout.fillWidth: true
                        text: root.info.onlineFixKind === "steamfix"
                              ? qsTr("Using SteamFix.")
                              : root.info.onlineFixKind === "onlinefix" ? qsTr("Using Online Fix.")
                              : qsTr("Runs without a Steam license.")
                        color: MD.Token.color.on_surface_variant
                        typescale: MD.Token.typescale.body_small
                        wrapMode: Text.WordWrap
                    }
                }

                MD.Switch {
                    Accessible.name: qsTr("Online Fix for this game")
                    checked: !!(root.info.onlineFixEnabled)
                    onToggled: {
                        Core.setGameOnlineFixEnabled(root.gameId, checked)
                        root.detailsRevision++
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.medium
                visible: root.installed && !!(root.info.canSwitchToSteamFix)

                MD.Label {
                    Layout.fillWidth: true
                    text: qsTr("If the game won't start, hangs, or Online Fix shows an error, try SteamFix instead. Online Fix is backed up.")
                    color: MD.Token.color.on_surface_variant
                    typescale: MD.Token.typescale.body_small
                    wrapMode: Text.WordWrap
                }

                MD.Button {
                    mdState.type: MD.Enum.BtFilledTonal
                    text: qsTr("Use SteamFix")
                    onClicked: {
                        Core.switchGameToSteamFix(root.gameId)
                        root.detailsRevision++
                    }
                }
            }

            MD.Button {
                mdState.type: MD.Enum.BtFilledTonal
                visible: root.installed
                enabled: !root.gameBusy && !root.verifyingFiles && !root.info.installUnavailable
                text: root.info.onlineFixPackageBackup ? qsTr("Restore pre-import files")
                                                       : qsTr("Import Online Fix package")
                onClicked: {
                    packageDialog.entryId = root.gameId
                    packageDialog.gameTitle = root.info.title ?? root.gameId
                    packageDialog.restore = !!root.info.onlineFixPackageBackup
                    if (packageDialog.restore)
                        packageDialog.open()
                    else
                        packagePicker.open()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.medium
                visible: root.installed && !!(root.info.steamOverlayCanToggle)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.extra_small

                    MD.Label {
                        Layout.fillWidth: true
                        text: qsTr("Steam overlay (shift+tab)")
                        typescale: MD.Token.typescale.body_large
                    }

                    MD.Label {
                        Layout.fillWidth: true
                        text: qsTr("Runs inside the Steam Linux Runtime with Valve's overlay loaded. Needs the Steam client running.")
                        color: MD.Token.color.on_surface_variant
                        typescale: MD.Token.typescale.body_small
                        wrapMode: Text.WordWrap
                    }
                }

                MD.Switch {
                    Accessible.name: qsTr("Steam overlay (shift+tab)")
                    checked: !!(root.info.steamOverlayForced)
                    onToggled: {
                        Core.setGameSteamOverlayForced(root.gameId, checked)
                        root.detailsRevision++
                    }
                }
            }

            ColumnLayout {
                visible: root.onLinux && root.gameId.length > 0
                Layout.fillWidth: true
                spacing: MD.Token.spacing.small
                MD.Label {
                    text: qsTr("Proton")
                    typescale: MD.Token.typescale.title_small
                }

                MD.Label {
                    Layout.fillWidth: true
                    text: qsTr("Override Proton for this game. Default uses Settings → Launch.")
                    color: MD.Token.color.on_surface_variant
                    typescale: MD.Token.typescale.body_small
                    wrapMode: Text.WordWrap
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.small

                    MD.FilterChip {
                        text: qsTr("Default")
                        checked: !(root.info.protonId ?? "").length
                        onClicked: Core.setGameProtonId(root.gameId, "")
                    }

                    Repeater {
                        model: Core.availableProtons

                        MD.FilterChip {
                            required property var modelData

                            text: modelData.name
                            checked: (root.info.protonId ?? "") === modelData.id
                            onClicked: Core.setGameProtonId(root.gameId, modelData.id)
                        }
                    }
                }
            }
            GameSettingsRuntimePanel {
                id: runtimePanel
                Layout.fillWidth: true
                page: root
            }
        }

        MD.Divider { Layout.fillWidth: true }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: MD.Token.spacing.small
            SectionTitle { text: qsTr("Files and updates") }
            RowLayout {
                Layout.fillWidth: true
                visible: root.installed
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.extra_small
                    MD.Label {
                        text: qsTr("Automatic updates")
                        typescale: MD.Token.typescale.body_large
                    }
                    MD.Label {
                        Layout.fillWidth: true
                        text: qsTr("Updates start after the catalog loads.")
                        color: MD.Token.color.on_surface_variant
                        typescale: MD.Token.typescale.body_small
                        wrapMode: Text.WordWrap
                    }
                }
                MD.Switch {
                    Accessible.name: qsTr("Automatic updates")
                    checked: root.info.autoUpdate !== false
                    onToggled: Core.setGameAutoUpdate(root.gameId, checked)
                }
            }
            MD.Label {
                Layout.fillWidth: true
                Layout.topMargin: MD.Token.spacing.small
                visible: root.installed
                text: {
                    if (root.verifyingFiles) return qsTr("Verifying files")
                    if (Core.gameRunning && Core.runningGameId === root.gameId) return qsTr("Stop the game to verify its files.")
                    if (Core.runtimeSetupInProgress && Core.runtimeSetupGameId === root.gameId) return qsTr("Wait for setup to finish.")
                    if (root.gameBusy) return qsTr("Wait for the current task to finish.")
                    if (root.info.installUnavailable) return qsTr("Game folder unavailable.")
                    if (!root.canVerifyFiles) return qsTr("Verification is unavailable for this installation.")
                    return qsTr("Repair missing or damaged files.")
                }
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.body_small
                wrapMode: Text.WordWrap
            }
            Flow {
                Layout.fillWidth: true
                visible: root.installed
                spacing: MD.Token.spacing.small
                MD.Button {
                    mdState.type: MD.Enum.BtFilledTonal
                    text: qsTr("Verify files")
                    icon.name: MD.Token.icon.fact_check
                    enabled: root.canVerifyFiles && !root.gameBusy && !root.verifyingFiles
                    onClicked: root.confirmVerify(root.gameId, !!root.info.hasUpdate)
                }
                MD.Button {
                    mdState.type: MD.Enum.BtText
                    text: qsTr("Open game folder")
                    icon.name: MD.Token.icon.folder_open
                    onClicked: Core.openGameFolder(root.gameId)
                }
            }
            MD.Button {
                id: installationDetails
                mdState.type: MD.Enum.BtText
                text: qsTr("Installation details")
                icon.name: checked ? MD.Token.icon.expand_less : MD.Token.icon.expand_more
                checkable: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: installationDetails.checked
                spacing: MD.Token.spacing.small
                Repeater {
                    model: [
                        { label: qsTr("Source"), value: root.sourceLabel },
                        { label: qsTr("Version"), value: root.info.version ?? "" },
                        { label: qsTr("Size"), value: root.info.sizeLabel || "-" },
                        {
                            label: qsTr("DLC"),
                            value: root.installedDlcCount > 0
                                   ? String(root.installedDlcCount)
                                   : qsTr("None")
                        },
                        { label: qsTr("Install type"), value: root.info.installKindLabel ?? "" },
                        {
                            label: qsTr("Online Fix"),
                            value: root.info.onlineFixRelevant
                                   ? (root.info.onlineFixLabel || qsTr("Not installed"))
                                   : qsTr("Not needed")
                        },
                        {
                            label: qsTr("Steamless"),
                            value: root.info.steamlessRelevant
                                   ? (root.info.steamlessLabel || qsTr("Not needed"))
                                   : qsTr("Not needed")
                        },
                        {
                            label: qsTr("Install path"),
                            value: root.playable
                                   ? (root.info.installPath || "-")
                                   : (root.isInstalling
                                          ? qsTr("Installing…")
                                          : root.readyToInstall || root.installFailed
                                            || root.downloadFailed
                                          ? qsTr("Waiting to install")
                                          : qsTr("-"))
                        },
                        {
                            label: qsTr("Download"),
                            value: root.info.downloadPath || "-"
                        }
                    ]

                    RowLayout {
                        required property var modelData
                        visible: !!modelData.value && modelData.value !== "-"
                        Layout.fillWidth: true
                        spacing: MD.Token.spacing.small

                        MD.Label {
                            Layout.preferredWidth: parent.width / 3
                            Layout.alignment: Qt.AlignTop
                            wrapMode: Text.WordWrap
                            text: modelData.label
                            color: MD.Token.color.on_surface_variant
                            typescale: MD.Token.typescale.body_medium
                        }

                        MD.Label {
                            Layout.fillWidth: true
                            text: modelData.value
                            typescale: MD.Token.typescale.body_medium
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }

        MD.Divider { Layout.fillWidth: true; visible: root.installed }
        ColumnLayout {
            visible: root.installed
            Layout.fillWidth: true
            spacing: MD.Token.spacing.small
            SectionTitle { text: qsTr("Shortcuts") }
            Flow {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.small
                MD.Button {
                    text: qsTr("Create shortcut")
                    mdState.type: MD.Enum.BtFilledTonal
                    icon.name: MD.Token.icon.expand_more
                    onClicked: shortcuts.open()
                    MD.Menu {
                        id: shortcuts
                        y: parent.height
                        autoClose: true
                        MD.MenuItem {
                            text: qsTr("Desktop shortcut")
                            onTriggered: Core.createGameDesktopShortcut(root.gameId)
                        }
                        MD.MenuItem {
                            text: qsTr("Start menu shortcut")
                            onTriggered: Core.createGameStartMenuShortcut(root.gameId)
                        }
                    }
                }
                MD.Button {
                    text: qsTr("Add to Steam")
                    mdState.type: MD.Enum.BtText
                    icon.name: MD.Token.icon.sports_esports
                    onClicked: Core.addGameToSteam(root.gameId)
                }
            }
        }
        Item { Layout.preferredHeight: MD.Token.spacing.extra_small }
    }

    Dialogs.FolderDialog {
        id: packagePicker
        title: qsTr("Choose the extracted Online Fix package folder")
        onAccepted: {
            packageDialog.folder = selectedFolder
            packageDialog.open()
        }
    }

    MD.Dialog {
        id: packageDialog
        parent: Overlay.overlay
        modal: true
        width: Math.min(440, Overlay.overlay ? Overlay.overlay.width - 48 : 440)
        property string entryId
        property string gameTitle
        property bool restore
        property url folder
        title: restore ? qsTr("Restore pre-import files?") : qsTr("Import package for %1?").arg(gameTitle)
        contentItem: MD.Label {
            wrapMode: Text.WrapAnywhere
            text: packageDialog.restore
                  ? qsTr("Restore the previous files for %1 and remove files added by the import. This replaces later edits to those files.").arg(packageDialog.gameTitle)
                  : qsTr("Sprout will copy the contents of this folder into %1 and back up files it replaces.\n\n%2").arg(packageDialog.gameTitle).arg(packageDialog.folder.toString())
        }
        footer: MD.DialogButtonBox {
            MD.Button {
                text: qsTr("Cancel")
                mdState.type: MD.Enum.BtText
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: packageDialog.close()
            }
            MD.Button {
                text: packageDialog.restore ? qsTr("Restore") : qsTr("Import")
                mdState.type: MD.Enum.BtFilled
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    packageDialog.close()
                    if (packageDialog.restore)
                        Core.restoreGameOnlineFixPackage(packageDialog.entryId)
                    else
                        Core.importGameOnlineFixPackage(packageDialog.entryId, packageDialog.folder)
                    root.detailsRevision++
                }
            }
        }
    }

    MD.Dialog {
        id: verifyDialog
        parent: Overlay.overlay
        modal: true
        width: Math.min(440, Overlay.overlay ? Overlay.overlay.width - 48 : 440)
        title: qsTr("Verify game files?")

        property string entryId: ""
        property bool withUpdate: false
        property bool dlcRisk: false

        onAboutToShow: {
            dlcRisk = Core.catalogUpdateHasDlcRisk(entryId)
        }

        contentItem: ColumnLayout {
            spacing: MD.Token.spacing.medium
            width: verifyDialog.width - verifyDialog.horizontalPadding * 2

            MD.Label {
                Layout.fillWidth: true
                text: qsTr("Every file is checked against the source. Missing or damaged files are downloaded again, and modded game files go back to the originals.")
                wrapMode: Text.WordWrap
                typescale: MD.Token.typescale.body_medium
                color: MD.Token.color.on_surface_variant
            }

            MD.Label {
                Layout.fillWidth: true
                visible: verifyDialog.withUpdate
                text: verifyDialog.dlcRisk
                      ? qsTr("The available update is installed too. It may break the game - DLC for the new build is not on the source yet.")
                      : qsTr("The available update is installed too.")
                wrapMode: Text.WordWrap
                typescale: MD.Token.typescale.body_medium
                color: verifyDialog.dlcRisk ? MD.Token.color.error : MD.Token.color.on_surface_variant
            }
        }

        footer: Item {
            implicitHeight: verifyFooterRow.implicitHeight + MD.Token.spacing.medium

            MD.DialogButtonBox {
                id: verifyFooterRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top

                MD.Button {
                    mdState.type: MD.Enum.BtText
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: verifyDialog.close()
                }

                MD.Button {
                    mdState.type: MD.Enum.BtFilled
                    text: qsTr("Verify")
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                    onClicked: {
                        const id = verifyDialog.entryId
                        verifyDialog.close()
                        // Close the sheet so the game page shows the progress.
                        if (root.visible)
                            root.close()
                        Core.verifyGameFiles(id)
                    }
                }
            }
        }
    }
}
