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
    color: theme.background

    palette.window: theme.background
    palette.windowText: theme.text
    palette.text: theme.text
    palette.buttonText: theme.text
    palette.base: theme.surface
    palette.button: theme.surface
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText
    palette.placeholderText: theme.faint
    palette.mid: theme.border

    background: Rectangle { color: theme.background }

    component FastWheel: WheelHandler {
        blocking: true
        onWheel: (event) => {
            const flick = parent
            const boost = theme.wheelBoost
            let dy = event.pixelDelta.y
            if (Math.abs(dy) < 1)
                dy = event.angleDelta.y * boost * 0.55
            else
                dy *= boost
            flick.contentY -= dy
            flick.returnToBounds()
            event.accepted = true
        }
    }

    component AccentButton: Button {
        implicitHeight: 34
        leftPadding: 14
        rightPadding: 14
        font.pixelSize: 13
        font.bold: true
        contentItem: Text {
            text: parent.text
            color: theme.accentText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: parent.font
        }
        background: Rectangle {
            radius: 6
            color: parent.down ? theme.accent : (parent.hovered ? theme.accent : theme.accent)
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
            color: parent.down ? theme.accent : theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: parent.font
        }
        background: Rectangle {
            radius: 6
            color: parent.hovered ? theme.card : "transparent"
            border.color: parent.checked ? theme.accent : theme.border
            border.width: 1
        }
    }

    component Field: TextField {
        color: theme.text
        placeholderTextColor: theme.faint
        font.pixelSize: 13
        leftPadding: 10
        rightPadding: 10
        selectByMouse: true
        background: Rectangle {
            radius: 6
            color: theme.surface
            border.color: parent.activeFocus ? theme.accent : theme.border
        }
    }

    function face(pubkey) {
        app.avatarEpoch
        return app.avatarUrl(pubkey)
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
        color: theme.border
        clip: true
        Image {
            id: avatarImage
            anchors.fill: parent
            source: picture
            fillMode: Image.PreserveAspectCrop
            asynchronous: false
            cache: false
            sourceSize.width: box
            sourceSize.height: box
        }
        Text {
            anchors.centerIn: parent
            visible: picture.length === 0 || avatarImage.status === Image.Error
            text: label.length ? label.charAt(0).toUpperCase() : "?"
            color: theme.accent
            font.pixelSize: box > 40 ? 20 : 13
        }
    }

