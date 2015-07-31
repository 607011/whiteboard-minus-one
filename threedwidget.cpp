/*

    Copyright (c) 2015 Oliver Lau <ola@ct.de>, Heise Medien GmbH & Co. KG

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "util.h"
#include "threedwidget.h"

#include <QDebug>
#include <QString>
#include <QGLFramebufferObject>
#include <QGLShaderProgram>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
#include <QMutexLocker>

#include <Kinect.h>

static const QVector3D Vertices[4] = {
  QVector3D(-1.92f, -1.08f, 0.f),
  QVector3D(-1.92f,  1.08f, 0.f),
  QVector3D( 1.92f, -1.08f, 0.f),
  QVector3D( 1.92f,  1.08f, 0.f)
};
static const QVector2D TexCoords4FBO[4] =
{
  QVector2D(1, 1),
  QVector2D(1, 0),
  QVector2D(0, 1),
  QVector2D(0, 0)
};
static const QVector2D TexCoords[4] = {
  QVector2D(0, 0),
  QVector2D(0, 1),
  QVector2D(1, 0),
  QVector2D(1, 1)
};
static const QVector2D Offset[9] = {
  QVector2D(1,  1), QVector2D(0 , 1), QVector2D(-1,  1),
  QVector2D(1,  0), QVector2D(0,  0), QVector2D(-1,  0),
  QVector2D(1, -1), QVector2D(0, -1), QVector2D(-1, -1)
};
static GLfloat SharpeningKernel[9] = {
  0.f, -.5f,  0.f,
  -.5f, 3.f, -.5f,
  0.f, -.5f,  0.f
};
static const int PROGRAM_VERTEX_ATTRIBUTE = 0;
static const int PROGRAM_TEXCOORD_ATTRIBUTE = 1;

struct DSP {
  DSP(void)
    : x(0)
    , y(0)
  { /* ... */ }
  DSP(UINT16 x, UINT16 y)
    : x(x)
    , y(y)
  { /* ... */ }
  UINT16 x;
  UINT16 y;
};

class ThreeDWidgetPrivate {
public:
  ThreeDWidgetPrivate(void)
    : xRot(180.f)
    , yRot(180.f)
    , xTrans(0.f)
    , yTrans(0.f)
    , zoom(-2.93f)
    , imageFBO(nullptr)
    , mixShaderProgram(nullptr)
    , depthSpaceData(new DepthSpacePoint[ColorSize])
    , depthSpaceDataINT16(new DSP[ColorSize])
    , timestamp(0)
    , firstPaintEventPending(true)
  {
    // ...
  }
  ~ThreeDWidgetPrivate()
  {
    SafeRelease(coordinateMapper);
    SafeDelete(mixShaderProgram);
    SafeDelete(imageFBO);
    SafeDeleteArray(depthSpaceData);
  }

  bool mixShaderProgramIsValid(void) const {
    return mixShaderProgram != nullptr && mixShaderProgram->isLinked();
  }

  GLfloat xRot;
  GLfloat yRot;
  GLfloat xTrans;
  GLfloat yTrans;
  GLfloat zoom;
  QMatrix4x4 mvMatrix;

  QGLFramebufferObject *imageFBO;
  QGLShaderProgram *mixShaderProgram;

  IKinectSensor *kinectSensor;
  ICoordinateMapper *coordinateMapper;
  DepthSpacePoint *depthSpaceData;
  DSP *depthSpaceDataINT16;

  GLuint imageTextureHandle;
  GLuint videoTextureHandle;
  GLuint depthTextureHandle;
  GLuint mapTextureHandle;

  int imageTextureLocation;
  int videoTextureLocation;
  int depthTextureLocation;
  int mapTextureLocation;
  int nearThresholdLocation;
  int farThresholdLocation;
  int gammaLocation;
  int contrastLocation;
  int saturationLocation;
  int offsetLocation;
  int sharpenLocation;
  int mvMatrixLocation;
  int matchFramesLocation;

  QPoint lastMousePos;
  INT64 timestamp;
  bool firstPaintEventPending;
};


static const QGLFormat DefaultGLFormat = QGLFormat(QGL::SingleBuffer | QGL::NoDepthBuffer | QGL::AlphaChannel | QGL::NoAccumBuffer | QGL::NoStencilBuffer | QGL::NoStereoBuffers | QGL::HasOverlay | QGL::NoSampleBuffers);

ThreeDWidget::ThreeDWidget(QWidget *parent)
  : QGLWidget(DefaultGLFormat, parent)
  , d_ptr(new ThreeDWidgetPrivate)
{
  Q_D(ThreeDWidget);
  setFocusPolicy(Qt::StrongFocus);
  setFocus(Qt::OtherFocusReason);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(ColorWidth, ColorHeight);
  setMinimumSize(ColorWidth / 8, ColorHeight / 8);
  HRESULT hr = GetDefaultKinectSensor(&d->kinectSensor);
  if (SUCCEEDED(hr) && d->kinectSensor != nullptr)
    hr = d->kinectSensor->get_CoordinateMapper(&d->coordinateMapper);
}


