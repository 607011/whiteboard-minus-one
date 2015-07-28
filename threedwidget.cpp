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

#include "globals.h"
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
    , nearThreshold(0)
    , farThreshold(0xffff)
    , timestamp(0)
  {
    // ...
  }
  ~ThreeDWidgetPrivate()
  {
    SafeDelete(mixShaderProgram);
    SafeDelete(imageFBO);
  }

  GLfloat xRot;
  GLfloat yRot;
  GLfloat xTrans;
  GLfloat yTrans;
  GLfloat zoom;
  QMatrix4x4 mvMatrix;

  GLuint videoTextureHandle;
  GLint uVideoTexture;
  GLuint depthTextureHandle;
  GLint uDepthTexture;

  QImage depthImage;
//  GLuint imageTextureHandle;
//  GLint uImageTexture;

  QGLFramebufferObject *imageFBO;
  QGLShaderProgram *mixShaderProgram;

  QVector2D videoFrameSize;
  QVector2D depthFrameSize;

  int nearThreshold;
  int farThreshold;

  QPoint lastMousePos;

  INT64 timestamp;

  GLenum GLerror;
};


ThreeDWidget::ThreeDWidget(QWidget *parent)
  : QGLWidget(parent)
  , d_ptr(new ThreeDWidgetPrivate)
{
  setFocus(Qt::OtherFocusReason);
  setCursor(Qt::OpenHandCursor);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(ColorWidth, ColorHeight);
  setMinimumSize(ColorWidth / 8, ColorHeight / 8);
}


ThreeDWidget::~ThreeDWidget()
{
  // ...
}


void ThreeDWidget::makeShader(void)
{
  Q_D(ThreeDWidget);
  qDebug() << "Making mixShaderProgram ...";
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

  d->uVideoTexture = glGetUniformLocation(d->mixShaderProgram->programId(), "uVideoTexture");
  glUniform1i(d->uVideoTexture, 0);

  d->uDepthTexture = glGetUniformLocation(d->mixShaderProgram->programId(), "uDepthTexture");
  glUniform1i(d->uDepthTexture, 1);

  // d->uImageTexture = glGetUniformLocation(d->mixShaderProgram->programId(), "uImageTexture");
  // glUniform1i(d->uImageTexture, 2);

  setGamma(2.1f);
  setSaturation(1.f);
  setContrast(1.f);

  makeWorldMatrix();
}


void ThreeDWidget::initializeGL(void)
{
  Q_D(ThreeDWidget);

  initializeOpenGLFunctions();

  GLint GLMajorVer, GLMinorVer;
  glGetIntegerv(GL_MAJOR_VERSION, &GLMajorVer);
  glGetIntegerv(GL_MINOR_VERSION, &GLMinorVer);
  qDebug() << QString("OpenGL %1.%2").arg(GLMajorVer).arg(GLMinorVer);

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &d->videoTextureHandle);
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glActiveTexture(GL_TEXTURE1);
  glGenTextures(1, &d->depthTextureHandle);
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

//  glActiveTexture(GL_TEXTURE2);
//  glGenTextures(1, &d->imageTextureHandle);
//  glBindTexture(GL_TEXTURE_2D, d->imageTextureHandle);
//  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
//  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  d->imageFBO = new QGLFramebufferObject(ColorWidth, ColorHeight);

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
  glClearColor(.2f, .3f, .5f, .5f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (d->imageFBO == nullptr || d->mixShaderProgram == nullptr)
    return;

  d->mixShaderProgram->setUniformValue("uMatrix", d->mvMatrix);
  d->mixShaderProgram->bind();

  glViewport(0, 0, width(), height());
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  d->imageFBO->bind();
  glViewport(0, 0, ColorWidth, ColorHeight);
  d->mixShaderProgram->setUniformValue("uMatrix", QMatrix4x4());
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  d->imageFBO->release();
  // d->imageFBO->toImage().save(QString("%1.jpg").arg(d->timestamp, 16, 10, QChar('0')), "JPG", 50);
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
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, nWidth, nHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, pBuffer);
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
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
#if 1
  QRgb *dst = reinterpret_cast<QRgb*>(d->depthImage.bits());
  const UINT16 *src = pBuffer;
  const UINT16 *srcEnd = pBuffer + DepthSize;
  while (src < srcEnd) {
    int depth = int(*src);
    if (depth == 0 || depth == USHRT_MAX) {
      *dst = qRgb(127, 250, 99);
    }
    else {
      const int lightness = 255 - 255 * depth / nMaxDepth;
      *dst = qRgb(lightness, lightness, lightness);
    }
    ++dst;
    ++src;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, nWidth, nHeight, 0, GL_RGBA, GL_RGBA, dst);
#else
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, nWidth, nHeight, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pBuffer);
#endif
  d->depthImage.save(QString("images/%1.jpg").arg(nTime, 14, 10, QChar('0')), "JPG", 50);
}


void ThreeDWidget::setContrast(GLfloat contrast)
{
  Q_D(ThreeDWidget);
  d->mixShaderProgram->setUniformValue("uContrast", contrast);
  updateGL();
}


void ThreeDWidget::setSaturation(GLfloat saturation)
{
  Q_D(ThreeDWidget);
  d->mixShaderProgram->setUniformValue("uSaturation", saturation);
  updateGL();
}


void ThreeDWidget::setGamma(GLfloat gamma)
{
  Q_D(ThreeDWidget);
  d->mixShaderProgram->setUniformValue("uGamma", gamma);
  updateGL();
}
