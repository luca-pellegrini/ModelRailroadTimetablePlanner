#include "stationsmanager.h"
#include "ui_stationsmanager.h"

#include "app/session.h"
#include "viewmanager/viewmanager.h"

#include "app/scopedebug.h"

#include <QToolBar>
#include <QTableView>
#include <QHeaderView>

#include <QMessageBox>

#include <QWindow>

#include "stations/manager/stations/model/stationsmodel.h"

#include "stations/manager/segments/model/railwaysegmentsmodel.h"
#include "stations/manager/segments/model/railwaysegmenthelper.h"

#include "stations/manager/lines/model/linesmodel.h"

#include "utils/combodelegate.h"
#include "stations/station_name_utils.h"

#include "utils/sqldelegate/modelpageswitcher.h"

#include "stations/manager/free_rs_viewer/stationfreersviewer.h" //TODO: move to ViewManager

#include "stations/manager/stations/dialogs/stationeditdialog.h"

#include "stations/manager/segments/dialogs/editrailwaysegmentdlg.h"

#include "stations/manager/lines/dialogs/editlinedlg.h"

#include <QInputDialog>
#include "utils/owningqpointer.h"

StationsManager::StationsManager(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::StationsManager),
    oldCurrentTab(StationsTab),
    clearModelTimers{0, 0},
    m_readOnly(false),
    windowConnected(false)
{
    ui->setupUi(this);
    setup_StationPage();
    setup_SegmentPage();
    setup_LinePage();

    connect(stationsModel, &StationsModel::modelError, this, &StationsManager::onModelError);
    connect(segmentsModel, &RailwaySegmentsModel::modelError, this, &StationsManager::onModelError);
    connect(linesModel, &LinesModel::modelError, this, &StationsManager::onModelError);

    setReadOnly(false);

    ui->tabWidget->setCurrentIndex(StationsTab);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &StationsManager::updateModels);

    setWindowTitle(tr("Stations Manager"));
}

StationsManager::~StationsManager()
{
    delete ui;

    for(int i = 0; i < NTabs; i++)
    {
        if(clearModelTimers[i] > 0)
        {
            killTimer(clearModelTimers[i]);
            clearModelTimers[i] = 0;
        }
    }
}

void StationsManager::setup_StationPage()
{
    QVBoxLayout *vboxLayout = new QVBoxLayout(ui->stationsTab);
    stationToolBar = new QToolBar(ui->stationsTab);
    vboxLayout->addWidget(stationToolBar);

    stationView = new QTableView(ui->stationsTab);
    vboxLayout->addWidget(stationView);

    stationsModel = new StationsModel(Session->m_Db, this);
    stationView->setModel(stationsModel);

    auto ps = new ModelPageSwitcher(false, this);
    vboxLayout->addWidget(ps);
    ps->setModel(stationsModel);
    //Custom colun sorting
    //NOTE: leave disconnect() in the old SIGLAL()/SLOT() version in order to work
    QHeaderView *header = stationView->horizontalHeader();
    disconnect(header, SIGNAL(sectionPressed(int)), stationView, SLOT(selectColumn(int)));
    disconnect(header, SIGNAL(sectionEntered(int)), stationView, SLOT(_q_selectColumn(int)));
    connect(header, &QHeaderView::sectionClicked, this, [this, header](int section)
            {
                stationsModel->setSortingColumn(section);
                header->setSortIndicator(stationsModel->getSortingColumn(), Qt::AscendingOrder);
            });
    header->setSortIndicatorShown(true);
    header->setSortIndicator(stationsModel->getSortingColumn(), Qt::AscendingOrder);

    //Station Type Delegate
    QStringList types;
    types.reserve(int(utils::StationType::NTypes));
    for(int i = 0; i < int(utils::StationType::NTypes); i++)
        types.append(utils::StationUtils::name(utils::StationType(i)));
    stationView->setItemDelegateForColumn(StationsModel::TypeCol, new ComboDelegate(types, Qt::EditRole, this));

    act_addSt = stationToolBar->addAction(tr("Add"), this, &StationsManager::onNewStation);
    act_remSt = stationToolBar->addAction(tr("Remove"), this, &StationsManager::onRemoveStation);
    act_stJobs = stationToolBar->addAction(tr("Jobs"), this, &StationsManager::showStJobViewer);
    act_stSVG = stationToolBar->addAction(tr("SVG Plan"), this, &StationsManager::showStSVGPlan);
    act_freeRs = stationToolBar->addAction(tr("Free RS"), this, &StationsManager::onShowFreeRS);
    act_editSt = stationToolBar->addAction(tr("Edit"), this, &StationsManager::onEditStation);
}

