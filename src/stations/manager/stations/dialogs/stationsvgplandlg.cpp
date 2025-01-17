/*
 * ModelRailroadTimetablePlanner
 * Copyright 2016-2023, Filippo Gentile
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stationsvgplandlg.h"

#include <QIODevice>
#include <QXmlStreamReader>
#include <QSvgRenderer>

#include <QVBoxLayout>
#include <QScrollArea>
#include <QToolBar>

#include <QSpinBox>
#include <QTimeEdit>

#include <QMessageBox>
#include <QPushButton>
#include "utils/owningqpointer.h"

#include <ssplib/svgstationplanlib.h>

#include "stations/manager/stations/model/stationsvghelper.h"
#include "stations/manager/segments/model/railwaysegmenthelper.h"
#include "utils/delegates/kmspinbox/kmutils.h"

#include "app/session.h"
#include "viewmanager/viewmanager.h"

#include "utils/jobcategorystrings.h"

#include <QDebug>

StationSVGPlanDlg::StationSVGPlanDlg(sqlite3pp::database &db, QWidget *parent) :
    QWidget(parent),
    mDb(db),
    stationId(0),
    m_showJobs(0),
    mJobTimer(0),
    m_zoom(100)
{
    QVBoxLayout *lay     = new QVBoxLayout(this);

    mSvg                 = new QSvgRenderer(this);
    m_plan               = new ssplib::StationPlan;
    m_station            = new StationSVGJobStops;
    m_station->stationId = 0;

    view                 = new ssplib::SSPViewer(m_plan);
    view->setRenderer(mSvg);
    connect(view, &ssplib::SSPViewer::labelClicked, this, &StationSVGPlanDlg::onLabelClicked);
    connect(view, &ssplib::SSPViewer::trackClicked, this, &StationSVGPlanDlg::onTrackClicked);
    connect(view, &ssplib::SSPViewer::trackConnClicked, this,
            &StationSVGPlanDlg::onTrackConnClicked);

    toolBar = new QToolBar;
    lay->addWidget(toolBar);

    scrollArea = new QScrollArea(this);
    scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setWidget(view);
    lay->addWidget(scrollArea);

    // Actions
    toolBar->addAction(tr("Reload"), this, &StationSVGPlanDlg::reloadPlan);
    toolBar->addSeparator();

    QSpinBox *zoomSpin = new QSpinBox;
    zoomSpin->setRange(25, 400);
    connect(zoomSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &StationSVGPlanDlg::setZoom_slot);
    connect(this, &StationSVGPlanDlg::zoomChanged, zoomSpin, &QSpinBox::setValue);

    QAction *zoomAction = toolBar->addWidget(zoomSpin);
    zoomAction->setText(tr("Zoom"));

    toolBar->addAction(tr("Fit To Window"), this, &StationSVGPlanDlg::zoomToFit);

    toolBar->addSeparator();
    act_showJobs = toolBar->addAction(tr("Show Jobs At:"));
    act_showJobs->setToolTip(tr("Show Jobs in this station at requested time.\n"
                                "Click to enable and enter time."));
    act_showJobs->setCheckable(true);
    connect(act_showJobs, &QAction::toggled, this, &StationSVGPlanDlg::showJobs);

    mTimeEdit = new QTimeEdit;
    connect(mTimeEdit, &QTimeEdit::timeChanged, this, &StationSVGPlanDlg::startJobTimer);
    connect(mTimeEdit, &QTimeEdit::editingFinished, this, &StationSVGPlanDlg::applyJobTime);
    act_timeEdit = toolBar->addWidget(mTimeEdit);
    act_timeEdit->setVisible(m_showJobs);

    act_prevTime = toolBar->addAction(tr("Previous"));
    act_prevTime->setToolTip(
      tr("Update time to go to <b>previous</b> Job arrival or departure in this station"));
    connect(act_prevTime, &QAction::triggered, this, &StationSVGPlanDlg::goToPrevStop);
    act_prevTime->setVisible(m_showJobs);

    act_nextTime = toolBar->addAction(tr("Next"));
    act_nextTime->setToolTip(
      tr("Update time to go to <b>next</b> Job arrival or departure in this station"));
    connect(act_nextTime, &QAction::triggered, this, &StationSVGPlanDlg::goToNextStop);
    act_nextTime->setVisible(m_showJobs);

    setMinimumSize(400, 300);
    resize(650, 500);
}

StationSVGPlanDlg::~StationSVGPlanDlg()
{
    stopJobTimer();

    view->setPlan(nullptr);
    view->setRenderer(nullptr);

    delete m_plan;
    m_plan = nullptr;

    delete m_station;
    m_station = nullptr;
}

void StationSVGPlanDlg::setStation(db_id stId)
{
    stationId            = stId;
    m_station->stationId = stationId;
}

void StationSVGPlanDlg::reloadSVG(QIODevice *dev)
{
    m_plan->clear();

    // TODO: load station data from DB

    ssplib::StreamParser parser(m_plan, dev);
    parser.parse();

    // Sort items
    std::sort(m_plan->labels.begin(), m_plan->labels.end());
    std::sort(m_plan->platforms.begin(), m_plan->platforms.end());
    std::sort(m_plan->trackConnections.begin(), m_plan->trackConnections.end());

    dev->reset();

    QXmlStreamReader xml(dev);
    mSvg->load(&xml);

    view->update();
    zoomToFit();
}

void StationSVGPlanDlg::reloadDBData()
{
    clearDBData();

    // Reload from database
    if (!StationSVGHelper::loadStationFromDB(mDb, stationId, m_plan, true))
    {
        QMessageBox::warning(this, tr("Error Loading Station"),
                             tr("Cannot load station from database"));
        return;
    }

    if (m_showJobs)
        reloadJobs();

    setWindowTitle(tr("%1 Station Plan").arg(m_plan->stationName));
}

void StationSVGPlanDlg::clearDBData()
{
    // Clear previous data obtained from Database
    for (ssplib::LabelItem &item : m_plan->labels)
    {
        item.visible = false;
        item.itemId  = 0;
        item.labelText.clear();
    }

    for (ssplib::TrackItem &item : m_plan->platforms)
    {
        item.visible = false;
        item.itemId  = 0;

        item.color   = ssplib::whiteRGB;
        item.tooltip.clear();

        item.trackName.clear();
    }

    for (ssplib::TrackConnectionItem &item : m_plan->trackConnections)
    {
        item.visible = false;
        item.itemId  = 0;

        item.color   = ssplib::whiteRGB;
        item.tooltip.clear();

        item.info.gateId  = 0;
        item.info.trackId = 0;
    }
}

void StationSVGPlanDlg::clearJobs()
{
    clearJobs_internal();
    view->update();
}

void StationSVGPlanDlg::reloadJobs()
{
    clearJobs_internal();

    if (m_showJobs)
    {
        StationSVGHelper::loadStationJobsFromDB(mDb, m_station);
        StationSVGHelper::applyStationJobsToPlan(m_station, m_plan);
    }

    view->update();
}

void StationSVGPlanDlg::showJobs(bool val)
{
    if (m_showJobs == val)
        return;

    m_showJobs = val;
    act_showJobs->setChecked(m_showJobs);
    act_timeEdit->setVisible(m_showJobs);
    act_prevTime->setVisible(m_showJobs);
    act_nextTime->setVisible(m_showJobs);

    if (m_station->time.isNull())
        m_station->time = QTime(0, 0);

    reloadJobs();
}

void StationSVGPlanDlg::setJobTime(const QTime &t)
{
    stopJobTimer();

    // Remove seconds part, round to minutes
    QTime rounded = QTime(t.hour(), t.minute());

    if (m_station->time == rounded)
        return;

    m_station->time = rounded;

    // Avoid starting timer
    mTimeEdit->blockSignals(true);
    mTimeEdit->setTime(rounded);
    mTimeEdit->blockSignals(false);

    reloadJobs();
}

bool StationSVGPlanDlg::stationHasSVG(sqlite3pp::database &db, db_id stId, QString *stNameOut)
{
    return StationSVGHelper::stationHasSVG(db, stId, stNameOut);
}

void StationSVGPlanDlg::reloadPlan()
{
    std::unique_ptr<QIODevice> dev;
    dev.reset(StationSVGHelper::loadImage(mDb, stationId));

    if (!dev)
    {
        QMessageBox::warning(this, tr("Error Loading SVG"), tr("Cannot find SVG data"));
        return;
    }

    if (!dev->open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Error Loading SVG"),
                             tr("Cannot read data: %1").arg(dev->errorString()));
        return;
    }

    reloadSVG(dev.get());
    reloadDBData();
}

void StationSVGPlanDlg::setZoom(int val, bool force)
{
    val = qBound(10, val, 500);

    if (val == m_zoom && !force)
        return;

    m_zoom = val;
    emit zoomChanged(m_zoom);

    QSize s = scrollArea->widget()->sizeHint();
    s       = s * m_zoom / 100;
    scrollArea->widget()->resize(s);
}

void StationSVGPlanDlg::setZoom_slot(int val)
{
    setZoom(val, false);
}

void StationSVGPlanDlg::zoomToFit()
{
    const QSize available = scrollArea->size();
    const QSize contents  = scrollArea->widget()->sizeHint();

    const int zoomH       = 100 * available.width() / contents.width();
    const int zoomV       = 100 * available.height() / contents.height();

    const int val         = qMin(zoomH, zoomV);
    setZoom(val, true);
}

void StationSVGPlanDlg::onLabelClicked(qint64 gateId, QChar letter, const QString &text)
{
    RailwaySegmentHelper helper(mDb);
    utils::RailwaySegmentInfo info;
    if (!helper.getSegmentInfoFromGate(gateId, info))
    {
        QMessageBox::warning(this, tr("Database Error"),
                             tr("Cannot retrive details for gate %1 (%2)").arg(letter).arg(text));
        return;
    }

    if (info.to.stationId == stationId)
    {
        // Reverse segment
        qSwap(info.from, info.to);
    }
    else if (info.from.stationId != stationId)
    {
        // Segment not of this station
        qWarning() << "StationSVGPlanDlg::onLabelClicked segment" << info.segmentId
                   << info.segmentName << "NOT OF THIS STATION" << stationId;
    }

    OwningQPointer<QMessageBox> msgBox = new QMessageBox(this);
    msgBox->setIcon(QMessageBox::Information);
    msgBox->setWindowTitle(tr("Gate %1").arg(letter));

    const QString translatedText =
      tr("<h3>Railway Segment Details</h3>"
         "<table><tr>"
         "<td>Segment:</td><td><b>%1</b></td>"
         "</tr><tr>"
         "<td>From:</td><td><b>%2</b> (Gate: %3)</td>"
         "</tr><tr>"
         "<td>To:</td><td><b>%4</b> (Gate: %5)</td>"
         "</tr><tr>"
         "<td>Distance:</td><td><b>%6 Km</b></td>"
         "</tr><tr>"
         "<td>Max. Speed:</td><td><b>%7 km/h</b></td>"
         "</tr></table>")
        .arg(info.segmentName, info.from.stationName, info.from.gateLetter, info.to.stationName,
             info.to.gateLetter, utils::kmNumToText(info.distanceMeters))
        .arg(info.maxSpeedKmH);

    msgBox->setTextFormat(Qt::RichText);
    msgBox->setText(translatedText);

    QPushButton *showSVGBut = msgBox->addButton(tr("Show SVG"), QMessageBox::YesRole);
    msgBox->addButton(QMessageBox::Ok);
    msgBox->setDefaultButton(QMessageBox::Ok);

    msgBox->exec();
    if (!msgBox)
        return;

    if (msgBox->clickedButton() == showSVGBut)
    {
        Session->getViewManager()->requestStSVGPlan(info.to.stationId);
    }
}

void showTrackMsgBox(const StationSVGJobStops::Stop &stop, ssplib::StationPlan *plan,
                     db_id stationId, QWidget *parent)
{
    const QString jobName = JobCategoryName::jobName(stop.job.jobId, stop.job.category);

    OwningQPointer<QMessageBox> msgBox = new QMessageBox(parent);
    msgBox->setIcon(QMessageBox::Information);
    msgBox->setWindowTitle(
      StationSVGPlanDlg::tr("Job %1", "Message box title on double click").arg(jobName));

    QString platformName;
    for (const ssplib::TrackItem &track : qAsConst(plan->platforms))
    {
        if (track.itemId == stop.in_gate.trackId || track.itemId == stop.out_gate.trackId)
        {
            platformName = track.trackName;
            break;
        }
    }

    const QString translatedText =
      StationSVGPlanDlg::tr("<h3>%1</h3>"
                            "<table><tr>"
                            "<td>Job:</td><td><b>%2</b></td>"
                            "</tr><tr>"
                            "<td>From:</td><td><b>%3</b></td>"
                            "</tr><tr>"
                            "<td>To:</td><td><b>%4</b></td>"
                            "</tr><tr>"
                            "<td>Platform:</td><td><b>%5</b></td>"
                            "</tr></table>")
        .arg(plan->stationName, jobName, stop.arrival.toString("HH:mm"),
             stop.departure.toString("HH:mm"), platformName);

    msgBox->setTextFormat(Qt::RichText);
    msgBox->setText(translatedText);

    QPushButton *showJobEditor =
      msgBox->addButton(StationSVGPlanDlg::tr("Show in Job Editor"), QMessageBox::YesRole);
    QPushButton *showStJobs =
      msgBox->addButton(StationSVGPlanDlg::tr("Show Station Jobs"), QMessageBox::YesRole);
    msgBox->addButton(QMessageBox::Ok);
    msgBox->setDefaultButton(QMessageBox::Ok);

    msgBox->exec();
    if (!msgBox)
        return;

    if (msgBox->clickedButton() == showJobEditor)
    {
        Session->getViewManager()->requestJobEditor(stop.job.jobId, stop.job.stopId);
    }
    else if (msgBox->clickedButton() == showStJobs)
    {
        Session->getViewManager()->requestStJobViewer(stationId);
    }
}

void StationSVGPlanDlg::onTrackClicked(qint64 trackId, const QString & /*name*/)
{
    for (const StationSVGJobStops::Stop &stop : qAsConst(m_station->stops))
    {
        if (stop.in_gate.trackId == trackId || stop.out_gate.trackId == trackId)
        {
            showTrackMsgBox(stop, m_plan, stationId, this);
            break;
        }
    }
}

