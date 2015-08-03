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
#include "rgbdwidget.h"

#include <limits>

#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QMutexLocker>
#include <QVector3D>

class RGBDWidgetPrivate
{
public:
  RGBDWidgetPrivate(void)
    : colorSpaceData(new ColorSpacePoint[DepthSize])
    , depthSpaceData(new DepthSpacePoint[ColorSize])
    , depthData(new UINT16[DepthSize])
    , tooNear(1100)
    , tooFar(2400)
    , minDepth(0)
    , maxDepth(USHRT_MAX)
    , refPoints(NRefPoints)
    , ref3D(NRefPoints)
    , refPointIndex(0)
    , kinectSensor(nullptr)
    , coordinateMapper(nullptr)
    , windowAspectRatio(1.0)
    , imageAspectRatio(1.0)
  {
    // ...
  }
  ~RGBDWidgetPrivate()
  {
    SafeRelease(coordinateMapper);
    SafeDeleteArray(colorSpaceData);
    SafeDeleteArray(depthSpaceData);
    SafeDeleteArray(depthData);
  }

  QImage videoFrame;
  ColorSpacePoint *colorSpaceData;
  DepthSpacePoint *depthSpaceData;
  UINT16 *depthData;
  RGBQUAD *colorData;
  int colorDataSize;
  int tooNear;
  int tooFar;
  int minDepth;
  int maxDepth;

  static const int NRefPoints = 3;
  int refPointIndex;
  QVector<QPoint> refPoints;
  QVector<QVector3D> ref3D;
  QRect destRect;

  IKinectSensor *kinectSensor;
  ICoordinateMapper *coordinateMapper;

  qreal windowAspectRatio;
  qreal imageAspectRatio;

  QMutex mtx;
};


RGBDWidget::RGBDWidget(QWidget *parent)
  : QWidget(parent)
  , d_ptr(new RGBDWidgetPrivate)
{
  Q_D(RGBDWidget);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMinimumSize(DepthWidth / 2, DepthHeight / 2);
  d->videoFrame = QImage(ColorWidth, ColorHeight, QImage::Format_ARGB32);
  d->imageAspectRatio = qreal(d->videoFrame.width()) / qreal(d->videoFrame.height());
  setMaximumSize(d->videoFrame.size());

  HRESULT hr = GetDefaultKinectSensor(&d->kinectSensor);
  if (SUCCEEDED(hr) && d->kinectSensor != nullptr)
    hr = d->kinectSensor->get_CoordinateMapper(&d->coordinateMapper);
}


void RGBDWidget::setColorData(INT64 nTime, const uchar *pBuffer, int nWidth, int nHeight)
{
  Q_D(RGBDWidget);
  Q_UNUSED(nTime);

  if (nWidth != ColorWidth || nHeight != ColorHeight || pBuffer == nullptr || d->videoFrame.isNull())
    return;

  d->mtx.lock();
  quint32 *dst = reinterpret_cast<quint32*>(d->videoFrame.bits());
  static const QRgb defaultColor = qRgb(88, 250, 44);
  static const QRgb tooNearColor = qRgb(250, 44, 88);
  static const QRgb tooFarColor = qRgb(88, 44, 250);

  for (int colorIndex = 0; colorIndex < ColorSize; ++colorIndex) {
    const DepthSpacePoint &dsp = d->depthSpaceData[colorIndex];
    const quint32 *src = &defaultColor;
    if (dsp.X != -std::numeric_limits<float>::infinity() && dsp.Y != -std::numeric_limits<float>::infinity()) {
      const int dx = qRound(dsp.X);
      const int dy = qRound(dsp.Y);
      if (dx >= 0 && dx < DepthWidth && dy >= 0 && dy < DepthHeight) {
        const int depth = d->depthData[dx + dy * DepthWidth];
        if (depth < d->tooNear)
          src = &tooNearColor;
        else if (depth > d->tooFar)
          src = &tooFarColor;
        else
          src = reinterpret_cast<const quint32*>(pBuffer) + colorIndex;
      }
    }
    dst[colorIndex] = *src;
  }
  d->mtx.unlock();

  update();
}


void RGBDWidget::setNearThreshold(int value)
{
  Q_D(RGBDWidget);
  d->tooNear = value;
  update();
}


void RGBDWidget::setFarThreshold(int value)
{
  Q_D(RGBDWidget);
  d->tooFar = value;
  update();
}


void RGBDWidget::setDepthData(INT64 nTime, const UINT16 *pBuffer, int nWidth, int nHeight, int nMinDepth, int nMaxDepth)
{
  Q_D(RGBDWidget);
  Q_UNUSED(nTime);

  if (nWidth != DepthWidth || nHeight != DepthHeight || pBuffer == nullptr)
    return;

  QMutexLocker(&d->mtx);

  d->minDepth = nMinDepth;
  d->maxDepth = nMaxDepth;

    memcpy_s(d->depthData, DepthSize * sizeof(UINT16), pBuffer, DepthSize * sizeof(UINT16));
    HRESULT hr = d->coordinateMapper->MapColorFrameToDepthSpace(DepthSize, pBuffer, ColorSize, d->depthSpaceData);
    if (FAILED(hr))
      qWarning() << "MapColorFrameToDepthSpace() failed.";
}


void RGBDWidget::resizeEvent(QResizeEvent *e)
{
  Q_D(RGBDWidget);
  d->windowAspectRatio = (qreal)e->size().width() / e->size().height();
  if (d->windowAspectRatio < d->imageAspectRatio) {
    const int h = int(width() / d->imageAspectRatio);
    d->destRect = QRect(0, (height()-h)/2, width(), h);
  }
  else {
    const int w = int(height() * d->imageAspectRatio);
    d->destRect = QRect((width()-w)/2, 0, w, height());
  }
}


void RGBDWidget::paintEvent(QPaintEvent*)
{
  Q_D(RGBDWidget);
  QPainter p(this);
  p.fillRect(rect(), Qt::gray);

  QMutexLocker(&d->mtx);

  if (d->videoFrame.isNull() || qFuzzyIsNull(d->imageAspectRatio) || qFuzzyIsNull(d->windowAspectRatio))
    return;

  p.drawImage(d->destRect, d->videoFrame);
  p.setBrush(QColor(255, 0, 0, 128));
  p.setPen(QColor(129, 0, 0, 192));
  for (int i = 0; i < d->refPoints.count(); ++i) {
    const QPointF &ref = d->refPoints.at(i);
    p.drawEllipse(d->destRect.topLeft() + QPointF(d->destRect.width() * ref.x() / ColorWidth, d->destRect.height() * ref.y() / ColorHeight), 3.5, 3.5);
  }
}


void RGBDWidget::mousePressEvent(QMouseEvent *e)
{
  Q_D(RGBDWidget);
  if (e->button() == Qt::LeftButton) {
    const QPoint &mPos = e->pos() - d->destRect.topLeft();
    const QPoint &p = QPoint(ColorWidth * mPos.x() / d->destRect.width(), ColorHeight *  mPos.y() / d->destRect.height());
    d->refPoints[d->refPointIndex] = p;
    const DepthSpacePoint &dsp = d->depthSpaceData[p.x() + p.y() * ColorWidth];
    const int dx = qRound(dsp.X);
    const int dy = qRound(dsp.Y);
    d->ref3D[d->refPointIndex] = QVector3D(float(p.x()), float(p.y()), float(d->depthData[dx + dy * DepthWidth]));
    if (++d->refPointIndex >= d->refPoints.count()) {
      emit refPointsSet(d->ref3D);
      d->refPointIndex = 0;
    }
    update();
  }
}
