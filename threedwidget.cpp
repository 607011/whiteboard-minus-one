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
#include <QGLFramebufferObject>
#include <QGLShaderProgram>
#include <QMatrix4x4>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QVector2D>
#include <QVector3D>


static const QVector3D Vertices[4] = {
  QVector3D(-1.6f, -1.2f, 0.f),
  QVector3D(-1.6f,  1.2f, 0.f),
  QVector3D( 1.6f, -1.2f, 0.f),
  QVector3D( 1.6f,  1.2f, 0.f)
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
  0.0f, -0.5f,  0.0f,
 -0.5f,  3.0f, -0.5f,
  0.0f, -0.5f,  0.0f
};
static const QVector3D TooNearColor = QVector3D(138.f, 158.f, 9.f) / 255.f;
static const QVector3D TooFarColor = QVector3D(158.f, 9.f, 138.f) / 255.f;
static const QVector3D InvalidDepthColor = QVector3D(9.f, 138.f, 158.f) / 255.f;
static const int PROGRAM_VERTEX_ATTRIBUTE = 0;
static const int PROGRAM_TEXCOORD_ATTRIBUTE = 1;
static const int MaxVideoFrameLag = 10;
static const int MaxMergedDepthFrames = 6;

class ThreeDWidgetPrivate {
public:
  ThreeDWidgetPrivate(void)
    : xRot(180.f)
    , yRot(180.f)
    , xTrans(0.f)
    , yTrans(0.f)
    , zoom(-2.93f)
    , depthImage(DepthWidth, DepthHeight, QImage::Format_ARGB32)
    , imageFBO(nullptr)
    , mixShaderProgram(nullptr)
    , videoFrameSize(QVector2D(ColorWidth, ColorHeight))
    , depthFrameSize(QVector2D(DepthWidth, DepthHeight))
    , timestamp(0)
    , firstPaintEventPending(true)
  {
    // ...
  }
  ~ThreeDWidgetPrivate()
  {
    SafeDelete(mixShaderProgram);
    SafeDelete(imageFBO);
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

  GLuint videoTextureHandle;
  int locVideoTexture;
  GLuint depthTextureHandle;
  int locDepthTexture;

  QImage depthImage;

  QGLFramebufferObject *imageFBO;
  QGLShaderProgram *mixShaderProgram;

  QVector2D videoFrameSize;
  QVector2D depthFrameSize;

  int locNearThreshold;
  int locFarThreshold;
  int locGamma;
  int locContrast;
  int locSaturation;
  int locMatrix;

  QPoint lastMousePos;

  INT64 timestamp;

  GLenum GLerror;

  bool firstPaintEventPending;
};


ThreeDWidget::ThreeDWidget(QWidget *parent)
  : QGLWidget(QGLFormat(QGL::SingleBuffer | QGL::NoDepthBuffer | QGL::AlphaChannel
                        | QGL::NoAccumBuffer | QGL::NoStencilBuffer | QGL::NoStereoBuffers
                        | QGL::HasOverlay | QGL::NoSampleBuffers), parent)
  , d_ptr(new ThreeDWidgetPrivate)
{
  setFocusPolicy(Qt::StrongFocus);
  setFocus(Qt::OtherFocusReason);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(ColorWidth, ColorHeight);
  setMinimumSize(ColorWidth / 8, ColorHeight / 8);
}


ThreeDWidget::~ThreeDWidget()
{
  Q_D(ThreeDWidget);
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

  Q_ASSERT_X(d->mixShaderProgramIsValid(), "error in shader program", "error in shader program");

  d->locVideoTexture = d->mixShaderProgram->uniformLocation("uVideoTexture");
  Q_ASSERT(d->locVideoTexture != GL_INVALID_INDEX);
  d->mixShaderProgram->setUniformValue(d->locVideoTexture, 0);
  d->locDepthTexture = d->mixShaderProgram->uniformLocation("uDepthTexture");
  Q_ASSERT(d->locDepthTexture != GL_INVALID_INDEX);
  d->mixShaderProgram->setUniformValue(d->locDepthTexture, 1);
  qDebug() << "texture locations:" << d->locVideoTexture << d->locDepthTexture;

  d->locGamma = d->mixShaderProgram->uniformLocation("uGamma");
  d->locContrast = d->mixShaderProgram->uniformLocation("uContrast");
  d->locSaturation = d->mixShaderProgram->uniformLocation("uSaturation");
  d->locNearThreshold = d->mixShaderProgram->uniformLocation("uNearThreshold");
  d->locFarThreshold = d->mixShaderProgram->uniformLocation("uFarThreshold");
  d->locMatrix = d->mixShaderProgram->uniformLocation("uMatrix");
}


void ThreeDWidget::initializeGL(void)
{
  Q_D(ThreeDWidget);

  initializeOpenGLFunctions();

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
//  glEnable(GL_ALPHA_TEST);
//  glEnable(GL_BLEND);
//  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glGenTextures(1, &d->videoTextureHandle);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glActiveTexture(GL_TEXTURE0);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");

  glGenTextures(1, &d->depthTextureHandle);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glActiveTexture(GL_TEXTURE1);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "OpenGL error", "OpenGL error");