void StationSVGPlanDlg::onTrackConnClicked(qint64 connId, qint64 /*trackId*/, qint64 /*gateId*/,
                                           int /*gateTrackPos*/, int /*trackSide*/)
{
    for (const StationSVGJobStops::Stop &stop : qAsConst(m_station->stops))
    {
        if (stop.in_gate.connId == connId || stop.out_gate.connId == connId)
        {
            showTrackMsgBox(stop, m_plan, stationId, this);
            break;
        }
    }
}

void StationSVGPlanDlg::startJobTimer()
{
    stopJobTimer();
    mJobTimer = startTimer(700);
}

void StationSVGPlanDlg::stopJobTimer()
{
    if (mJobTimer)
    {
        killTimer(mJobTimer);
        mJobTimer = 0;
    }
}

void StationSVGPlanDlg::applyJobTime()
{
    setJobTime(mTimeEdit->time());
}

void StationSVGPlanDlg::goToPrevStop()
{
    QTime time = m_station->time;
    if (!StationSVGHelper::getPrevNextStop(mDb, stationId, false, time))
    {
        QMessageBox::warning(this, tr("No Stop Found"),
                             tr("No Jobs found to arrive or depart from station <b>%1</b>"
                                " before <b>%2</b>")
                               .arg(m_plan->stationName, m_station->time.toString("HH:mm")));
        return;
    }

    setJobTime(time);
}

