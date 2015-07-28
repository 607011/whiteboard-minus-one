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

#include <Kinect.h>

#include <QDebug>
#include <QElapsedTimer>

#include "globals.h"
#include "util.h"
#include "depthwidget.h"
#include "videowidget.h"
#include "rgbdwidget.h"
#include "threedwidget.h"
#include "mainwindow.h"

#include "ui_mainwindow.h"



class MainWindowPrivate {
public:
  MainWindowPrivate(QWidget *parent = nullptr)
    : kinectSensor(nullptr)
    , depthFrameReader(nullptr)
    , colorFrameReader(nullptr)
    , irFrameReader(nullptr)
    , depthWidget(nullptr)
    , videoWidget(nullptr)
    , rgbdWidget(nullptr)
    , threeDWidget(nullptr)
    , colorBuffer(new RGBQUAD[ColorWidth * ColorHeight])
  {
    Q_UNUSED(parent);
    // ...
  }
  ~MainWindowPrivate()
  {
    if (kinectSensor)
      kinectSensor->Close();
    SafeRelease(kinectSensor);
    SafeDelete(colorBuffer);
  }

  IKinectSensor *kinectSensor;
  IDepthFrameReader *depthFrameReader;
  IColorFrameReader *colorFrameReader;
  IInfraredFrameReader *irFrameReader;
  QElapsedTimer timer;

  DepthWidget *depthWidget;
  VideoWidget *videoWidget;
  RGBDWidget *rgbdWidget;
  ThreeDWidget *threeDWidget;

  RGBQUAD *colorBuffer;
};


MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , d_ptr(new MainWindowPrivate(this))
{
  Q_D(MainWindow);

  ui->setupUi(this);

  initKinect();

  d->depthWidget = new DepthWidget;
  ui->gridLayout->addWidget(d->depthWidget, 0, 0);

  d->rgbdWidget = new RGBDWidget;
  ui->gridLayout->addWidget(d->rgbdWidget, 1, 0);

  d->videoWidget = new VideoWidget;
  ui->gridLayout->addWidget(d->videoWidget, 0, 1);

  d->threeDWidget = new ThreeDWidget;
  ui->gridLayout->addWidget(d->threeDWidget, 1, 1);

  QObject::connect(ui->gammaDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(gammaChanged(double)));
  QObject::connect(ui->contrastDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(contrastChanged(double)));
  QObject::connect(ui->saturationDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(saturationChanged(double)));

  d->timer.start();
  startTimer(1000 / 25, Qt::PreciseTimer);
}


MainWindow::~MainWindow()
{
  delete ui;
}


