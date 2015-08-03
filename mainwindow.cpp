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
#include <QBoxLayout>

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
    , colorBuffer(new RGBQUAD[ColorSize])
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
  d->rgbdWidget = new RGBDWidget;
  d->videoWidget = new VideoWidget;
  d->threeDWidget = new ThreeDWidget;


  QBoxLayout *hbox = new QBoxLayout(QBoxLayout::LeftToRight);
  hbox->addWidget(d->videoWidget);
  hbox->addWidget(d->depthWidget);
  hbox->addWidget(d->rgbdWidget);

  QBoxLayout *vbox = new QBoxLayout(QBoxLayout::TopToBottom);
  vbox->addLayout(hbox);
  vbox->addWidget(d->threeDWidget);

  ui->gridLayout->addLayout(vbox, 0, 0);

  QObject::connect(d->threeDWidget, SIGNAL(ready()), SLOT(initAfterGL()));

  QObject::connect(ui->gammaDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(gammaChanged(double)));
  QObject::connect(ui->contrastDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(contrastChanged(double)));
  QObject::connect(ui->saturationDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(saturationChanged(double)));
  QObject::connect(ui->actionMapFromColorToDepth, SIGNAL(toggled(bool)), d->rgbdWidget, SLOT(setMapFromColorToDepth(bool)));
  QObject::connect(ui->actionExit, SIGNAL(triggered(bool)),SLOT(close()));
  QObject::connect(ui->farVerticalSlider, SIGNAL(valueChanged(int)), SLOT(setFarThreshold(int)));
  QObject::connect(ui->nearVerticalSlider, SIGNAL(valueChanged(int)), SLOT(setNearThreshold(int)));
  QObject::connect(ui->haloRadiusVerticalSlider, SIGNAL(valueChanged(int)), d->threeDWidget, SLOT(setHaloRadius(int)));

//  showMaximized();
}


MainWindow::~MainWindow()
{
  delete ui;
}


void MainWindow::initAfterGL(void)
{
  // Q_D(MainWindow);
  qDebug() << "MainWindow::initAfterGL()";
  ui->actionMapFromColorToDepth->setChecked(true);
  ui->actionMatchColorAndDepthSpace->setChecked(true);
  ui->haloRadiusVerticalSlider->setValue(10);
  ui->nearVerticalSlider->setValue(1564);
  ui->farVerticalSlider->setValue(1954);
  ui->saturationDoubleSpinBox->setValue(1.3);
  ui->gammaDoubleSpinBox->setValue(1.4);
  ui->contrastDoubleSpinBox->setValue(1.1);
  startTimer(1000 / 25, Qt::PreciseTimer);
}