void StationsManager::setup_SegmentPage()
{
    QVBoxLayout *vboxLayout = new QVBoxLayout(ui->segmentsTab);
    segmentsToolBar = new QToolBar;
    vboxLayout->addWidget(segmentsToolBar);

    segmentsView = new QTableView;
    vboxLayout->addWidget(segmentsView);

    segmentsModel = new RailwaySegmentsModel(Session->m_Db, this);
    segmentsView->setModel(segmentsModel);

    auto ps = new ModelPageSwitcher(false, this);
    vboxLayout->addWidget(ps);
    ps->setModel(segmentsModel);
    //Custom colun sorting
    //NOTE: leave disconnect() in the old SIGLAL()/SLOT() version in order to work
    QHeaderView *header = segmentsView->horizontalHeader();
    disconnect(header, SIGNAL(sectionPressed(int)), segmentsView, SLOT(selectColumn(int)));
    disconnect(header, SIGNAL(sectionEntered(int)), segmentsView, SLOT(_q_selectColumn(int)));
    connect(header, &QHeaderView::sectionClicked, this, [this, header](int section)
            {
                segmentsModel->setSortingColumn(section);
                header->setSortIndicator(segmentsModel->getSortingColumn(), Qt::AscendingOrder);
            });
    header->setSortIndicatorShown(true);
    header->setSortIndicator(segmentsModel->getSortingColumn(), Qt::AscendingOrder);

    segmentsToolBar->addAction(tr("Add"), this, &StationsManager::onNewSegment);
    segmentsToolBar->addAction(tr("Remove"), this, &StationsManager::onRemoveSegment);
    segmentsToolBar->addAction(tr("Edit"), this, &StationsManager::onEditSegment);
}

void StationsManager::setup_LinePage()
{
    QVBoxLayout *vboxLayout = new QVBoxLayout(ui->linesTab);
    linesToolBar = new QToolBar(ui->linesTab);
    vboxLayout->addWidget(linesToolBar);

    linesView = new QTableView(ui->linesTab);
    vboxLayout->addWidget(linesView);

    linesModel = new LinesModel(Session->m_Db, this);
    linesView->setModel(linesModel);

    auto ps = new ModelPageSwitcher(false, this);
    vboxLayout->addWidget(ps);
    ps->setModel(linesModel);
    //Custom colun sorting
    //NOTE: leave disconnect() in the old SIGLAL()/SLOT() version in order to work
    QHeaderView *header = linesView->horizontalHeader();
    disconnect(header, SIGNAL(sectionPressed(int)), linesView, SLOT(selectColumn(int)));
    disconnect(header, SIGNAL(sectionEntered(int)), linesView, SLOT(_q_selectColumn(int)));
    connect(header, &QHeaderView::sectionClicked, this, [this, header](int section)
            {
                linesModel->setSortingColumn(section);
                header->setSortIndicator(linesModel->getSortingColumn(), Qt::AscendingOrder);
            });
    header->setSortIndicatorShown(true);
    header->setSortIndicator(linesModel->getSortingColumn(), Qt::AscendingOrder);

    linesToolBar->addAction(tr("Add"), this, &StationsManager::onNewLine);
    linesToolBar->addAction(tr("Remove"), this, &StationsManager::onRemoveLine);
    linesToolBar->addAction(tr("Edit"), this, &StationsManager::onEditLine);
}

