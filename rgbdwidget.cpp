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

#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QMutexLocker>

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
    , kinectSensor(nullptr)
    , coordinateMapper(nullptr)
    , windowAspectRatio(1.0)
    , imageAspectRatio(1.0)
    , mapFromColorToDepth(true)
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

  IKinectSensor *kinectSensor;
  ICoordinateMapper *coordinateMapper;

  qreal windowAspectRatio;
  qreal imageAspectRatio;

  bool mapFromColorToDepth;

  QMutex mtx;
};


RGBDWidget::RGBDWidget(QWidget *parent)
  : QWidget(parent)
  , d_ptr(new RGBDWidgetPrivate)
{
  Q_D(RGBDWidget);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMinimumSize(DepthWidth / 2, DepthHeight / 2);

  HRESULT hr = GetDefaultKinectSensor(&d->kinectSensor);
  if (SUCCEEDED(hr) && d->kinectSensor != nullptr)
    hr = d->kinectSensor->get_CoordinateMapper(&d->coordinateMapper);
}


void RGBDWidget::setMapFromColorToDepth(bool mapFromColorToDepth)
{
  Q_D(RGBDWidget);
  d->mtx.lock();
  d->mapFromColorToDepth = mapFromColorToDepth;
  d->videoFrame = (d->mapFromColorToDepth)
      ? QImage(ColorWidth, ColorHeight, QImage::Format_ARGB32)
      : QImage(DepthWidth, DepthHeight, QImage::Format_ARGB32);
  d->imageAspectRatio = qreal(d->videoFrame.width()) / qreal(d->videoFrame.height());
  setMaximumSize(d->videoFrame.size());
  d->mtx.unlock();
  update();
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

  if (d->mapFromColorToDepth) {
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
  }
  else {
    for (int depthIndex = 0; depthIndex < DepthSize; ++depthIndex) {
      const ColorSpacePoint &c = d->colorSpaceData[depthIndex];
      const quint32 *src = &defaultColor;
      if (c.X != -std::numeric_limits<float>::infinity() && c.Y != -std::numeric_limits<float>::infinity()) {
        const int cx = qRound(c.X);
        const int cy = qRound(c.Y);
        if (cx >= 0 && cx < ColorWidth && cy >= 0 && cy < ColorHeight) {
          const int colorIndex = cx + cy * ColorWidth;
          src = reinterpret_cast<const quint32*>(pBuffer) + colorIndex;
        }
      }
      dst[depthIndex] = *src;
    }
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

  if (d->mapFromColorToDepth) {
    HRESULT hr = d->coordinateMapper->MapColorFrameToDepthSpace(DepthSize, pBuffer, ColorSize, d->depthSpaceData);
    if (FAILED(hr))
      qWarning() << "MapColorFrameToDepthSpace() failed.";
  }
  else {
    HRESULT hr = d->coordinateMapper->MapDepthFrameToColorSpace(DepthSize, pBuffer, DepthSize, d->colorSpaceData);
    if (FAILED(hr))
      qWarning() << "MapDepthFrameToColorSpace() failed.";
  }
}


void RGBDWidget::resizeEvent(QResizeEvent *e)
{
  Q_D(RGBDWidget);
  d->windowAspectRatio = (qreal)e->size().width() / e->size().height();
}


void RGBDWidget::paintEvent(QPaintEvent*)
{
  Q_D(RGBDWidget);
  QPainter p(this);
  p.fillRect(rect(), Qt::gray);

  QMutexLocker(&d->mtx);

  if (d->videoFrame.isNull() || qFuzzyIsNull(d->imageAspectRatio) || qFuzzyIsNull(d->windowAspectRatio))
    return;

  QRect destRect;
  if (d->windowAspectRatio < d->imageAspectRatio) {
    const int h = int(width() / d->imageAspectRatio);
    destRect = QRect(0, (height()-h)/2, width(), h);
  }
  else {
    const int w = int(height() * d->imageAspectRatio);
    destRect = QRect((width()-w)/2, 0, w, height());
  }
  p.drawImage(destRect, d->videoFrame);
}
