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
#include "videowidget.h"

#include <QDebug>
#include <QImage>
#include <QPainter>

class VideoWidgetPrivate
{
public:
  VideoWidgetPrivate(void)
    : videoFrame(ColorWidth, ColorHeight, QImage::Format_ARGB32)
    , windowAspectRatio(1.0)
    , imageAspectRatio(qreal(ColorWidth) / qreal(ColorHeight))
  {
    // ...
  }
  ~VideoWidgetPrivate()
  {
    // ...
  }

  QImage videoFrame;
  qreal windowAspectRatio;
  qreal imageAspectRatio;
};


VideoWidget::VideoWidget(QWidget *parent)
  : QWidget(parent)
  , d_ptr(new VideoWidgetPrivate)
{
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(ColorWidth / 2, ColorHeight / 2);
  setMinimumSize(ColorWidth / 8, ColorHeight / 8);
}


VideoWidget::~VideoWidget()
{
  // ...
}


void VideoWidget::setVideoData(INT64 nTime, const uchar *pBuffer, int nWidth, int nHeight)
{
  Q_D(VideoWidget);
  Q_UNUSED(nTime);

  if (nWidth != ColorWidth || nHeight != ColorHeight || pBuffer == nullptr)
    return;

  quint32 *dst = reinterpret_cast<quint32*>(d->videoFrame.bits());
  const quint32 *src = reinterpret_cast<const quint32*>(pBuffer);
  const quint32 *srcEnd = src + (nWidth * nHeight);
  while (src < srcEnd)
    *dst++ = *src++;

  update();
}


void VideoWidget::resizeEvent(QResizeEvent* e)
{
  Q_D(VideoWidget);
  d->windowAspectRatio = (qreal)e->size().width() / e->size().height();
}


void VideoWidget::paintEvent(QPaintEvent*)
{
  Q_D(VideoWidget);
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