void StationsManager::showEvent(QShowEvent *e)
{
    if(!windowConnected)
    {
        QWindow *w = windowHandle();
        if(w)
        {
            windowConnected = true;
            connect(w, &QWindow::visibilityChanged, this, &StationsManager::visibilityChanged);
            updateModels();
        }
    }
    QWidget::showEvent(e);
}

void StationsManager::timerEvent(QTimerEvent *e)
{
    if(e->timerId() == clearModelTimers[StationsTab])
    {
        stationsModel->clearCache();
        killTimer(e->timerId());
        clearModelTimers[StationsTab] = ModelCleared;
        return;
    }
    else if(e->timerId() == clearModelTimers[RailwaySegmentsTab])
    {
        segmentsModel->clearCache();
        killTimer(e->timerId());
        clearModelTimers[RailwaySegmentsTab] = ModelCleared;
        return;
    }
    else if(e->timerId() == clearModelTimers[LinesTab])
    {
        linesModel->clearCache();
        killTimer(e->timerId());
        clearModelTimers[LinesTab] = ModelCleared;
        return;
    }

    QWidget::timerEvent(e);
}

void StationsManager::visibilityChanged(int v)
{
    if(v == QWindow::Minimized || v == QWindow::Hidden)
    {
        //If the window is minimized start timer to clear model cache of current tab
        //The other tabs already have been cleared or are waiting with their timers
        if(clearModelTimers[oldCurrentTab] == ModelLoaded)
        {
            clearModelTimers[oldCurrentTab] = startTimer(ClearModelTimeout, Qt::VeryCoarseTimer);
        }
    }else{
        updateModels();
    }
}

void StationsManager::updateModels()
{
    int curTab = ui->tabWidget->currentIndex();

    if(clearModelTimers[curTab] > 0)
    {
        //This page was already cached, stop it from clearing
        killTimer(clearModelTimers[curTab]);
    }
    else if(clearModelTimers[curTab] == ModelCleared)
    {
        //This page wasn't already cached
        switch (curTab)
        {
        case StationsTab:
        {
            stationsModel->refreshData(true);
            break;
        }
        case RailwaySegmentsTab:
        {
            segmentsModel->refreshData(true);
            break;
        }
        case LinesTab:
        {
            linesModel->refreshData(true);
            break;
        }
        }
    }
    clearModelTimers[curTab] = ModelLoaded;

    //Now start timer to clear old current page if not already done
    if(oldCurrentTab != curTab && clearModelTimers[oldCurrentTab] == ModelLoaded) //Wait 10 seconds and then clear cache
    {
        clearModelTimers[oldCurrentTab] = startTimer(ClearModelTimeout, Qt::VeryCoarseTimer);
    }

    oldCurrentTab = curTab;
}

void StationsManager::onRemoveStation()
{
    DEBUG_ENTRY;

    if(!stationView->selectionModel()->hasSelection())
        return;

    db_id stId = stationsModel->getIdAtRow(stationView->currentIndex().row());
    if(!stId)
        return;

    stationsModel->removeStation(stId);
}

void StationsManager::onNewStation()
{
    DEBUG_ENTRY;

    OwningQPointer<QInputDialog> dlg = new QInputDialog(this);
    dlg->setWindowTitle(tr("Add Station"));
    dlg->setLabelText(tr("Please choose a name for the new station."));
    dlg->setTextValue(QString());

    do{
        int ret = dlg->exec();
        if(ret != QDialog::Accepted || !dlg)
        {
            break; //User canceled
        }

        const QString name = dlg->textValue().simplified();
        if(name.isEmpty())
        {
            QMessageBox::warning(this, tr("Error"), tr("Station name cannot be empty."));
            continue; //Second chance
        }

        if(stationsModel->addStation(name))
        {
            break; //Done!
        }
    }
    while (true);

    //TODO
    //    QModelIndex idx = stationsModel->index(row, 0);
    //    stationView->setCurrentIndex(idx);
    //    stationView->scrollTo(idx);
    //    stationView->edit(idx);
}

