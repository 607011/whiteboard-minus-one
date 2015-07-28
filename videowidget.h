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

#ifndef __VIDEOWIDGET_H_
#define __VIDEOWIDGET_H_

#include <Kinect.h>

#include <QWidget>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScopedPointer>


class VideoWidgetPrivate;

class VideoWidget : public QWidget
{
  Q_OBJECT
public:
  explicit VideoWidget(QWidget *parent = nullptr);
  ~VideoWidget();
  void setVideoData(INT64 nTime, const uchar *pBuffer, int nWidth, int nHeight);

protected:
  void resizeEvent(QResizeEvent*);
  void paintEvent(QPaintEvent*);

signals:

public slots:

private:
  QScopedPointer<VideoWidgetPrivate> d_ptr;
  Q_DECLARE_PRIVATE(VideoWidget)
  Q_DISABLE_COPY(VideoWidget)
};

#endif // __VIDEOWIDGET_H_
