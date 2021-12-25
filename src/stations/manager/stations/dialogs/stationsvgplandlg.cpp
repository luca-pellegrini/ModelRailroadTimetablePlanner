#include "stationsvgplandlg.h"

#include <QIODevice>
#include <QXmlStreamReader>
#include <QSvgRenderer>

#include <QVBoxLayout>
#include <QScrollArea>
#include <QToolBar>

#include <QSpinBox>

#include <QMessageBox>
#include <QPushButton>
#include "utils/owningqpointer.h"

#include <ssplib/svgstationplanlib.h>

#include "stations/manager/stations/model/stationsvghelper.h"
#include "stations/manager/segments/model/railwaysegmenthelper.h"
#include "utils/kmutils.h"

#include "app/session.h"
#include "viewmanager/viewmanager.h"

#include <QDebug>

StationSVGPlanDlg::StationSVGPlanDlg(sqlite3pp::database &db, QWidget *parent) :
    QWidget(parent),
    mDb(db),
    stationId(0)
{
    QVBoxLayout *lay = new QVBoxLayout(this);

    m_plan = new ssplib::StationPlan;
    mSvg = new QSvgRenderer(this);

    view = new ssplib::SSPViewer(m_plan);
    view->setRenderer(mSvg);
    connect(view, &ssplib::SSPViewer::labelClicked, this, &StationSVGPlanDlg::onLabelClicked);

    toolBar = new QToolBar;
    lay->addWidget(toolBar);

    scrollArea = new QScrollArea(this);
    scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setWidget(view);
    lay->addWidget(scrollArea);

    //Actions
    toolBar->addAction(tr("Reload"), this, &StationSVGPlanDlg::reloadPlan);
    toolBar->addSeparator();

    QSpinBox *zoomSpin = new QSpinBox;
    zoomSpin->setRange(25, 400);
    connect(zoomSpin, qOverload<int>(&QSpinBox::valueChanged), this, &StationSVGPlanDlg::setZoom);
    connect(this, &StationSVGPlanDlg::zoomChanged, zoomSpin, &QSpinBox::setValue);

    QAction *zoomAction = toolBar->addWidget(zoomSpin);
    zoomAction->setText(tr("Zoom"));

    toolBar->addAction(tr("Fit To Window"), this, &StationSVGPlanDlg::zoomToFit);

    setMinimumSize(400, 300);
    resize(600, 500);
}

StationSVGPlanDlg::~StationSVGPlanDlg()
{
    view->setPlan(nullptr);
    view->setRenderer(nullptr);

    delete m_plan;
    m_plan = nullptr;
}

void StationSVGPlanDlg::setStation(db_id stId)
{
    stationId = stId;
}

void StationSVGPlanDlg::reloadSVG(QIODevice *dev)
{
    m_plan->clear();

    //TODO: load station data from DB

    ssplib::StreamParser parser(m_plan, dev);
    parser.parse();

    //Sort items
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

    //Reload from database
    QString stationName;
    if(!StationSVGHelper::loadStationFromDB(mDb, stationId, stationName, m_plan))
    {
        QMessageBox::warning(this, tr("Error Loading Station"),
                             tr("Cannot load station from database"));
        return;
    }

    setWindowTitle(tr("%1 Station Plan").arg(stationName));
}

void StationSVGPlanDlg::clearDBData()
{
    //Clear previous data obtained from Database
    for(ssplib::LabelItem& item : m_plan->labels)
    {
        item.visible = false;
        item.itemId = 0;
        item.labelText.clear();
    }

    for(ssplib::TrackItem& item : m_plan->platforms)
    {
        item.visible = false;
        item.itemId = 0;

        item.jobId = 0;
        item.color = ssplib::whiteRGB;
        item.jobName.clear();

        item.trackName.clear();
    }

    for(ssplib::TrackConnectionItem& item : m_plan->trackConnections)
    {
        item.visible = false;
        item.itemId = 0;

        item.jobId = 0;
        item.color = ssplib::whiteRGB;
        item.jobName.clear();

        item.info.gateId = 0;
        item.info.trackId = 0;
    }
}

