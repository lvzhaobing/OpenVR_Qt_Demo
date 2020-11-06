#include <QDebug>
#include "vr_render.h"
#include "modelFormats.h"

const float NEAR_CLIP = 0.1f;
const float FAR_CLIP = 10000.0f;

#define SAFE_DELETE(p)       { if(p) { delete (p);     (p)=nullptr; } }
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=nullptr; } }

VRRender::VRRender(QObject *parent)
    : QObject(parent)
    ,m_frame(QImage())
    ,m_frameSize(QSize(0,0))
    ,m_aspectRatio(0)
    ,m_frameCount(0)
    ,m_surfaceFormat(QSurfaceFormat())
    ,m_openGLContext(nullptr)
    ,m_shader(nullptr)
    ,m_skyTexture(nullptr)
    ,m_vertCount(0)
    ,m_leftBuffer(nullptr)
    ,m_rightBuffer(nullptr)
    ,m_resolveBuffer(nullptr)
    ,m_hmd(nullptr)
    ,m_eyeWidth(0)
    ,m_eyeHeight(0)
{
    initGL();
    initVR();
}

VRRender::~VRRender()
{
    release();
}

QImage VRRender::frame() const
{
    return m_frame;
}

QSize VRRender::frameSize() const
{
    return m_frameSize;
}

void VRRender::initGL()
{
    //   Viewport size
    m_frameSize = QSize(800,600);
    emit frameSizeChanged(m_frameSize);
    m_aspectRatio = (float)m_frameSize.width() / m_frameSize.height();

    //   =======CONTEXT SETUP======
    //   Set OpenGL version to use
    m_surfaceFormat.setMajorVersion(4);
    m_surfaceFormat.setMinorVersion(3);
    m_openGLContext.setFormat(m_surfaceFormat);
    m_openGLContext.create();
    if(!m_openGLContext.isValid()) qDebug("Unable to create context");
    m_surface.setFormat(m_surfaceFormat);
    m_surface.create();
    if(!m_surface.isValid()) qDebug("Unable to create the Offscreen surface");
    m_openGLContext.makeCurrent(&m_surface);
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);

    m_shader = new QOpenGLShaderProgram();
    m_shader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/unlit.vert");
    m_shader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/unlit.frag");
    if (m_shader->link()) {
        qDebug("Shaders link success.");
    } else {
        qDebug("Shaders link failed!");
    }

    m_vao = new QOpenGLVertexArrayObject();
    m_vao->create();
    m_vao->bind();
    QVector<GLfloat> points = readObj(":/models/sphere.obj");
    m_vertCount = points.length();
    qDebug() << "loaded" << m_vertCount << "verts";

    m_vertexBuffer.create();
    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vertexBuffer.bind();

    m_vertexBuffer.allocate(points.data(), points.length() * sizeof(GLfloat));

    m_shader->bind();

    m_shader->setAttributeBuffer("vertex", GL_FLOAT, 0, 3, 5 * sizeof(GLfloat));
    m_shader->enableAttributeArray("vertex");
    m_shader->setAttributeBuffer("texCoord", GL_FLOAT, 3 * sizeof(GLfloat), 2, 5 * sizeof(GLfloat));
    m_shader->enableAttributeArray("texCoord");
    m_shader->setUniformValue("diffuse", 0);
    m_skyTexture = new QOpenGLTexture(QImage(":/textures/uvmap.png"));

}

void VRRender::initVR()
{
    vr::EVRInitError error = vr::VRInitError_None;
    m_hmd = vr::VR_Init(&error, vr::VRApplication_Scene);

    if (error != vr::VRInitError_None)
    {
        m_hmd = 0;
        QString message = vr::VR_GetVRInitErrorAsEnglishDescription(error);
        qCritical() << message;
        return;
    }

    // get eye matrices
    m_rightProjection = vrMatrixToQt(m_hmd->GetProjectionMatrix(vr::Eye_Right, NEAR_CLIP, FAR_CLIP));
    m_rightPose = vrMatrixToQt(m_hmd->GetEyeToHeadTransform(vr::Eye_Right)).inverted();

    m_leftProjection = vrMatrixToQt(m_hmd->GetProjectionMatrix(vr::Eye_Left, NEAR_CLIP, FAR_CLIP));
    m_leftPose = vrMatrixToQt(m_hmd->GetEyeToHeadTransform(vr::Eye_Left)).inverted();

    QString device = getTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
    QString serialNum = getTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);
    qDebug() << "device: " << device << "serialNumber: " << serialNum;

    // setup frame buffers for eyes
    m_hmd->GetRecommendedRenderTargetSize(&m_eyeWidth, &m_eyeHeight);

    QOpenGLFramebufferObjectFormat buffFormat;
    buffFormat.setAttachment(QOpenGLFramebufferObject::Depth);
    buffFormat.setInternalTextureFormat(GL_RGBA8);
    buffFormat.setSamples(4);

    m_leftBuffer = new QOpenGLFramebufferObject(m_eyeWidth, m_eyeHeight, buffFormat);
    m_rightBuffer = new QOpenGLFramebufferObject(m_eyeWidth, m_eyeHeight, buffFormat);

    QOpenGLFramebufferObjectFormat resolveFormat;
    resolveFormat.setInternalTextureFormat(GL_RGBA8);
    buffFormat.setSamples(0);

    m_resolveBuffer = new QOpenGLFramebufferObject(m_eyeWidth*2, m_eyeHeight, resolveFormat);

    // turn on compositor
    if (!vr::VRCompositor())
    {
        QString message = "Compositor initialization failed. See log file for details";
        qCritical() << message;
        return;
    }
}

