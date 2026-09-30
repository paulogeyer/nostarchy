import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1080
    height: 740
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: "Nostarchy"
    color: "#100e0c"

    palette.window: "#100e0c"
    palette.windowText: "#f6f1e7"
    palette.text: "#f6f1e7"
    palette.buttonText: "#f6f1e7"
    palette.base: "#181512"
    palette.button: "#2a241e"
    palette.highlight: "#e4a15a"
    palette.highlightedText: "#1a1208"
    palette.placeholderText: "#6f675e"
    palette.mid: "#3a332c"

    background: Rectangle { color: "#100e0c" }

    component AccentButton: Button {
        implicitHeight: 34
        leftPadding: 14
        rightPadding: 14
        font.pixelSize: 13
        font.weight: Font.DemiBold
        contentItem: Text {
            text: parent.text
            color: "#1a1208"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: parent.font
        }
        background: Rectangle {
            radius: 6
            color: parent.down ? "#c4843a" : (parent.hovered ? "#efb16a" : "#e4a15a")
            opacity: parent.enabled ? 1 : 0.4
        }
    }

    component QuietButton: Button {
        implicitHeight: 30
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: 12
        contentItem: Text {
            text: parent.text
            color: parent.down ? "#e4a15a" : "#f6f1e7"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: parent.font
        }
        background: Rectangle {
            radius: 6
            color: parent.hovered ? "#2a241e" : "transparent"
            border.color: parent.checked ? "#e4a15a" : "#3a332c"
            border.width: 1
        }
    }

    component Field: TextField {
        color: "#f6f1e7"
        placeholderTextColor: "#6f675e"
        font.pixelSize: 13
        leftPadding: 10
        rightPadding: 10
        selectByMouse: true
        background: Rectangle {
            radius: 6
            color: "#181512"
            border.color: parent.activeFocus ? "#e4a15a" : "#3a332c"
        }
    }

    component Avatar: Rectangle {
        property string picture: ""
        property string label: ""
        property int box: 28
        implicitWidth: box
        implicitHeight: box
        width: box
        height: box
        radius: 6
        color: "#2e2923"
        clip: true
        Image {
            id: avatarImage
            anchors.fill: parent
            source: picture
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.width: box * 2
            sourceSize.height: box * 2
            visible: status === Image.Ready
        }
        Text {
            anchors.centerIn: parent
            visible: avatarImage.status !== Image.Ready
            text: label.length ? label.charAt(0).toUpperCase() : "?"
            color: "#e4a15a"
            font.pixelSize: box > 40 ? 20 : 13
            font.weight: Font.DemiBold
        }
    }

    component RichNote: Text {
        property string body
        width: parent.width
        text: body
        textFormat: Text.RichText
        color: "#f3ead7"
        font.pixelSize: 14
        wrapMode: Text.Wrap
        linkColor: "#e4a15a"
        onLinkActivated: (link) => app.openLink(link)
    }

    Item {
        anchors.fill: parent
        visible: !app.hasIdentity

        Column {
            anchors.centerIn: parent
            width: 420
            spacing: 14

            Text {
                text: "NOSTARCHY"
                color: "#e4a15a"
                font.pixelSize: 22
                font.letterSpacing: 6
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: "A Nostr client. Keys stay on this machine."
                color: "#a3988c"
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
            AccentButton {
                text: "Generate identity"
                onClicked: app.generateIdentity()
            }
            Field {
                id: importField
                width: parent.width
                placeholderText: "or paste nsec / hex secret"
                echoMode: TextInput.Password
            }
            QuietButton {
                text: "Import"
                onClicked: app.importSecret(importField.text)
            }
            Text {
                width: parent.width
                text: app.statusText
                color: "#d4785a"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                visible: app.statusText.length > 0
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18
        visible: app.hasIdentity

        Rectangle {
            Layout.preferredWidth: 248
            Layout.fillHeight: true
            radius: 12
            color: "#181512"
            border.color: "#2e2923"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                Text {
                    text: "NOSTARCHY"
                    color: "#e4a15a"
                    font.pixelSize: 13
                    font.letterSpacing: 3
                    font.weight: Font.DemiBold
                }
                Text {
                    text: app.openRelays + " / " + app.relayCount + " relays"
                    color: "#a3988c"
                    font.pixelSize: 12
                }

                Item { Layout.preferredHeight: 8 }

                QuietButton {
                    text: "Global"
                    checked: app.feedMode === 0
                    Layout.fillWidth: true
                    onClicked: app.feedMode = 0
                }
                QuietButton {
                    text: app.followCount > 0 ? "Following (" + app.followCount + ")" : "Following"
                    checked: app.feedMode === 1
                    Layout.fillWidth: true
                    onClicked: app.feedMode = 1
                }
                Field {
                    id: followField
                    Layout.fillWidth: true
                    placeholderText: "npub1…"
                    font.pixelSize: 11
                    font.family: "DejaVu Sans Mono"
                    onAccepted: app.followNpub(text)
                }
                QuietButton {
                    text: "Follow npub"
                    Layout.fillWidth: true
                    onClicked: app.followNpub(followField.text)
                }

                Item { Layout.preferredHeight: 6 }
                Text {
                    text: "RELAYS"
                    color: "#6f675e"
                    font.pixelSize: 11
                    font.letterSpacing: 1.4
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: relayCol.implicitHeight
                    clip: true
                    Column {
                        id: relayCol
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: app.relays
                            delegate: Column {
                                required property var modelData
                                width: relayCol.width
                                spacing: 1
                                Row {
                                    spacing: 8
                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: modelData.state === "open" ? "#8fbf7f"
                                             : modelData.state === "connecting" ? "#e4a15a"
                                             : "#d4785a"
                                    }
                                    Text {
                                        text: modelData.host
                                        color: "#f6f1e7"
                                        font.pixelSize: 12
                                        width: relayCol.width - 16
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    text: modelData.detail
                                    color: "#6f675e"
                                    font.pixelSize: 11
                                    width: parent.width
                                    elide: Text.ElideRight
                                    visible: modelData.detail.length > 0
                                    leftPadding: 16
                                }
                            }
                        }
                    }
                }

                QuietButton {
                    text: "Edit relays"
                    Layout.fillWidth: true
                    onClicked: {
                        relayEdit.text = app.relayText
                        relayDialog.open()
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#2e2923" }

                RowLayout {
                    Layout.fillWidth: true
                    Avatar {
                        picture: app.selfPicture
                        label: app.displayName
                        box: 32
                    }
                    Text {
                        text: app.displayName.length ? app.displayName : "unnamed"
                        color: "#e4a15a"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: app.openProfile(app.pubkey)
                        }
                    }
                }
                Text {
                    text: app.npub
                    color: "#a3988c"
                    font.pixelSize: 11
                    font.family: "DejaVu Sans Mono"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    QuietButton { text: "Copy npub"; onClicked: app.copyNpub() }
                    QuietButton { text: showSecret.checked ? "Hide nsec" : "Show nsec"; id: showSecret; checkable: true }
                }
                Text {
                    visible: showSecret.checked
                    text: app.nsec
                    color: "#e4a15a"
                    font.pixelSize: 11
                    font.family: "DejaVu Sans Mono"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                QuietButton {
                    text: "Profile"
                    Layout.fillWidth: true
                    onClicked: profileDialog.open()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: composerCol.implicitHeight + 20
                radius: 12
                color: "#181512"
                border.color: "#2e2923"

                Column {
                    id: composerCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 8

                    Row {
                        visible: app.replying
                        spacing: 8
                        width: parent.width
                        Text {
                            text: "Replying to " + app.replyLabel
                            color: "#e4a15a"
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        QuietButton {
                            text: "Cancel"
                            onClicked: app.cancelReply()
                        }
                    }

                    ScrollView {
                        width: parent.width
                        implicitHeight: 88
                        TextArea {
                            id: composer
                            placeholderText: app.replying ? "Write a reply" : "Write a note"
                            wrapMode: TextArea.Wrap
                            color: "#f6f1e7"
                            placeholderTextColor: "#6f675e"
                            font.pixelSize: 15
                            selectByMouse: true
                            background: Rectangle { color: "transparent" }
                            Keys.onPressed: (event) => {
                                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                                    post()
                                    event.accepted = true
                                }
                            }
                        }
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        AccentButton {
                            text: "Post"
                            enabled: composer.text.trim().length > 0
                            onClicked: post()
                        }
                        Text {
                            text: app.statusText
                            color: "#a3988c"
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 90
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#14110e"
                border.color: "#2e2923"
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    visible: app.viewingProfile
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        QuietButton { text: "Back"; onClicked: app.closeProfile() }
                        Avatar { picture: app.profilePicture; label: app.profileName; box: 48 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text {
                                text: app.profileName
                                color: "#f6f1e7"
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: app.profileNpub
                                color: "#6f675e"
                                font.pixelSize: 11
                                font.family: "DejaVu Sans Mono"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                    RichNote {
                        body: app.profileAbout
                        visible: app.profileAbout.length > 0
                        Layout.fillWidth: true
                    }
                    QuietButton {
                        visible: app.profilePubkey !== app.pubkey
                        text: app.followCount >= 0 && app.isFollowed(app.profilePubkey) ? "Following" : "Follow"
                        onClicked: app.toggleFollow(app.profilePubkey)
                    }
                    Text {
                        text: "NOTES"
                        color: "#6f675e"
                        font.pixelSize: 11
                        font.letterSpacing: 1.4
                    }
                    ListView {
                        id: profileView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8
                        clip: true
                        model: app.profileNotes
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            required property string eventId
                            required property string pubkey
                            required property string name
                            required property string content
                            required property string richContent
                            required property string timeText
                            required property string picture
                            width: profileView.width
                            implicitHeight: personCol.implicitHeight + 16
                            height: implicitHeight
                            radius: 8
                            color: "#1c1915"
                            Column {
                                id: personCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 4
                                RowLayout {
                                    width: parent.width
                                    Avatar { picture: picture; label: name; box: 24 }
                                    Text {
                                        text: name
                                        color: "#e4a15a"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: app.openProfile(pubkey)
                                        }
                                    }
                                    Text { text: timeText; color: "#6f675e"; font.pixelSize: 11 }
                                }
                                RichNote { body: richContent }
                                QuietButton {
                                    text: "View"
                                    onClicked: app.openNote(eventId, pubkey, name, content, timeText)
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    visible: app.viewing && !app.viewingProfile
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        QuietButton {
                            text: "Back"
                            onClicked: app.closeNote()
                        }
                        Avatar { picture: app.openNotePicture; label: app.openNoteName; box: 28 }
                        Text {
                            text: app.openNoteName
                            color: "#e4a15a"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: app.openProfile(app.openNotePubkey)
                            }
                        }
                        Text {
                            text: app.openNoteTime
                            color: "#6f675e"
                            font.pixelSize: 11
                        }
                    }
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(160, openNoteText.implicitHeight + 4)
                        contentHeight: openNoteText.implicitHeight
                        clip: true
                        RichNote {
                            id: openNoteText
                            body: app.openNoteRich
                            font.pixelSize: 15
                        }
                    }
                    Row {
                        spacing: 6
                        QuietButton {
                            text: "Reply"
                            onClicked: app.beginReply(app.openNoteId, app.openNotePubkey, app.openNoteName)
                        }
                        QuietButton {
                            visible: app.openNotePubkey !== app.pubkey
                            text: app.followCount >= 0 && app.isFollowed(app.openNotePubkey) ? "Following" : "Follow"
                            onClicked: app.toggleFollow(app.openNotePubkey)
                        }
                    }
                    Text {
                        text: "REPLIES"
                        color: "#6f675e"
                        font.pixelSize: 11
                        font.letterSpacing: 1.4
                    }
                    ListView {
                        id: replyView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8
                        clip: true
                        model: app.replies
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            required property string eventId
                            required property string pubkey
                            required property string name
                            required property string content
                            required property string richContent
                            required property string timeText
                            required property string picture
                            width: replyView.width
                            implicitHeight: replyCol.implicitHeight + 16
                            height: implicitHeight
                            radius: 8
                            color: "#1c1915"
                            Column {
                                id: replyCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 4
                                RowLayout {
                                    width: parent.width
                                    Avatar { picture: picture; label: name; box: 24 }
                                    Text {
                                        text: name
                                        color: "#e4a15a"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: app.openProfile(pubkey)
                                        }
                                    }
                                    Text {
                                        text: timeText
                                        color: "#6f675e"
                                        font.pixelSize: 11
                                    }
                                }
                                RichNote { body: richContent }
                                QuietButton {
                                    text: "View"
                                    onClicked: app.openNote(eventId, pubkey, name, content, timeText)
                                }
                            }
                        }
                    }
                    Text {
                        visible: replyView.count === 0
                        text: "No replies yet."
                        color: "#6f675e"
                        font.pixelSize: 13
                    }
                }

                ListView {
                    id: feedView
                    visible: !app.viewing && !app.viewingProfile
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    clip: true
                    model: app.timeline
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        required property string eventId
                        required property string pubkey
                        required property string name
                        required property string content
                        required property string richContent
                        required property string timeText
                        required property string picture
                        required property string npub
                        required property bool followed
                        width: feedView.width
                        implicitHeight: noteCol.implicitHeight + 20
                        height: implicitHeight
                        radius: 10
                        color: "#1c1915"

                        Column {
                            id: noteCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            spacing: 6

                            RowLayout {
                                width: parent.width
                                spacing: 8
                                Avatar { picture: picture; label: name; box: 28 }
                                Text {
                                    text: name
                                    color: "#e4a15a"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: app.openProfile(pubkey)
                                    }
                                }
                                Text {
                                    text: timeText
                                    color: "#6f675e"
                                    font.pixelSize: 11
                                }
                            }
                            RichNote { body: richContent }
                            Row {
                                spacing: 6
                                QuietButton {
                                    text: "View"
                                    onClicked: app.openNote(eventId, pubkey, name, content, timeText)
                                }
                                QuietButton {
                                    text: "Reply"
                                    onClicked: app.beginReply(eventId, pubkey, name)
                                }
                                QuietButton {
                                    visible: pubkey !== app.pubkey
                                    text: followed ? "Following" : "Follow"
                                    checked: followed
                                    onClicked: app.toggleFollow(pubkey)
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    visible: !app.viewing && !app.viewingProfile && feedView.count === 0
                    color: "#6f675e"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                    text: app.feedMode === 1
                          ? "No notes from people you follow yet."
                          : "Waiting for notes from relays."
                }
            }
        }
    }

    function post() {
        if (composer.text.trim().length === 0)
            return
        app.publishNote(composer.text)
        composer.clear()
    }

    Dialog {
        id: relayDialog
        title: "Relays"
        modal: true
        anchors.centerIn: parent
        width: 440
        standardButtons: Dialog.NoButton
        background: Rectangle { color: "#181512"; radius: 12; border.color: "#3a332c" }
        header: Label {
            text: "Relays"
            color: "#f6f1e7"
            font.pixelSize: 16
            leftPadding: 16
            topPadding: 14
        }
        contentItem: Column {
            spacing: 10
            width: 400
            Text {
                text: "One websocket URL per line."
                color: "#a3988c"
                font.pixelSize: 12
            }
            ScrollView {
                width: 400
                height: 180
                TextArea {
                    id: relayEdit
                    color: "#f6f1e7"
                    wrapMode: TextArea.Wrap
                    font.pixelSize: 13
                    font.family: "DejaVu Sans Mono"
                    selectByMouse: true
                }
            }
            Row {
                spacing: 8
                AccentButton {
                    text: "Save"
                    onClicked: {
                        app.setRelaysFromText(relayEdit.text)
                        relayDialog.close()
                    }
                }
                QuietButton { text: "Cancel"; onClicked: relayDialog.close() }
            }
        }
    }

    Dialog {
        id: profileDialog
        title: "Profile"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.NoButton
        background: Rectangle { color: "#181512"; radius: 12; border.color: "#3a332c" }
        header: Label {
            text: "Profile"
            color: "#f6f1e7"
            font.pixelSize: 16
            leftPadding: 16
            topPadding: 14
        }
        onOpened: {
            nameField.text = app.displayName
            aboutField.text = app.about
        }
        contentItem: Column {
            spacing: 10
            width: 380
            Field { id: nameField; width: parent.width; placeholderText: "display name" }
            Field { id: aboutField; width: parent.width; placeholderText: "about" }
            Row {
                spacing: 8
                AccentButton {
                    text: "Publish"
                    onClicked: {
                        app.publishProfile(nameField.text, aboutField.text)
                        profileDialog.close()
                    }
                }
                QuietButton { text: "Cancel"; onClicked: profileDialog.close() }
            }
            QuietButton {
                text: "Replace identity"
                onClicked: confirmDialog.open()
            }
        }
    }

    Dialog {
        id: confirmDialog
        modal: true
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.NoButton
        background: Rectangle { color: "#181512"; radius: 12; border.color: "#3a332c" }
        contentItem: Column {
            spacing: 12
            width: 340
            Text {
                width: parent.width
                text: "Replace the key stored on this machine? The old nsec will be forgotten unless you have a copy."
                color: "#f6f1e7"
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }
            Row {
                spacing: 8
                AccentButton {
                    text: "Replace"
                    onClicked: {
                        app.generateIdentity()
                        confirmDialog.close()
                        profileDialog.close()
                    }
                }
                QuietButton {
                    text: "Cancel"
                    onClicked: confirmDialog.close()
                }
            }
        }
    }
}