ThreeDWidget::~ThreeDWidget()
{
}


void ThreeDWidget::makeShader(void)
{
  Q_D(ThreeDWidget);
  qDebug() << "ThreeDWidget::makeShader()";
  SafeRenew(d->mixShaderProgram, new QGLShaderProgram);
  d->mixShaderProgram->addShaderFromSourceFile(QGLShader::Fragment, ":/shaders/mixfragmentshader.glsl");
  d->mixShaderProgram->addShaderFromSourceFile(QGLShader::Vertex, ":/shaders/mixvertexshader.glsl");
  d->mixShaderProgram->bindAttributeLocation("aVertex", PROGRAM_VERTEX_ATTRIBUTE);
  d->mixShaderProgram->bindAttributeLocation("aTexCoord", PROGRAM_TEXCOORD_ATTRIBUTE);
  d->mixShaderProgram->enableAttributeArray(PROGRAM_VERTEX_ATTRIBUTE);
  d->mixShaderProgram->enableAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE);
  d->mixShaderProgram->setAttributeArray(PROGRAM_VERTEX_ATTRIBUTE, Vertices);
  d->mixShaderProgram->setAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE, TexCoords);
  d->mixShaderProgram->link();
  d->mixShaderProgram->bind();

  qDebug() << "Shader linker says:" << d->mixShaderProgram->log();
  Q_ASSERT_X(d->mixShaderProgramIsValid(), "ThreeDWidget::makeShader()", "error in shader program");

  d->videoTextureLocation = d->mixShaderProgram->uniformLocation("uVideoTexture");
  d->mixShaderProgram->setUniformValue(d->videoTextureLocation, 0);

  d->depthTextureLocation = d->mixShaderProgram->uniformLocation("uDepthTexture");
  d->mixShaderProgram->setUniformValue(d->depthTextureLocation, 1);

  d->mapTextureLocation = d->mixShaderProgram->uniformLocation("uMapTexture");
  d->mixShaderProgram->setUniformValue(d->mapTextureLocation, 2);

  qDebug() << "texture locations:" << d->videoTextureLocation << d->depthTextureLocation << d->mapTextureLocation;

  d->gammaLocation = d->mixShaderProgram->uniformLocation("uGamma");
  d->contrastLocation = d->mixShaderProgram->uniformLocation("uContrast");
  d->saturationLocation = d->mixShaderProgram->uniformLocation("uSaturation");
  d->nearThresholdLocation = d->mixShaderProgram->uniformLocation("uNearThreshold");
  d->farThresholdLocation = d->mixShaderProgram->uniformLocation("uFarThreshold");
  d->mvMatrixLocation = d->mixShaderProgram->uniformLocation("uMatrix");
  d->matchFramesLocation = d->mixShaderProgram->uniformLocation("uMatchFrames");

  d->sharpenLocation = d->mixShaderProgram->uniformLocation("uSharpen");
  d->mixShaderProgram->setUniformValueArray(d->sharpenLocation, SharpeningKernel, 9, 1);
  d->offsetLocation = d->mixShaderProgram->uniformLocation("uOffset");
  d->mixShaderProgram->setUniformValueArray(d->offsetLocation, Offset, 9);
}


void ThreeDWidget::initializeGL(void)
{
  Q_D(ThreeDWidget);

  initializeOpenGLFunctions();

  glEnable(GL_ALPHA_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glGenTextures(1, &d->videoTextureHandle);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glGenTextures(1, &d->depthTextureHandle);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glGenTextures(1, &d->mapTextureHandle);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, d->mapTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glGenTextures(1, &d->imageTextureHandle);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, d->imageTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  d->imageFBO = new QGLFramebufferObject(ColorWidth, ColorHeight);

  makeWorldMatrix();
  makeShader();
}


void ThreeDWidget::resizeGL(int width, int height)
{
  Q_UNUSED(width);
  Q_UNUSED(height);
  qDebug() << "ThreeDWidget::resizeGL(" << width << "," << height << ")";
}


void ThreeDWidget::paintGL(void)
{
  Q_D(ThreeDWidget);

  makeCurrent();

  if (d->firstPaintEventPending) {
    d->firstPaintEventPending = false;
    GLint GLMajorVer, GLMinorVer;
    glGetIntegerv(GL_MAJOR_VERSION, &GLMajorVer);
    glGetIntegerv(GL_MINOR_VERSION, &GLMinorVer);
    qDebug() << QString("OpenGL %1.%2").arg(GLMajorVer).arg(GLMinorVer);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Multitexture) ==" << hasOpenGLFeature(QOpenGLFunctions::Multitexture);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Shaders) ==" << hasOpenGLFeature(QOpenGLFunctions::Shaders);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Framebuffers) ==" << hasOpenGLFeature(QOpenGLFunctions::Framebuffers);
    qDebug() << "doubleBuffer() ==" << doubleBuffer();
    GLint h0 = 0, h1 = 0, h2 = 0, h3 = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h1);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h2);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h3);
    qDebug() << h0 << h1 << h2 << h3;
    emit ready();
  }
  else {
    glClearColor(.15f, .15f, .15f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (d->imageFBO == nullptr || d->mixShaderProgram == nullptr || d->timestamp == 0)
      return;

    d->mixShaderProgram->setAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE, TexCoords);
    d->mixShaderProgram->setUniformValue(d->mvMatrixLocation, d->mvMatrix);
    d->mixShaderProgram->bind();

    glViewport(0, 0, width(), height());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    d->imageFBO->bind();
    glViewport(0, 0, ColorWidth, ColorHeight);
    d->mixShaderProgram->setUniformValue(d->mvMatrixLocation, QMatrix4x4());
    d->mixShaderProgram->setAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE, TexCoords4FBO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    d->imageFBO->release();
  }
}


