#include <QDebug>
#include "vr_render.h"

const float NEAR_CLIP = 0.1f;
const float FAR_CLIP = 10000.0f;

#define SAFE_DELETE(p)       { if(p) { delete (p);     (p)=nullptr; } }
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=nullptr; } }


const float CALIB_DEPTH = 10.0f;
// lighting
static QVector3D lightPos(1.2f, 1.0f, -2.0f);

const float vertices[] = {
    // positions          // normals           // texture coords
    -0.5f, -0.5f, 0.0f,  0.0f,  0.0f, 1.0f,  0.0f, 1.0f,
    0.5f, -0.5f, 0.0f,  0.0f,  0.0f, 1.0f,  1.0f, 1.0f,
    0.5f, 0.5f, 0.0f,  0.0f,  0.0f, 1.0f,  1.0f, 0.0f,
    0.5f, 0.5f, 0.0f,  0.0f,  0.0f, 1.0f,  1.0f, 0.0f,
    -0.5f, 0.5f, 0.0f,  0.0f,  0.0f, 1.0f,  0.0f, 0.0f,
    -0.5f, -0.5f, 0.0f,  0.0f,  0.0f, 1.0f,  0.0f, 1.0f
};


VRRender::VRRender(QObject *parent)
    : QObject(parent)
    ,m_frame(QImage())
    ,m_frameSize(QSize(0,0))
    ,m_aspectRatio(0)
    ,m_frameCount(0)
    ,m_surfaceFormat(QSurfaceFormat())
    ,m_openGLContext(nullptr)
    ,m_hmd(nullptr)
    ,m_leftBuffer(nullptr)
    ,m_rightBuffer(nullptr)
    ,m_resolveBuffer(nullptr)
    ,m_eyeWidth(0)
    ,m_eyeHeight(0)
{
    //   Viewport size
    m_frameSize = QSize(1024,768);
    emit frameSizeChanged(m_frameSize);
    m_aspectRatio = (float)m_frameSize.width() / m_frameSize.height();

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
    //   =======CONTEXT SETUP======
    m_openGLContext.setFormat(m_surfaceFormat);
    m_openGLContext.create();
    if(!m_openGLContext.isValid()) qDebug("Unable to create context");
    m_surface.setFormat(m_surfaceFormat);
    m_surface.create();
    if(!m_surface.isValid()) qDebug("Unable to create the Offscreen surface");
    m_openGLContext.makeCurrent(&m_surface);
    initializeOpenGLFunctions();

    createShader();
    vbo.create();
    vbo.bind();
    vbo.allocate(vertices, sizeof(vertices));

    {
        QOpenGLVertexArrayObject::Binder vaoBind(&cubeVAO);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
    }

    // load textures (we now use a utility function to keep the code more organized)
    // -----------------------------------------------------------------------------
    caliBallTexture = std::make_unique<QOpenGLTexture>(QImage(":/image/point.png"), QOpenGLTexture::GenerateMipMaps);
    caliBallTexture->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::Repeat);
    caliBallTexture->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::Repeat);
    caliBallTexture->setMinificationFilter(QOpenGLTexture::Linear);
    caliBallTexture->setMagnificationFilter(QOpenGLTexture::Linear);

    ballCenterTexture = std::make_unique<QOpenGLTexture>(QImage(":/image/red_point.png"), QOpenGLTexture::GenerateMipMaps);
    ballCenterTexture->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::Repeat);
    ballCenterTexture->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::Repeat);
    ballCenterTexture->setMinificationFilter(QOpenGLTexture::Linear);
    ballCenterTexture->setMagnificationFilter(QOpenGLTexture::Linear);

    // shader configuration
    // --------------------
    lightingShader.bind();
    lightingShader.setUniformValue("material.diffuse", 0);
    lightingShader.setUniformValue("material.specular", 1);
    lightingShader.release();

    vbo.release();
    glEnable(GL_DEPTH_TEST);
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
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
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

    if(m_leftBuffer){
        m_frame = m_leftBuffer->toImage();
        emit frameChanged(m_frame);
    }

    m_frameCount += 1;
    if(m_frameCount > 100)
        m_frameCount = 0;
}

void VRRender::release()
{
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

void VRRender::renderEye(vr::Hmd_Eye eye)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);        //启用混合状态
    glEnable(GL_DEPTH_TEST);    //启用深度检测
    glEnable(GL_ALPHA_TEST);  // Enable Alpha Testing (To Make BlackTansparent)
    glAlphaFunc(GL_GREATER, 0.1f);  // Set Alpha Testing (To Make Black Transparent)

    // be sure to activate shader when setting uniforms/drawing objects
    lightingShader.bind();
    lightingShader.setUniformValue("light.position", lightPos);

    QVector4D hmdPosition;
    QVector4D eyePosition;
    QMatrix4x4 model, view, projection;
    model = m_hmdPose.inverted() * model;
    model.translate(0,0,-CALIB_DEPTH);
    if(eye == vr::Hmd_Eye::Eye_Left){
        projection = m_leftProjection;
        view = m_leftPose * m_hmdPose;
    } else {
        projection = m_rightProjection;
        view = m_rightPose * m_hmdPose;
    }
    hmdPosition = m_hmdPose * QVector4D(0.0f, 0.0f, 0.0f, 1.0f);
    eyePosition = view * QVector4D(0.0f, 0.0f, 0.0f, 1.0f);

    lightingShader.setUniformValue("viewPos",  eyePosition);

    // light properties
    lightingShader.setUniformValue("light.ambient", QVector3D(0.2f, 0.2f, 0.2f));
    lightingShader.setUniformValue("light.diffuse", QVector3D(0.5f, 0.5f, 0.5f));
    lightingShader.setUniformValue("light.specular", QVector3D(1.0f, 1.0f, 1.0f));

    // material properties
    lightingShader.setUniformValue("material.shininess", 64.0f);
    lightingShader.setUniformValue("projection", projection);
    lightingShader.setUniformValue("view", view);
    lightingShader.setUniformValue("model", model);

    // bind diffuse map
    glActiveTexture(GL_TEXTURE0);
    caliBallTexture->bind();

    glActiveTexture(GL_TEXTURE1);
    ballCenterTexture->bind();

    {// render the cube
        QOpenGLVertexArrayObject::Binder vaoBind(&cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    lightingShader.release();
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
    if (eye == vr::Eye_Left)
        return m_leftProjection * m_leftPose * m_hmdPose;
    else
        return m_rightProjection * m_rightPose * m_hmdPose;
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

QVector<GLfloat> VRRender::drawCircle(float x, float y, float z, float r, int lineSegmentCount)
{
    return QVector<GLfloat>();
}

bool VRRender::createShader()
{
    bool success = lightingShader.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shader/shader.vert");
    if (!success) {
        qDebug() << "shaderProgram addShaderFromSourceFile failed!" << lightingShader.log();
        return success;
    }

    success = lightingShader.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shader/shader.frag");
    if (!success) {
        qDebug() << "shaderProgram addShaderFromSourceFile failed!" << lightingShader.log();
        return success;
    }

    success = lightingShader.link();
    if(!success) {
        qDebug() << "shaderProgram link failed!" << lightingShader.log();
    }

    return success;
}