void VRRender::renderImage()
{
    if (m_hmd)
    {
        updatePoses();
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glViewport(0, 0, m_eyeWidth, m_eyeHeight);

        QRect sourceRect(0, 0, m_eyeWidth, m_eyeHeight);

        glEnable(GL_MULTISAMPLE);
        m_leftBuffer->bind();
        renderEye(vr::Eye_Left);
        m_leftBuffer->release();
        QRect targetLeft(0, 0, m_eyeWidth, m_eyeHeight);
        QOpenGLFramebufferObject::blitFramebuffer(m_resolveBuffer, targetLeft,
                                                  m_leftBuffer, sourceRect);

        glEnable(GL_MULTISAMPLE);
        m_rightBuffer->bind();
        renderEye(vr::Eye_Right);
        m_rightBuffer->release();
        QRect targetRight(m_eyeWidth, 0, m_eyeWidth, m_eyeHeight);
        QOpenGLFramebufferObject::blitFramebuffer(m_resolveBuffer, targetRight,
                                                  m_rightBuffer, sourceRect);
    }

    if (m_hmd)
    {
        vr::VRTextureBounds_t leftRect = { 0.0f, 0.0f, 0.5f, 1.0f };
        vr::VRTextureBounds_t rightRect = { 0.5f, 0.0f, 1.0f, 1.0f };
        vr::Texture_t composite = { (void*)m_resolveBuffer->texture(), vr::TextureType_OpenGL, vr::ColorSpace_Gamma };

        vr::VRCompositor()->Submit(vr::Eye_Left, &composite, &leftRect);
        vr::VRCompositor()->Submit(vr::Eye_Right, &composite, &rightRect);
    }

    m_frame = m_resolveBuffer->toImage();
    emit frameChanged(m_frame);

    m_frameCount += 1;
    if(m_frameCount > 100)
        m_frameCount = 0;
}

void VRRender::release()
{
    SAFE_DELETE(m_skyTexture);
    m_surface.destroy();
    if(m_vao)
        m_vao->destroy();
    SAFE_DELETE(m_vao);

    m_vertexBuffer.destroy();

    SAFE_DELETE(m_shader);
    SAFE_DELETE(m_leftBuffer);
    SAFE_DELETE(m_rightBuffer);
    SAFE_DELETE(m_resolveBuffer);

    if(m_hmd){
        vr::VR_Shutdown();
        m_hmd = nullptr;
    }
}

void VRRender::updatePoses()
{
    vr::VRCompositor()->WaitGetPoses(m_trackedDevicePose, vr::k_unMaxTrackedDeviceCount, NULL, 0);

    for (unsigned int i=0; i<vr::k_unMaxTrackedDeviceCount; i++)
    {
        if (m_trackedDevicePose[i].bPoseIsValid)
        {
            m_matrixDevicePose[i] =  vrMatrixToQt(m_trackedDevicePose[i].mDeviceToAbsoluteTracking);
        }
    }

    if (m_trackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
    {
        m_hmdPose = m_matrixDevicePose[vr::k_unTrackedDeviceIndex_Hmd].inverted();
    }
}

void VRRender::renderEye(vr::Hmd_Eye eye, bool overUnder)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    m_vao->bind();
    m_shader->bind();
    m_skyTexture->bind(0);

    m_shader->setUniformValue("transform", viewProjection(eye));
    m_shader->setUniformValue("leftEye", eye==vr::Eye_Left);
    m_shader->setUniformValue("overUnder", overUnder);
    glDrawArrays(GL_TRIANGLES, 0, m_vertCount);
}

QMatrix4x4 VRRender::vrMatrixToQt(const vr::HmdMatrix34_t &mat)
{
    return QMatrix4x4(
                mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
            mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
            mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
            0.0,         0.0,         0.0,         1.0f
            );
}

QMatrix4x4 VRRender::vrMatrixToQt(const vr::HmdMatrix44_t &mat)
{
    return QMatrix4x4(
                mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
            mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
            mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
            mat.m[3][0], mat.m[3][1], mat.m[3][2], mat.m[3][3]
            );
}

void VRRender::glUniformMatrix4(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    m_openGLContext.functions()->glUniformMatrix4fv(location, count, transpose, value);
}

QMatrix4x4 VRRender::viewProjection(vr::Hmd_Eye eye)
{
    QMatrix4x4 s;
    s.scale(1000.0f);

    if (eye == vr::Eye_Left)
        return m_leftProjection * m_leftPose * m_hmdPose * s;
    else
        return m_rightProjection * m_rightPose * m_hmdPose * s;
}

QString VRRender::getTrackedDeviceString(vr::TrackedDeviceIndex_t device, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError *error)
{
    uint32_t len = m_hmd->GetStringTrackedDeviceProperty(device, prop, NULL, 0, error);
    if(len == 0)
        return "";

    char *buf = new char[len];
    m_hmd->GetStringTrackedDeviceProperty(device, prop, buf, len, error);

    QString result = QString::fromLocal8Bit(buf);
    delete [] buf;

    return result;
}