void MainWindow::timerEvent(QTimerEvent*)
{
  Q_D(MainWindow);
  bool depthReady = false;
  bool rgbReady = false;
  INT64 nTime = 0;
  UINT16 *pDepthBuffer = nullptr;
  IDepthFrame* pDepthFrame = nullptr;
  USHORT nDepthMinReliableDistance = 0;
  USHORT nDepthMaxDistance = 0;

  if (d->depthFrameReader) {
    HRESULT hr = d->depthFrameReader->AcquireLatestFrame(&pDepthFrame);
    if (SUCCEEDED(hr)) {
      IFrameDescription *pDepthFrameDescription = nullptr;
      int nWidth = 0;
      int nHeight = 0;
      UINT nBufferSize = 0;
      hr = pDepthFrame->get_RelativeTime(&nTime);
      if (SUCCEEDED(hr))
        hr = pDepthFrame->get_FrameDescription(&pDepthFrameDescription);
      if (SUCCEEDED(hr))
        hr = pDepthFrameDescription->get_Width(&nWidth);
      if (SUCCEEDED(hr))
        hr = pDepthFrameDescription->get_Height(&nHeight);
      if (SUCCEEDED(hr))
        hr = pDepthFrame->get_DepthMinReliableDistance(&nDepthMinReliableDistance);
      if (SUCCEEDED(hr)) {
        nDepthMaxDistance = USHRT_MAX;
        hr = pDepthFrame->get_DepthMaxReliableDistance(&nDepthMaxDistance);
      }
      if (SUCCEEDED(hr))
        hr = pDepthFrame->AccessUnderlyingBuffer(&nBufferSize, &pDepthBuffer);
      if (SUCCEEDED(hr)) {
        d->depthWidget->setDepthData(nTime, pDepthBuffer, nWidth, nHeight, nDepthMinReliableDistance, nDepthMaxDistance);
        d->rgbdWidget->setDepthData(nTime, pDepthBuffer, nWidth, nHeight, nDepthMinReliableDistance, nDepthMaxDistance);
        depthReady = true;
      }
      SafeRelease(pDepthFrameDescription);
    }
  }

  IColorFrame* pColorFrame = nullptr;
  if (d->colorFrameReader) {
    HRESULT hr = d->colorFrameReader->AcquireLatestFrame(&pColorFrame);
    if (SUCCEEDED(hr)) {
      IFrameDescription *pRGBFrameDescription = nullptr;
      int nWidth = 0;
      int nHeight = 0;
      ColorImageFormat imageFormat = ColorImageFormat_None;
      UINT nBufferSize = 0;
      hr = pColorFrame->get_RelativeTime(&nTime);
      if (SUCCEEDED(hr))
        hr = pColorFrame->get_FrameDescription(&pRGBFrameDescription);
      if (SUCCEEDED(hr))
        hr = pRGBFrameDescription->get_Width(&nWidth);
      if (SUCCEEDED(hr))
        hr = pRGBFrameDescription->get_Height(&nHeight);
      if (SUCCEEDED(hr))
        hr = pColorFrame->get_RawColorImageFormat(&imageFormat);
      if (SUCCEEDED(hr)) {
        if (imageFormat == ColorImageFormat_Bgra) {
          hr = pColorFrame->AccessRawUnderlyingBuffer(&nBufferSize, reinterpret_cast<BYTE**>(&d->colorBuffer));
        }
        else if (d->colorBuffer != nullptr) {
          nBufferSize = ColorSize * sizeof(RGBQUAD);
          hr = pColorFrame->CopyConvertedFrameDataToArray(nBufferSize, reinterpret_cast<BYTE*>(d->colorBuffer), ColorImageFormat_Bgra);
        }
        else {
          hr = E_FAIL;
        }
      }
      if (SUCCEEDED(hr)) {
        d->videoWidget->setVideoData(nTime, reinterpret_cast<const uchar*>(d->colorBuffer), nWidth, nHeight);
        d->rgbdWidget->setColorData(nTime, reinterpret_cast<const uchar*>(d->colorBuffer), nWidth, nHeight);
        rgbReady = true;
      }
      SafeRelease(pRGBFrameDescription);
    }
  }

  if (rgbReady && depthReady) {
    d->threeDWidget->process(nTime, reinterpret_cast<const uchar*>(d->colorBuffer), pDepthBuffer, nDepthMinReliableDistance, nDepthMaxDistance);
  }

  SafeRelease(pDepthFrame);
  SafeRelease(pColorFrame);
}


bool MainWindow::initKinect(void)
{
  Q_D(MainWindow);

  qDebug() << "MainWindow::initKinect()";

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
  d->threeDWidget->setContrast(GLfloat(contrast));
}


void MainWindow::gammaChanged(double gamma)
{
  Q_D(MainWindow);
  d->threeDWidget->setGamma(GLfloat(gamma));
}


void MainWindow::saturationChanged(double saturation)
{
  Q_D(MainWindow);
  d->threeDWidget->setSaturation(GLfloat(saturation));
}


void MainWindow::setNearThreshold(int value)
{
  Q_D(MainWindow);
  if (value < ui->farVerticalSlider->value()) {
    d->rgbdWidget->setNearThreshold(value);
    d->threeDWidget->setNearThreshold(GLfloat(value));
  }
  else {
    ui->farVerticalSlider->setValue(value);
  }
}


void MainWindow::setFarThreshold(int value)
{
  Q_D(MainWindow);
  if (value > ui->nearVerticalSlider->value()) {
    d->rgbdWidget->setFarThreshold(value);
    d->threeDWidget->setFarThreshold(GLfloat(value));
  }
  else {
    ui->nearVerticalSlider->setValue(value);
  }
}
