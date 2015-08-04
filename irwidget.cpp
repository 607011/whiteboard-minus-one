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
#include "irwidget.h"

#include <QPainter>

class IRWidgetPrivate {
public:
  IRWidgetPrivate(void)
    : irFrame(IRWidth, IRHeight, QImage::Format_ARGB32)
    , windowAspectRatio(1.0)
    , imageAspectRatio(qreal(IRWidth) / qreal(IRHeight))
  { /* ... */ }
  ~IRWidgetPrivate(void)
  { /* ... */ }

  QImage irFrame;

  qreal windowAspectRatio;
  qreal imageAspectRatio;
};


// InfraredSourceValueMaximum is the highest value that can be returned in the InfraredFrame.
// It is cast to a float for readability in the visualization code.
static const float InfraredSourceValueMaximum = float(USHRT_MAX);

// The InfraredOutputValueMinimum value is used to set the lower limit, post processing, of the
// infrared data that we will render.
// Increasing or decreasing this value sets a brightness "wall" either closer or further away.
static const float InfraredOutputValueMinimum = .01f;

// The InfraredOutputValueMaximum value is the upper limit, post processing, of the
// infrared data that we will render.
static const float InfraredOutputValueMaximum = 1.f;

// The InfraredSceneValueAverage value specifies the average infrared value of the scene.
// This value was selected by analyzing the average pixel intensity for a given scene.
// Depending on the visualization requirements for a given application, this value can be
// hard coded, as was done here, or calculated by averaging the intensity for each pixel prior
// to rendering.
static const float InfraredSceneValueAverage = .08f;

/// The InfraredSceneStandardDeviations value specifies the number of standard deviations
/// to apply to InfraredSceneValueAverage. This value was selected by analyzing data
/// from a given scene.
/// Depending on the visualization requirements for a given application, this value can be
/// hard coded, as was done here, or calculated at runtime.
#define InfraredSceneStandardDeviations 3.0f


IRWidget::IRWidget(QWidget *parent)
  : QWidget(parent)
  , d_ptr(new IRWidgetPrivate)
{
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(IRWidth, IRHeight);
  setMinimumSize(IRWidth / 2, IRHeight / 2);
}


void IRWidget::setIRData(INT64 nTime, const UINT16 *pBuffer, int nWidth, int nHeight)
{
  Q_D(IRWidget);
  Q_UNUSED(nTime);

  if (nWidth != IRWidth || nHeight != IRHeight || pBuffer == nullptr)
    return;

  /*
   * float intensityRatio = static_cast<float>(*pBuffer) / InfraredSourceValueMaximum;

      // 2. dividing by the (average scene value * standard deviations)
      intensityRatio /= InfraredSceneValueAverage * InfraredSceneStandardDeviations;

      // 3. limiting the value to InfraredOutputValueMaximum
      intensityRatio = min(InfraredOutputValueMaximum, intensityRatio);

      // 4. limiting the lower value InfraredOutputValueMinimym
      intensityRatio = max(InfraredOutputValueMinimum, intensityRatio);

      // 5. converting the normalized value to a byte and using the result
      // as the RGB components required by the image
      byte intensity = static_cast<byte>(intensityRatio * 255.0f);
      pDest->rgbRed = intensity;
      pDest->rgbGreen = intensity;
      pDest->rgbBlue = intensity;
   */

  const UINT16* pBufferEnd = pBuffer + nWidth * nHeight;
  BYTE *dst = d->irFrame.bits();
  while (pBuffer < pBufferEnd) {
    float intensityRatio = float(*pBuffer) / InfraredSourceValueMaximum;
    intensityRatio /= InfraredSceneValueAverage * InfraredSceneStandardDeviations;
    intensityRatio = clamp(intensityRatio, InfraredOutputValueMinimum, InfraredOutputValueMaximum);
    byte intensity = byte(intensityRatio * 255.0f);
    dst[0] = intensity;
    dst[1] = intensity;
    dst[2] = intensity;
    dst[3] = 0xffU;
    dst += 4;
    ++pBuffer;
  }

  update();

}



void IRWidget::resizeEvent(QResizeEvent* e)
{
  Q_D(IRWidget);
  d->windowAspectRatio = (qreal)e->size().width() / e->size().height();
}


void IRWidget::paintEvent(QPaintEvent *)
{
  Q_D(IRWidget);
  QPainter p(this);

  p.fillRect(rect(), Qt::gray);
  QRect destRect;
  if (d->irFrame.isNull() || qFuzzyIsNull(d->imageAspectRatio) || qFuzzyIsNull(d->windowAspectRatio))
    return;
  if (d->windowAspectRatio < d->imageAspectRatio) {
    const int h = int(width() / d->imageAspectRatio);
    destRect = QRect(0, (height()-h)/2, width(), h);
  }
  else {
    const int w = int(height() * d->imageAspectRatio);
    destRect = QRect((width()-w)/2, 0, w, height());
  }

  p.drawImage(destRect, d->irFrame);
}