void ThreeDWidget::process(INT64 nTime, const uchar *pRGB, const UINT16 *pDepth, int nMinReliableDist, int nMaxDist)
{
  Q_D(ThreeDWidget);
  Q_UNUSED(nMinReliableDist);
  Q_UNUSED(nMaxDist);

  Q_ASSERT_X(pDepth != nullptr && pRGB != nullptr, "ThreeDWidget::process()", "RGB and depth pointers must not be null");

  d->timestamp = nTime;

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ColorWidth, ColorHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, pRGB);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "ThreeDWidget::process()", "glTexImage2D() failed");

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, DepthWidth, DepthHeight, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pDepth);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "ThreeDWidget::process()", "glTexImage2D() failed");

  HRESULT hr = d->coordinateMapper->MapColorFrameToDepthSpace(DepthSize, pDepth, ColorSize, d->depthSpaceData);
  if (FAILED(hr))
    qWarning() << "MapColorFrameToDepthSpace() failed.";
  for (int i = 0; i < ColorSize; ++i) {
    d->depthSpaceDataINT16[i].x = (INT16)d->depthSpaceData[i].X;
    d->depthSpaceDataINT16[i].y = (INT16)d->depthSpaceData[i].Y;
  }
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, d->mapTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16UI, ColorWidth, ColorHeight, 0, GL_RG_INTEGER, GL_UNSIGNED_SHORT, d->depthSpaceDataINT16);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "ThreeDWidget::process()", "glTexImage2D() failed");

  updateGL();
}


void ThreeDWidget::mousePressEvent(QMouseEvent *e)
{
  Q_D(ThreeDWidget);
  setCursor(Qt::ClosedHandCursor);
  d->lastMousePos = e->pos();
}


void ThreeDWidget::mouseReleaseEvent(QMouseEvent *)
{
  setCursor(Qt::OpenHandCursor);
}


void ThreeDWidget::mouseMoveEvent(QMouseEvent *e)
{
  Q_D(ThreeDWidget);
  if (e->buttons() & Qt::LeftButton) {
    d->xRot += .3f * (e->y() - d->lastMousePos.y());
    d->yRot += .3f * (e->x() - d->lastMousePos.x());
  }
  else if (e->buttons() & Qt::RightButton) {
    d->xTrans += .01f * (e->x() - d->lastMousePos.x());
    d->yTrans += .01f * (e->y() - d->lastMousePos.y());
  }
  d->lastMousePos = e->pos();
  makeWorldMatrix();
  updateGL();
}


void ThreeDWidget::wheelEvent(QWheelEvent *e)
{
  Q_D(ThreeDWidget);
  d->zoom += (e->delta() < 0 ? -1 : 1 ) * ((e->modifiers() & Qt::ShiftModifier)? .04f : .2f);
  makeWorldMatrix();
  updateGL();
}


void ThreeDWidget::makeWorldMatrix(void)
{
  Q_D(ThreeDWidget);
  d->mvMatrix.setToIdentity();
  d->mvMatrix.perspective(45.f, float(width()) / height(), .01f, 12.f);
  d->mvMatrix.translate(0.f, 0.f, d->zoom);
  d->mvMatrix.rotate(d->xRot, 1.f, 0.f, 0.f);
  d->mvMatrix.rotate(d->yRot, 0.f, 1.f, 0.f);
  d->mvMatrix.translate(d->xTrans, d->yTrans, 0.f);
}


void ThreeDWidget::setContrast(GLfloat contrast)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->contrastLocation, contrast);
  updateGL();
}


void ThreeDWidget::setSaturation(GLfloat saturation)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->saturationLocation, saturation);
  updateGL();
}


void ThreeDWidget::setGamma(GLfloat gamma)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->gammaLocation, gamma);
  updateGL();
}


void ThreeDWidget::setNearThreshold(GLuint nearThreshold)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->nearThresholdLocation, (GLfloat)nearThreshold);
  updateGL();
}


void ThreeDWidget::setFarThreshold(GLuint farThreshold)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->farThresholdLocation, (GLfloat)farThreshold);
  updateGL();
}


void ThreeDWidget::setMatchColorAndDepthSpace(bool doMatch)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->matchFramesLocation, doMatch);
  updateGL();
}
