import QtQuick 2.12
import QtQuick.Window 2.12
import OpenGLDemo 1.0

Window {
    visible: true
    width: 640
    height: 480
    title: qsTr("OpenGL Demo")

    VRRender{
        id: render
    }

    ImageView{
        id: imageView
        anchors.centerIn: parent
        fillColor: "gray"
        image: render.frame
        width: render.frameSize.width
        height: render.frameSize.height

        onRequestRender: {
            render.renderImage();
        }
    }

    Component.onCompleted: {
        render.renderImage();
    }
}