component RichNote: Text {
    property string body
    width: parent.width
    text: body
    textFormat: Text.StyledText
        color: theme.bright
        font.pixelSize: 14
        wrapMode: Text.Wrap
        linkColor: theme.accent
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
                color: theme.accent
                font.pixelSize: 22
                    font.bold: true
            }
            Text {
                width: parent.width
                text: "A Nostr client. Keys stay on this machine."
                color: theme.muted
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
                color: theme.danger
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
            color: theme.surface
            border.color: theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                Text {
                    text: "NOSTARCHY"
                    color: theme.accent
                    font.pixelSize: 13
                    font.bold: true
                }
                Text {
                    text: app.openRelays + " / " + app.relayCount + " relays"
                    color: theme.muted
                    font.pixelSize: 12
                }

                Item { Layout.preferredHeight: 8 }

                QuietButton {
                    text: "Global"
                    checked: app.feedMode === 0
                    Layout.fillWidth: true
                    onClicked: {
                        app.closeProfile()
                        app.closeNote()
                        app.feedMode = 0
                    }
                }
                QuietButton {
                    text: app.followCount > 0 ? "Following (" + app.followCount + ")" : "Following"
                    checked: app.feedMode === 1
                    Layout.fillWidth: true
                    onClicked: {
                        app.closeProfile()
                        app.closeNote()
                        app.feedMode = 1
                    }
                }
                Field {
                    id: followField
                    Layout.fillWidth: true
                    placeholderText: "npub1…"
                    font.pixelSize: 11
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
                    color: theme.faint
                    font.pixelSize: 11
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: relayCol.implicitHeight
                    FastWheel {}
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
                                        color: modelData.state === "open" ? theme.ok
                                             : modelData.state === "connecting" ? theme.accent
                                             : theme.danger
                                    }
                                    Text {
                                        text: modelData.host
                                        color: theme.text
                                        font.pixelSize: 12
                                        width: relayCol.width - 16
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    text: modelData.detail
                                    color: theme.faint
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

                Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                RowLayout {
                    Layout.fillWidth: true
                    Avatar {
                        picture: app.selfPicture
                        label: app.displayName
                        box: 32
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: app.openProfile(app.pubkey)
                        }
                    }
                    Text {
                        text: app.displayName.length ? app.displayName : "unnamed"
                        color: theme.accent
                        font.pixelSize: 14
                        font.bold: true
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
                    color: theme.muted
                    font.pixelSize: 11
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
                    color: theme.accent
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                QuietButton {
                    text: logs.enabled ? "Logging on" : "Store logs"
                    Layout.fillWidth: true
                    onClicked: logs.enabled = !logs.enabled
                }
                Text {
                    visible: logs.enabled
                    text: logs.path
                    color: theme.faint
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                QuietButton {
                    text: "View profile"
                    Layout.fillWidth: true
                    onClicked: app.openProfile(app.pubkey)
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
                color: theme.surface
                border.color: theme.border

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
                            color: theme.accent
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
                            color: theme.text
                            placeholderTextColor: theme.faint
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
                            color: theme.muted
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
                color: theme.darkBackground
                border.color: theme.border
                clip: true

                Flickable {
                    id: profilePage
                    anchors.fill: parent
                    anchors.margins: 8
                    visible: app.viewingProfile
                    contentWidth: width
                    contentHeight: profileColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    FastWheel {}
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    property string shownPubkey: app.profilePubkey
                    onShownPubkeyChanged: contentY = 0

                    Column {
                        id: profileColumn
                        width: profilePage.width
                        spacing: 8
                        RowLayout {
                            width: parent.width
                            QuietButton { text: "Back"; onClicked: app.closeProfile() }
                            Avatar { picture: app.profilePicture; label: app.profileName; box: 48 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: app.profileName
                                    color: theme.text
                                    font.pixelSize: 16
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: app.profileNpub
                                    color: theme.faint
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        RichNote {
                            body: app.profileAbout
                            visible: app.profileAbout.length > 0
                        }
                        QuietButton {
                            visible: app.profilePubkey !== app.pubkey
                            text: app.followCount >= 0 && app.isFollowed(app.profilePubkey) ? "Following" : "Follow"
                            onClicked: app.toggleFollow(app.profilePubkey)
                        }
                        Row {
                            spacing: 16
                            Text {
                                text: "Timeline"
                                color: !app.viewingPeople ? theme.accent : theme.muted
                                font.pixelSize: 13
                                font.bold: !app.viewingPeople
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: app.closePeople()
                                }
                            }
                            Text {
                                text: app.profileFollowingCount + " following"
                                color: app.viewingPeople && app.peopleTitle === "Following" ? theme.accent : theme.muted
                                font.pixelSize: 13
                                font.bold: app.viewingPeople && app.peopleTitle === "Following"
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: app.showFollowing()
                                }
                            }
                            Text {
                                text: app.profileFollowerCount + " followers"
                                color: app.viewingPeople && app.peopleTitle === "Followers" ? theme.accent : theme.muted
                                font.pixelSize: 13
                                font.bold: app.viewingPeople && app.peopleTitle === "Followers"
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: app.showFollowers()
                                }
                            }
                        }
                        RowLayout {
                            visible: app.viewingPeople
                            width: parent.width
                            QuietButton { text: "Back"; onClicked: app.closePeople() }
                            Text {
                                text: app.peopleTitle
                                color: theme.text
                                font.pixelSize: 14
                                font.bold: true
                            }
                        }
                        ListView {
                            visible: app.viewingPeople
                            width: parent.width
                            height: contentHeight
                            interactive: false
                            model: app.people
                            spacing: 6
                            delegate: Rectangle {
                                required property string pubkey
                                required property string name
                                required property string picture
                                width: ListView.view.width
                                height: 44
                                radius: 8
                                color: theme.card
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8
                                    Avatar { picture: face(pubkey); label: name; box: 28 }
                                    Text {
                                        text: name
                                        color: theme.accent
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: app.openProfile(pubkey)
                                }
                            }
                        }
                        Text {
                            visible: !app.viewingPeople
                            text: "NOTES"
                            color: theme.faint
                            font.pixelSize: 11
                        }
                        ListView {
                            id: profileView
                            visible: !app.viewingPeople
                            width: parent.width
                            height: contentHeight
                            interactive: false
                            spacing: 8
                            model: app.profileNotes
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
                                color: theme.card
                                Column {
                                    id: personCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 10
                                    spacing: 4
                                    RowLayout {
                                        width: parent.width
                                        Avatar { picture: face(pubkey); label: name; box: 24 }
                                        Text {
                                            text: name
                                            color: theme.accent
                                            font.pixelSize: 13
                                            font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: app.openProfile(pubkey)
                                            }
                                        }
                                        Text { text: timeText; color: theme.faint; font.pixelSize: 11 }
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
                }

                Flickable {
                    id: notePage
                    anchors.fill: parent
                    anchors.margins: 8
                    visible: app.viewing && !app.viewingProfile
                    clip: true
                    contentHeight: noteColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    FastWheel {}
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    Column {
                        id: noteColumn
                        width: notePage.width
                        spacing: 8

                    RowLayout {
                        width: parent.width
                        QuietButton {
                            text: "Back"
                            onClicked: app.closeNote()
                        }
                        Avatar { picture: app.openNotePicture; label: app.openNoteName; box: 28 }
                        Text {
                            text: app.openNoteName
                            color: theme.accent
                            font.pixelSize: 14
                            font.bold: true
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
                            color: theme.faint
                            font.pixelSize: 11
                        }
                    }
                    RichNote {
                        id: openNoteText
                        width: parent.width
                        body: app.openNoteRich
                        font.pixelSize: 15
                    }
                    Row {
                        spacing: 6
                        QuietButton {
                            text: "Reply"
                            onClicked: app.beginReply(app.openNoteId, app.openNotePubkey, app.openNoteName)
                        }
                        QuietButton {
                            visible: app.openNotePubkey === app.pubkey
                            text: "Delete"
                            onClicked: deleteDialog.open()
                        }
                        QuietButton {
                            visible: app.openNotePubkey !== app.pubkey
                            text: app.followCount >= 0 && app.isFollowed(app.openNotePubkey) ? "Following" : "Follow"
                            onClicked: app.toggleFollow(app.openNotePubkey)
                        }
                    }
                    Text {
                        text: "REPLIES"
                        color: theme.faint
                        font.pixelSize: 11
                        }
                    ListView {
                        id: replyView
                        width: parent.width
                        height: contentHeight
                        interactive: false
                        spacing: 8
                        model: app.replies
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
                            color: theme.card
                            Column {
                                id: replyCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 4
                                RowLayout {
                                    width: parent.width
                                    Avatar { picture: face(pubkey); label: name; box: 24 }
                                    Text {
                                        text: name
                                        color: theme.accent
                                        font.pixelSize: 13
                                        font.bold: true
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
                                        color: theme.faint
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
                        color: theme.faint
                        font.pixelSize: 13
                    }
                    }
                }

                ListView {
                    id: feedView
                    FastWheel {}
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
                        height: Math.max(48, noteCol.childrenRect.height + 24)
                        radius: 10
                        color: theme.card

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
                                Avatar { picture: face(pubkey); label: name; box: 28 }
                                Text {
                                    text: name
                                    color: theme.accent
                                    font.pixelSize: 13
                                    font.bold: true
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
                                    color: theme.faint
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
                    color: theme.faint
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
        id: deleteDialog
        title: "Delete note"
        modal: true
        anchors.centerIn: parent
        width: 360
        standardButtons: Dialog.NoButton
        background: Rectangle { color: theme.surface; radius: 12; border.color: theme.border }
        header: Label {
            text: "Delete note"
            color: theme.text
            font.pixelSize: 16
            leftPadding: 16
            topPadding: 14
        }
        contentItem: Column {
            spacing: 12
            width: 320
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Ask relays to delete this note. Some may keep a copy."
                color: theme.muted
                font.pixelSize: 13
            }
            Row {
                spacing: 8
                QuietButton {
                    text: "Delete"
                    onClicked: {
                        app.deleteNote(app.openNoteId)
                        deleteDialog.close()
                    }
                }
                QuietButton { text: "Cancel"; onClicked: deleteDialog.close() }
            }
        }
    }

    Dialog {
        id: relayDialog
        title: "Relays"
        modal: true
        anchors.centerIn: parent
        width: 440
        standardButtons: Dialog.NoButton
        background: Rectangle { color: theme.surface; radius: 12; border.color: theme.border }
        header: Label {
            text: "Relays"
            color: theme.text
            font.pixelSize: 16
            leftPadding: 16
            topPadding: 14
        }
        contentItem: Column {
            spacing: 10
            width: 400
            Text {
                text: "One websocket URL per line."
                color: theme.muted
                font.pixelSize: 12
            }
            ScrollView {
                width: 400
                height: 180
                TextArea {
                    id: relayEdit
                    color: theme.text
                    wrapMode: TextArea.Wrap
                    font.pixelSize: 13
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
        background: Rectangle { color: theme.surface; radius: 12; border.color: theme.border }
        header: Label {
            text: "Profile"
            color: theme.text
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
        background: Rectangle { color: theme.surface; radius: 12; border.color: theme.border }
        contentItem: Column {
            spacing: 12
            width: 340
            Text {
                width: parent.width
                text: "Replace the key stored on this machine? The old nsec will be forgotten unless you have a copy."
                color: theme.text
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
