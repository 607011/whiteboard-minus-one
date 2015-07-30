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

#ifndef __THREEDWIDGET_H_
#define __THREEDWIDGET_H_

#include <QScopedPointer>
#include <QGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>

#include <Kinect.h>

#include "globals.h"

class ThreeDWidgetPrivate;

class ThreeDWidget : public QGLWidget, protected QOpenGLFunctions
{
  Q_OBJECT

public:
  explicit ThreeDWidget(QWidget *parent = nullptr);
   ~ThreeDWidget();

  virtual QSize minimumSizeHint(void) const { return QSize(ColorWidth / 4, ColorHeight / 4); }
  virtual QSize sizeHint(void) const { return QSize(ColorWidth, ColorHeight); }

  void setVideoData(INT64 nTime, const uchar *pBuffer, int nWidth, int nHeight);
  void setDepthData(INT64 nTime, const UINT16 *pBuffer, int nWidth, int nHeight, int nMinDepth, int nMaxDepth);

  void setContrast(GLfloat);
  void setSaturation(GLfloat);
  void setGamma(GLfloat);

  void setNearThreshold(GLuint);
  void setFarThreshold(GLuint);

signals:
  void ready(void);

protected:
  void initializeGL(void);
  void resizeGL(int w, int h);
  void paintGL(void);
  void mousePressEvent(QMouseEvent*);
  void mouseReleaseEvent(QMouseEvent*);
  void mouseMoveEvent(QMouseEvent*);
  void wheelEvent(QWheelEvent*);

private:
  QScopedPointer<ThreeDWidgetPrivate> d_ptr;
  Q_DECLARE_PRIVATE(ThreeDWidget)
  Q_DISABLE_COPY(ThreeDWidget)

  void makeShader(void);
  void makeWorldMatrix(void);
};


#endif // __THREEDWIDGET_H_