  d->imageFBO = new QGLFramebufferObject(ColorWidth, ColorHeight);

  makeWorldMatrix();
  makeShader();
}


void ThreeDWidget::resizeGL(int width, int height)
{
  Q_UNUSED(width);
  Q_UNUSED(height);
  // ...
}


void ThreeDWidget::paintGL(void)
{
  Q_D(ThreeDWidget);

  if (d->firstPaintEventPending) {
    d->firstPaintEventPending = false;
    GLint GLMajorVer = 0, GLMinorVer = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &GLMajorVer);
    glGetIntegerv(GL_MINOR_VERSION, &GLMinorVer);
    qDebug() << QString("OpenGL %1.%2").arg(GLMajorVer).arg(GLMinorVer);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Multitexture) ==" << hasOpenGLFeature(QOpenGLFunctions::Multitexture);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Shaders) ==" << hasOpenGLFeature(QOpenGLFunctions::Shaders);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Framebuffers) ==" << hasOpenGLFeature(QOpenGLFunctions::Framebuffers);
    GLint h0 = 0, h1 = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h1);
    qDebug() << h0 << h1;
    emit ready();
  }

  glClearColor(.15f, .15f, .15f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (d->imageFBO == nullptr || d->mixShaderProgram == nullptr || d->timestamp == 0)
    return;

  d->mixShaderProgram->setAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE, TexCoords);
  d->mixShaderProgram->setUniformValue(d->locMatrix, d->mvMatrix);
  d->mixShaderProgram->bind();

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);

  glViewport(0, 0, width(), height());
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  d->imageFBO->bind();
  glViewport(0, 0, ColorWidth, ColorHeight);
  d->mixShaderProgram->setUniformValue(d->locMatrix, QMatrix4x4());
  d->mixShaderProgram->setAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE, TexCoords4FBO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  d->imageFBO->release();
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


void ThreeDWidget::setVideoData(INT64 nTime, const uchar *pBuffer, int nWidth, int nHeight)
{
  Q_D(ThreeDWidget);

  if (nWidth != ColorWidth || nHeight != ColorHeight || pBuffer == nullptr)
    return;

  d->timestamp = nTime;
  glActiveTexture(GL_TEXTURE0);
  // glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, nWidth, nHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, pBuffer);

  QImage(pBuffer, nWidth, nHeight, QImage::Format_ARGB32).save(QString("images/%1.jpg").arg(nTime, 14, 10, QChar('0')), "JPG", 50);
  updateGL();
}


void ThreeDWidget::setDepthData(INT64 nTime, const UINT16 *pBuffer, int nWidth, int nHeight, int nMinDepth, int nMaxDepth)
{
  Q_D(ThreeDWidget);
  Q_UNUSED(nTime);
  Q_UNUSED(nMinDepth);
  Q_UNUSED(nMaxDepth);

  if (nWidth != DepthWidth || nHeight != DepthHeight || pBuffer == nullptr)
    return;

  glActiveTexture(GL_TEXTURE1);
  // glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, nWidth, nHeight, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pBuffer);

#if 1
  // d->depthImage.save(QString("images/%1.jpg").arg(nTime, 14, 10, QChar('0')), "JPG", 50);
#else
  d->imageFBO->toImage().save(QString("images/%1.jpg").arg(d->timestamp, 16, 10, QChar('0')), "JPG", 50);
#endif

}


void ThreeDWidget::setContrast(GLfloat contrast)
{
  Q_D(ThreeDWidget);
  d->mixShaderProgram->setUniformValue(d->locContrast, contrast);
  qDebug() << "ThreeDWidget::setContrast(" << contrast << ")";
  updateGL();
}


void ThreeDWidget::setSaturation(GLfloat saturation)
{
  Q_D(ThreeDWidget);
  d->mixShaderProgram->setUniformValue(d->locSaturation, saturation);
  qDebug() << "ThreeDWidget::setSaturation(" << saturation << ")";
  updateGL();
}


void ThreeDWidget::setGamma(GLfloat gamma)
{
  Q_D(ThreeDWidget);
  d->mixShaderProgram->setUniformValue(d->locGamma, gamma);
  qDebug() << "ThreeDWidget::setGamma(" << gamma << ")";
  updateGL();
}


void ThreeDWidget::setNearThreshold(GLuint nearThreshold)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->locNearThreshold, (GLfloat)nearThreshold);
  qDebug() << "ThreeDWidget::setNearThreshold(" << nearThreshold << ")";
  updateGL();
}


void ThreeDWidget::setFarThreshold(GLuint farThreshold)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->mixShaderProgram->setUniformValue(d->locFarThreshold, (GLfloat)farThreshold);
  qDebug() << "ThreeDWidget::setFarThreshold(" << farThreshold << ")";
  updateGL();
}