bool StationSVGPlanDlg::stationHasSVG(sqlite3pp::database &db, db_id stId, QString *stNameOut)
{
    return StationSVGHelper::stationHasSVG(db, stId, stNameOut);
}

void StationSVGPlanDlg::reloadPlan()
{
    std::unique_ptr<QIODevice> dev;
    dev.reset(StationSVGHelper::loadImage(mDb, stationId));

    if(!dev)
    {
        QMessageBox::warning(this, tr("Error Loading SVG"),
                             tr("Cannot find SVG data"));
        return;
    }

    if(!dev->open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Error Loading SVG"),
                             tr("Cannot read data: %1").arg(dev->errorString()));
        return;
    }

    reloadSVG(dev.get());
    reloadDBData();
}

void StationSVGPlanDlg::setZoom(int val)
{
    if(val == m_zoom || val > 400 || val < 25)
        return;

    m_zoom = val;
    emit zoomChanged(m_zoom);

    QSize s = scrollArea->widget()->sizeHint();
    s = s * m_zoom / 100;
    scrollArea->widget()->resize(s);
}

void StationSVGPlanDlg::zoomToFit()
{
    const QSize available = scrollArea->size();
    const QSize contents = scrollArea->widget()->sizeHint();

    const int zoomH = 100 * available.width() / contents.width();
    const int zoomV = 100 * available.height() / contents.height();

    const int val = qMin(zoomH, zoomV);
    setZoom(val);
}

void StationSVGPlanDlg::onLabelClicked(qint64 gateId, QChar letter, const QString &text)
{
    RailwaySegmentHelper helper(mDb);
    utils::RailwaySegmentInfo info;
    if(!helper.getSegmentInfoFromGate(gateId, info))
    {
        QMessageBox::warning(this, tr("Database Error"),
                             tr("Cannot retrive details for gate %1 (%2)").arg(letter).arg(text));
        return;
    }

    if(info.to.stationId == stationId)
    {
        //Reverse segment
        qSwap(info.from, info.to);
    }
    else if(info.from.stationId != stationId)
    {
        //Segment not of this station
        qWarning() << "StationSVGPlanDlg::onLabelClicked segment" << info.segmentId << info.segmentName
                   << "NOT OF THIS STATION" << stationId;
    }

    OwningQPointer<QMessageBox> msgBox = new QMessageBox(this);
    msgBox->setIcon(QMessageBox::Information);
    msgBox->setWindowTitle(tr("Gate %1").arg(letter));

    const QString translatedText =
        tr(
            "<h3>Railway Segment Details</h3>"
            "<p>"
            "Segment: <b>%1</b><br>"
            "From: <b>%2</b> (Gate: %3)<br>"
            "To:   <b>%4</b> (Gate: %5)<br>"
            "Distance: <b>%6 Km</b><br>"
            "Max. Speed: <b>%7 km/h</b>"
            "</p>")
            .arg(info.segmentName)
            .arg(info.from.stationName)
            .arg(info.from.gateLetter)
            .arg(info.to.stationName)
            .arg(info.to.gateLetter)
            .arg(utils::kmNumToText(info.distanceMeters))
            .arg(info.maxSpeedKmH);

    msgBox->setTextFormat(Qt::RichText);
    msgBox->setText(translatedText);

    QPushButton *showSVGBut = msgBox->addButton(tr("Show SVG"), QMessageBox::YesRole);
    msgBox->addButton(QMessageBox::Ok);
    msgBox->setDefaultButton(QMessageBox::Ok);

    msgBox->exec();
    if(!msgBox)
        return;

    if(msgBox->clickedButton() == showSVGBut)
    {
        Session->getViewManager()->requestStSVGPlan(info.to.stationId);
    }
}

void StationSVGPlanDlg::showEvent(QShowEvent *)
{
    //NOTE: when dialog is created it is hidden so it cannot zoom
    //We load the station and then show the dialog
    //Since dialog is hidden at first it cannot calculate zoom
    //So when the dialog is first shown we trigger zoom again.
    zoomToFit();
}
