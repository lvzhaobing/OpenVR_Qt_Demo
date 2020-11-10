#ifndef VRRENDER_H
#define VRRENDER_H

#include <QObject>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include "openvr.h"

class VRRender : public QObject, QOpenGLFunctions
{
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)


public:
    explicit VRRender(QObject *parent = nullptr);
    ~VRRender();

    QImage frame() const;

    QSize frameSize() const;

public slots:

    void renderImage();

signals:
    void frameChanged(QImage frame);
    void frameSizeChanged(QSize frameSize);

private:
    void initGL();
    void initVR();
    void release();
    void updatePoses();
    void renderEye(vr::Hmd_Eye eye);

    QMatrix4x4 vrMatrixToQt(const vr::HmdMatrix34_t &mat);
    QMatrix4x4 vrMatrixToQt(const vr::HmdMatrix44_t &mat);

    // QMatrix is using qreal, so we need to overload to handle both platform cases
    void glUniformMatrix4(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

    QMatrix4x4 viewProjection(vr::Hmd_Eye eye);

    QString getTrackedDeviceString(vr::TrackedDeviceIndex_t device,
                                   vr::TrackedDeviceProperty prop,
                                   vr::TrackedPropertyError *error = 0);

    QVector<GLfloat> drawCircle(float x, float y, float z, float r, int lineSegmentCount);

    bool createShader();

private:
    QImage m_frame;
    QSize m_frameSize;
    float m_aspectRatio;
    int m_frameCount;

    //OpenGL
    QSurfaceFormat m_surfaceFormat;
    QOffscreenSurface m_surface;
    QOpenGLContext m_openGLContext;

    QOpenGLShaderProgram lightingShader, lampShader;
    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject cubeVAO, lightVAO;
    std::unique_ptr<QOpenGLTexture> m_pDiffuseMap;
    std::unique_ptr<QOpenGLTexture> m_pSpecularMap;


    //OpenVR
    vr::IVRSystem *m_hmd;
    vr::TrackedDevicePose_t m_trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
    QMatrix4x4 m_matrixDevicePose[vr::k_unMaxTrackedDeviceCount];

    QMatrix4x4 m_leftProjection, m_leftPose;
    QMatrix4x4 m_rightProjection, m_rightPose;
    QMatrix4x4 m_hmdPose;

    QOpenGLFramebufferObject *m_leftBuffer;
    QOpenGLFramebufferObject *m_rightBuffer;
    QOpenGLFramebufferObject *m_resolveBuffer;

    uint32_t m_eyeWidth, m_eyeHeight;

};

#endif // VRRENDER_H
