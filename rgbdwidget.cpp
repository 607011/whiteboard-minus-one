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
    : videoFrame(DepthWidth, DepthHeight, QImage::Format_ARGB32)
    , colorSpaceData(new ColorSpacePoint[DepthWidth * DepthHeight])
    , colorSpaceDataSize(DepthWidth * DepthHeight)
    , depthData(new UINT16[DepthWidth * DepthHeight])
    , depthDataSize(DepthWidth * DepthHeight)
    , minDepth(0)
    , maxDepth(USHRT_MAX)
    , windowAspectRatio(1.0)
    , imageAspectRatio(qreal(DepthWidth) / qreal(DepthHeight))
    , kinectSensor(nullptr)
    , coordinateMapper(nullptr)
  {
    // ...
  }
  ~RGBDWidgetPrivate()
  {
    SafeDeleteArray(depthData);
    SafeRelease(coordinateMapper);
  }

  QImage videoFrame;
  ColorSpacePoint *colorSpaceData;
  int colorSpaceDataSize;
  RGBQUAD *colorData;
  int colorDataSize;
  UINT16 *depthData;
  int depthDataSize;
  int minDepth;
  int maxDepth;

  IKinectSensor *kinectSensor;
  ICoordinateMapper *coordinateMapper;

  qreal windowAspectRatio;
  qreal imageAspectRatio;
};


RGBDWidget::RGBDWidget(QWidget *parent)
  : QWidget(parent)
  , d_ptr(new RGBDWidgetPrivate)
{
  Q_D(RGBDWidget);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(DepthWidth, DepthHeight);
  setMinimumSize(DepthWidth / 2, DepthHeight / 2);

  HRESULT hr = GetDefaultKinectSensor(&d->kinectSensor);
  if (SUCCEEDED(hr) && d->kinectSensor != nullptr)
    hr = d->kinectSensor->get_CoordinateMapper(&d->coordinateMapper);
}


void RGBDWidget::setColorData(INT64 nTime, const uchar *pBuffer, int nWidth, int nHeight)
{
  Q_D(RGBDWidget);
  Q_UNUSED(nTime);

  if (nWidth != ColorWidth || nHeight != ColorHeight || pBuffer == nullptr || d->depthData == nullptr)
    return;

  // d->coordinateMapper->MapColorFrameToDepthSpace(nWidth * nHeight, reinterpret_cast<const UINT16*>(pBuffer), d->depthSpaceDataSize, d->depthSpaceData);

  quint32 *dst = reinterpret_cast<quint32*>(d->videoFrame.bits());
  static const QRgb defaultColor = qRgb(88, 255, 44);
  for (int depthIndex = 0; depthIndex < d->depthDataSize; ++depthIndex) {
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

  update();
}


void RGBDWidget::setDepthData(INT64 nTime, const UINT16 *pBuffer, int nWidth, int nHeight, int nMinDepth, int nMaxDepth)
{
  Q_D(RGBDWidget);
  Q_UNUSED(nTime);

  if (nWidth != DepthWidth || nHeight != DepthHeight || pBuffer == nullptr || d->depthData == nullptr)
    return;

  qDebug() << "RGBDWidget::setDepthData(" << nTime << pBuffer << nWidth << nHeight << nMinDepth << nMaxDepth << ")";

  d->minDepth = nMinDepth;
  d->maxDepth = nMaxDepth;
  HRESULT hr = d->coordinateMapper->MapDepthFrameToColorSpace(d->depthDataSize, pBuffer, d->colorSpaceDataSize, d->colorSpaceData);
  if (FAILED(hr))
    qWarning() << "MapDepthFrameToColorSpace() failed.";
}


void RGBDWidget::resizeEvent(QResizeEvent* e)
{
  Q_D(RGBDWidget);
  d->windowAspectRatio = (qreal)e->size().width() / e->size().height();
}


void RGBDWidget::paintEvent(QPaintEvent*)
{
  Q_D(RGBDWidget);
  QPainter p(this);
  p.fillRect(rect(), Qt::gray);
  QRect destRect;
  if (d->videoFrame.isNull() || qFuzzyIsNull(d->imageAspectRatio) || qFuzzyIsNull(d->windowAspectRatio))
    return;
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