void MainWindow::timerEvent(QTimerEvent*)
{
  Q_D(MainWindow);

  if (d->depthFrameReader) {
    IDepthFrame* pDepthFrame = nullptr;
    HRESULT hr = d->depthFrameReader->AcquireLatestFrame(&pDepthFrame);
    if (SUCCEEDED(hr)) {
      INT64 nTime = 0;
      IFrameDescription *pFrameDescription = nullptr;
      int nWidth = 0;
      int nHeight = 0;
      USHORT nDepthMinReliableDistance = 0;
      USHORT nDepthMaxDistance = 0;
      UINT nBufferSize = 0;
      UINT16 *pBuffer = nullptr;
      hr = pDepthFrame->get_RelativeTime(&nTime);
      if (SUCCEEDED(hr))
        hr = pDepthFrame->get_FrameDescription(&pFrameDescription);
      if (SUCCEEDED(hr))
        hr = pFrameDescription->get_Width(&nWidth);
      if (SUCCEEDED(hr))
        hr = pFrameDescription->get_Height(&nHeight);
      if (SUCCEEDED(hr))
        hr = pDepthFrame->get_DepthMinReliableDistance(&nDepthMinReliableDistance);
      if (SUCCEEDED(hr)) {
        nDepthMaxDistance = USHRT_MAX;
        hr = pDepthFrame->get_DepthMaxReliableDistance(&nDepthMaxDistance);
      }
      if (SUCCEEDED(hr))
        hr = pDepthFrame->AccessUnderlyingBuffer(&nBufferSize, &pBuffer);
      if (SUCCEEDED(hr)) {
        d->depthWidget->setDepthData(nTime, pBuffer, nWidth, nHeight, nDepthMinReliableDistance, nDepthMaxDistance);
        d->rgbdWidget->setDepthData(nTime, pBuffer, nWidth, nHeight, nDepthMinReliableDistance, nDepthMaxDistance);
        d->threeDWidget->setDepthData(nTime, pBuffer, nWidth, nHeight, nDepthMinReliableDistance, nDepthMaxDistance);
      }
      SafeRelease(pFrameDescription);
    }
    SafeRelease(pDepthFrame);
  }

  if (d->colorFrameReader) {
    IColorFrame* pColorFrame = nullptr;
    HRESULT hr = d->colorFrameReader->AcquireLatestFrame(&pColorFrame);
    if (SUCCEEDED(hr)) {
      IFrameDescription *pFrameDescription = nullptr;
      int nWidth = 0;
      int nHeight = 0;
      ColorImageFormat imageFormat = ColorImageFormat_None;
      UINT nBufferSize = 0;
      INT64 nTime = 0;
      hr = pColorFrame->get_RelativeTime(&nTime);
      if (SUCCEEDED(hr))
        hr = pColorFrame->get_FrameDescription(&pFrameDescription);
      if (SUCCEEDED(hr))
        hr = pFrameDescription->get_Width(&nWidth);
      if (SUCCEEDED(hr))
        hr = pFrameDescription->get_Height(&nHeight);
      if (SUCCEEDED(hr))
        hr = pColorFrame->get_RawColorImageFormat(&imageFormat);
      if (SUCCEEDED(hr)) {
        if (imageFormat == ColorImageFormat_Bgra) {
          hr = pColorFrame->AccessRawUnderlyingBuffer(&nBufferSize, reinterpret_cast<BYTE**>(&d->colorBuffer));
        }
        else if (d->colorBuffer != nullptr) {
          nBufferSize = ColorWidth * ColorHeight * sizeof(RGBQUAD);
          hr = pColorFrame->CopyConvertedFrameDataToArray(nBufferSize, reinterpret_cast<BYTE*>(d->colorBuffer), ColorImageFormat_Bgra);
        }
        else {
          hr = E_FAIL;
        }
      }
      if (SUCCEEDED(hr)) {
        d->videoWidget->setVideoData(nTime, reinterpret_cast<const BYTE*>(d->colorBuffer), nWidth, nHeight);
        d->rgbdWidget->setColorData(nTime, reinterpret_cast<const BYTE*>(d->colorBuffer), nWidth, nHeight);
        d->threeDWidget->setVideoData(nTime, reinterpret_cast<const BYTE*>(d->colorBuffer), nWidth, nHeight);
      }
      SafeRelease(pFrameDescription);
    }
    SafeRelease(pColorFrame);
  }
}


bool MainWindow::initKinect(void)
{
  Q_D(MainWindow);
  HRESULT hr;

  hr = GetDefaultKinectSensor(&d->kinectSensor);
  if (FAILED(hr))
    return false;

  if (d->kinectSensor) {
    IDepthFrameSource *pDepthFrameSource = nullptr;
    hr = d->kinectSensor->Open();

    if (SUCCEEDED(hr))
      hr = d->kinectSensor->get_DepthFrameSource(&pDepthFrameSource);
    if (SUCCEEDED(hr))
      hr = pDepthFrameSource->OpenReader(&d->depthFrameReader);
    SafeRelease(pDepthFrameSource);

    IColorFrameSource *pColorFrameSource = nullptr;
    if (SUCCEEDED(hr))
      hr = d->kinectSensor->get_ColorFrameSource(&pColorFrameSource);
    if (SUCCEEDED(hr))
      hr = pColorFrameSource->OpenReader(&d->colorFrameReader);
    SafeRelease(pColorFrameSource);
  }

  if (!d->kinectSensor || FAILED(hr)) {
    qWarning() << "No ready Kinect found!";
    return false;
  }

  return true;
}


void MainWindow::contrastChanged(double contrast)
{
  Q_D(MainWindow);
  d->threeDWidget->setContrast((GLfloat)contrast);
}


void MainWindow::gammaChanged(double gamma)
{
  Q_D(MainWindow);
  d->threeDWidget->setGamma((GLfloat)gamma);
}


void MainWindow::saturationChanged(double saturation)
{
  Q_D(MainWindow);
  d->threeDWidget->setSaturation((GLfloat)saturation);
}