void StationsManager::onModelError(const QString& msg)
{
    QMessageBox::warning(this, tr("Station Error"), msg);
}

void StationsManager::onEditStation()
{
    DEBUG_ENTRY;
    if(!stationView->selectionModel()->hasSelection())
        return;

    QModelIndex idx = stationView->currentIndex();
    db_id stId = stationsModel->getIdAtRow(idx.row());
    if(!stId)
        return;

    OwningQPointer<StationEditDialog> dlg(new StationEditDialog(Session->m_Db, this));
    dlg->setStationInternalEditingEnabled(true);
    dlg->setStationExternalEditingEnabled(true);
    dlg->setStation(stId);
    int ret = dlg->exec();
    if(!dlg || ret != QDialog::Accepted)
        return;

    //Refresh stations model
    stationsModel->refreshData(true);

    //FIXME: check if actually changed
    emit Session->stationNameChanged(stId);
    emit Session->stationPlanChanged(stId);

    //Refresh segments
    int &segmentsTimer = clearModelTimers[RailwaySegmentsTab];
    if(segmentsTimer != ModelCleared)
    {
        //If model was loaded clear cache and refresh row count
        segmentsModel->refreshData(true);

        if(segmentsTimer != ModelLoaded)
        {
            //Mark as cleared
            killTimer(segmentsTimer);
            segmentsTimer = ModelCleared;
        }
    }
}

void StationsManager::showStJobViewer()
{
    DEBUG_ENTRY;
    if(!stationView->selectionModel()->hasSelection())
        return;

    QModelIndex idx = stationView->currentIndex();
    db_id stId = stationsModel->getIdAtRow(idx.row());
    if(!stId)
        return;
    Session->getViewManager()->requestStJobViewer(stId);
}

void StationsManager::showStSVGPlan()
{
    DEBUG_ENTRY;
    if(!stationView->selectionModel()->hasSelection())
        return;

    QModelIndex idx = stationView->currentIndex();
    db_id stId = stationsModel->getIdAtRow(idx.row());
    if(!stId)
        return;
    Session->getViewManager()->requestStSVGPlan(stId);
}

void StationsManager::onShowFreeRS()
{
    DEBUG_COLOR_ENTRY(SHELL_BLUE);
    if(!stationView->selectionModel()->hasSelection())
        return;

    QModelIndex idx = stationView->currentIndex();
    db_id stId = stationsModel->getIdAtRow(idx.row());
    if(!stId)
        return;
    Session->getViewManager()->requestStFreeRSViewer(stId);
}

void StationsManager::onRemoveSegment()
{
    if(!segmentsView->selectionModel()->hasSelection())
        return;

    QModelIndex idx = segmentsView->currentIndex();
    db_id segmentId = segmentsModel->getIdAtRow(idx.row());
    if(!segmentId)
        return;

    QString errMsg;
    RailwaySegmentHelper helper(Session->m_Db);
    if(!helper.removeSegment(segmentId, &errMsg))
    {
        onModelError(tr("Cannot remove segment:\n%1").arg(errMsg));
        return;
    }

    //Re-calc row count
    segmentsModel->refreshData();
}

void StationsManager::onNewSegment()
{
    OwningQPointer<EditRailwaySegmentDlg> dlg(new EditRailwaySegmentDlg(Session->m_Db, this));
    dlg->setSegment(0, EditRailwaySegmentDlg::DoNotLock, EditRailwaySegmentDlg::DoNotLock);
    int ret = dlg->exec();

    if(ret != QDialog::Accepted || !dlg)
        return;

    //Re-calc row count
    segmentsModel->refreshData();
}

