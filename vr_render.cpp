#include <QDebug>
#include "vr_render.h"

const float NEAR_CLIP = 0.1f;
const float FAR_CLIP = 10000.0f;

VRRender::VRRender(QObject *parent)
    : QObject(parent)
    ,m_frame(QImage())
    ,m_frameSize(QSize(0,0))
    ,m_aspectRatio(0)
    ,m_frameCount(0)
    ,m_surfaceFormat(QSurfaceFormat())
    ,m_openGLContext(nullptr)
    ,m_fbo(nullptr)
    ,m_vbo(nullptr), m_vao(nullptr)
    ,m_shader(nullptr)
    ,m_texture(nullptr)
    ,camera_pos(0.0f, 3.0f, 0.0f)
    ,light_pos(0.0f, 5.0f, 0.0f)
{
    initGL();
}

VRRender::~VRRender()
{
    if(m_texture){
        delete  m_texture;
        m_texture = nullptr;
    }

    if(m_fbo){
        delete m_fbo;
        m_fbo = nullptr;
    }
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

    const GLfloat VERTEX_INIT_DATA[] = {
        //face 1
        -0.5f, 0.0f, -0.2887f,
        0.0f, 0.0f, 0.5774f,
        0.5f, 0.0f, -0.2887f,
        //face 2
        -0.5f, 0.0f, -0.2887f,
        0.5f, 0.0f, -0.2887f,
        0.0f, 0.8165f, 0.0f,
        //face 3
        -0.5f, 0.0f, -0.2887f,
        0.0f, 0.8165f, 0.0f,
        0.0f, 0.0f, 0.5774f,
        //face 4
        0.5f, 0.0f, -0.2887f,
        0.0f, 0.0f, 0.5774f,
        0.0f, 0.8165f, 0.0f,
    };
    const GLfloat UV_INIT_DATA[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    };
    memcpy(this->vertexData, VERTEX_INIT_DATA, sizeof(this->vertexData));
    memset(this->normalBuffer, 0, sizeof(this->normalBuffer));
    computeNormalVectors(4);
    memcpy(this->uvData, UV_INIT_DATA, sizeof(this->uvData));

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

    QOpenGLFunctions *f = m_openGLContext.functions();
    f->glEnable(GL_DEPTH_TEST);
    f->glViewport(0,0,m_frameSize.width(), m_frameSize.height());
    m_texture = new QOpenGLTexture(QImage(":/textures/lenna.png"));
    m_shader = new QOpenGLShaderProgram();
    m_shader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/vertexShader.shader");
    m_shader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/fragmentShader.shader");
    if (m_shader->link()) {
        qDebug("Shaders link success.");
    } else {
        qDebug("Shaders link failed!");
    }
    m_vao = new QOpenGLVertexArrayObject();
    m_vbo = new QOpenGLBuffer(QOpenGLBuffer::Type::VertexBuffer);
    m_cbo = new QOpenGLBuffer(QOpenGLBuffer::Type::VertexBuffer);
    m_uvbo = new QOpenGLBuffer(QOpenGLBuffer::Type::VertexBuffer);
    m_vao->create();
    m_vao->bind();
    m_vbo->create();
    m_vbo->bind();
    m_vbo->allocate(this->vertexData, 4 * 3 * 3 * sizeof(GLfloat));
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,3*sizeof(GLfloat), 0);
    m_vbo->release();
    m_cbo->create();
    m_cbo->bind();
    m_cbo->allocate(this->normalBuffer, 4*3*3*sizeof(GLfloat));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3*sizeof(GLfloat), 0);
    m_cbo->release();
    m_uvbo->create();
    m_uvbo->bind();
    m_uvbo->allocate(this->uvData, 4 * 3 * 2 * sizeof(GLfloat));
    f->glEnableVertexAttribArray(2);
    f->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2*sizeof(GLfloat), 0);
    m_uvbo->release();
    m_vao->release();

    renderImage();
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

    QOpenGLFramebufferObjectFormat fboFormat;
    fboFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    m_fbo = new QOpenGLFramebufferObject(m_frameSize, fboFormat);
    m_fbo->bind();

    QOpenGLFunctions *f = m_openGLContext.functions();

    f->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    f->glClearColor(0.0f, 0.2f, 0.0f, 1.0f);
    m_vao->bind();
    m_shader->bind();
    m_texture->bind();
    QMatrix4x4 vpMat;
    vpMat.perspective(45.0f, m_aspectRatio, 0.1f, 1000.0f);
    QVector3D center(0.0, 0.0, 0.0);
    center.setY(0);
    vpMat.lookAt(camera_pos, center, QVector3D(1.0f, 0.0f, 0.0f));
    QMatrix4x4 modelMat;
    float process = m_frameCount / 100.0f;
    modelMat.rotate(40.0f * process, QVector3D(0.7f, 0.5f, 0.2f));

    m_shader->setUniformValue(m_shader->uniformLocation("vpMat"), vpMat);
    m_shader->setUniformValue(m_shader->uniformLocation("modelMat"), modelMat);
    m_shader->setUniformValue(m_shader->uniformLocation("lightPos"), light_pos);
    m_shader->setUniformValue(m_shader->uniformLocation("cameraPos"), camera_pos);
    f->glDrawArrays(GL_TRIANGLES, 0, 4 * 3);
    m_texture->release();
    m_shader->release();
    m_vao->release();
    m_fbo->release();
    m_frame = m_fbo->toImage();
    emit frameChanged(m_frame);

    if(m_fbo){
        delete m_fbo;
        m_fbo = nullptr;
    }

    m_frameCount += 1;
    if(m_frameCount > 100)
        m_frameCount = 0;
}

void VRRender::computeNormalVectors(size_t num_vertices)
{
    for (size_t i=0;i<num_vertices;++i){
        GLfloat v1x = this->vertexData[i * 9];
        GLfloat v1y = this->vertexData[i * 9 + 1];
        GLfloat v1z = this->vertexData[i * 9 + 2];

        GLfloat v2x = this->vertexData[i * 9 + 3];
        GLfloat v2y = this->vertexData[i * 9 + 4];
        GLfloat v2z = this->vertexData[i * 9 + 5];

        GLfloat v3x = this->vertexData[i * 9 + 6];
        GLfloat v3y = this->vertexData[i * 9 + 7];
        GLfloat v3z = this->vertexData[i * 9 + 8];

        GLfloat x1 = v2x - v1x, y1 = v2y - v1y, z1 = v2z - v1z;
        GLfloat x2 = v3x - v1x, y2 = v3y - v1y, z2 = v3z - v1z;
        GLfloat nx = y1 *z2 - z1 * y2;
        GLfloat ny = z1 * x2 - x1 * z2;
        GLfloat nz = x1 * y2 - y1 * x2;
        for (int j=0;j<3;++j){
            this->normalBuffer[i * 9 + j * 3] = nx;
            this->normalBuffer[i * 9 + j * 3 + 1] = ny;
            this->normalBuffer[i * 9 + j * 3 + 2] = nz;
        }
    }
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