void StationSVGPlanDlg::goToNextStop()
{
    QTime time = m_station->time;
    if (!StationSVGHelper::getPrevNextStop(mDb, stationId, true, time))
    {
        QMessageBox::warning(this, tr("No Stop Found"),
                             tr("No Jobs found to arrive or depart from station <b>%1</b>"
                                " after <b>%2</b>")
                               .arg(m_plan->stationName, m_station->time.toString("HH:mm")));
        return;
    }

    setJobTime(time);
}

void StationSVGPlanDlg::showEvent(QShowEvent *)
{
    // NOTE: when dialog is created it is hidden so it cannot zoom
    // We load the station and then show the dialog
    // Since dialog is hidden at first it cannot calculate zoom
    // So when the dialog is first shown we trigger zoom again.
    zoomToFit();
}

void StationSVGPlanDlg::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == mJobTimer)
    {
        applyJobTime();
        return;
    }

    QWidget::timerEvent(e);
}

void StationSVGPlanDlg::clearJobs_internal()
{
    for (ssplib::TrackItem &item : m_plan->platforms)
    {
        item.visible = false;

        item.color   = ssplib::whiteRGB;
        item.tooltip.clear();
    }

    for (ssplib::TrackConnectionItem &item : m_plan->trackConnections)
    {
        item.visible = false;

        item.color   = ssplib::whiteRGB;
        item.tooltip.clear();
    }
}