void StationsManager::onEditSegment()
{
    if(!segmentsView->selectionModel()->hasSelection())
        return;

    QModelIndex idx = segmentsView->currentIndex();
    db_id segmentId = segmentsModel->getIdAtRow(idx.row());
    if(!segmentId)
        return;

    OwningQPointer<EditRailwaySegmentDlg> dlg(new EditRailwaySegmentDlg(Session->m_Db, this));
    dlg->setSegment(segmentId, EditRailwaySegmentDlg::DoNotLock, EditRailwaySegmentDlg::DoNotLock);
    int ret = dlg->exec();

    if(ret != QDialog::Accepted || !dlg)
        return;

    //FIXME: check if actually changed
    emit Session->segmentNameChanged(segmentId);
    emit Session->segmentStationsChanged(segmentId);

    //Refresh fields
    segmentsModel->refreshData(true);
}

void StationsManager::onNewLine()
{
    DEBUG_ENTRY;

    OwningQPointer<QInputDialog> dlg = new QInputDialog(this);
    dlg->setWindowTitle(tr("Add Line"));
    dlg->setLabelText(tr("Please choose a name for the new railway line."));
    dlg->setTextValue(QString());

    do{
        int ret = dlg->exec();
        if(ret != QDialog::Accepted || !dlg)
        {
            break; //User canceled
        }

        const QString name = dlg->textValue().simplified();
        if(name.isEmpty())
        {
            QMessageBox::warning(this, tr("Error"), tr("Line name cannot be empty."));
            continue; //Second chance
        }

        if(linesModel->addLine(name))
        {
            break; //Done!
        }
    }
    while (true);

    //TODO
    //    QModelIndex idx = linesModel->index(row, 0);
    //    linesView->setCurrentIndex(idx);
    //    linesView->scrollTo(idx);
    //    linesView->edit(idx);
}

void StationsManager::onRemoveLine()
{
    DEBUG_ENTRY;
    if(!linesView->selectionModel()->hasSelection())
        return;

    int row = linesView->currentIndex().row();
    db_id lineId = linesModel->getIdAtRow(row);
    if(!lineId)
        return;
    linesModel->removeLine(lineId);
}

void StationsManager::onEditLine()
{
    DEBUG_ENTRY;
    if(!linesView->selectionModel()->hasSelection())
        return;

    int row = linesView->currentIndex().row();
    db_id lineId = linesModel->getIdAtRow(row);
    if(!lineId)
        return;

    OwningQPointer<EditLineDlg> dlg(new EditLineDlg(Session->m_Db, this));
    dlg->setLineId(lineId);
    int ret = dlg->exec();

    if(ret != QDialog::Accepted || !dlg)
        return;

    //FIXME: check if actually changed
    emit Session->lineNameChanged(lineId);
    emit Session->lineSegmentsChanged(lineId);

    //Refresh fields
    linesModel->refreshData(true);
}

void StationsManager::setReadOnly(bool readOnly)
{
    if(m_readOnly == readOnly)
        return;

    m_readOnly = readOnly;

    segmentsToolBar->setDisabled(m_readOnly);
    linesToolBar->setDisabled(m_readOnly);

    act_addSt->setDisabled(m_readOnly);
    act_remSt->setDisabled(m_readOnly);
    act_editSt->setDisabled(m_readOnly);

    if(m_readOnly)
    {
        stationView->setEditTriggers(QTableView::NoEditTriggers);
        segmentsView->setEditTriggers(QTableView::NoEditTriggers);
        linesView->setEditTriggers(QTableView::NoEditTriggers);
    }
    else
    {
        stationView->setEditTriggers(QTableView::DoubleClicked);
        segmentsView->setEditTriggers(QTableView::DoubleClicked);
        linesView->setEditTriggers(QTableView::DoubleClicked);
    }
}
