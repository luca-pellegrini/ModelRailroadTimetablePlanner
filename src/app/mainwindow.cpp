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

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "app/session.h"

#include "viewmanager/viewmanager.h"

#include "jobs/jobeditor/jobpatheditor.h"
#include <QDockWidget>

#include "utils/owningqpointer.h"
#include <QMessageBox>
#include <QFileDialog>
#include "utils/files/recentdirstore.h"
#include "utils/files/file_format_names.h"

#include <QPushButton>
#include <QLabel>

#include <QCloseEvent>

#include "settings/settingsdialog.h"

#include "graph/model/linegraphmanager.h"

#include "graph/view/linegraphwidget.h"

#include "stations/manager/segments/model/railwaysegmenthelper.h"

#include "db_metadata/meetinginformationdialog.h"

#include "printing/wizard/printwizard.h"

#ifdef ENABLE_USER_QUERY
#    include "sqlconsole/sqlconsole.h"
#endif

#include <QActionGroup>

#include "utils/delegates/sql/customcompletionlineedit.h"
#include "searchbox/searchresultmodel.h"

#ifdef ENABLE_BACKGROUND_MANAGER
#    include "backgroundmanager/backgroundmanager.h"
#    include "backgroundmanager/backgroundresultpanel.h"
#    include "jobs/jobs_checker/crossing/jobcrossingchecker.h"
#    include "rollingstock/rs_checker/rscheckermanager.h"
#endif // ENABLE_BACKGROUND_MANAGER

#include "propertiesdialog.h"
#include "info.h"

#include <QThreadPool>

#include <QTimer> //HACK: TODO remove

#include "app/scopedebug.h"

namespace directory_key {

const QLatin1String session = QLatin1String("session_dir_key");

} // namespace directory_key

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    jobEditor(nullptr),
#ifdef ENABLE_BACKGROUND_MANAGER
    resPanelDock(nullptr),
#endif // ENABLE_BACKGROUND_MANAGER
    view(nullptr),
    jobDock(nullptr),
    searchEdit(nullptr),
    welcomeLabel(nullptr),
    recentFileActs{nullptr},
    m_mode(CentralWidgetMode::StartPageMode),
    closeTimerId(0)
{
    ui->setupUi(this);
    ui->actionAbout->setText(tr("About %1").arg(qApp->applicationDisplayName()));

    auto viewMgr          = Session->getViewManager();
    viewMgr->m_mainWidget = this;

    auto graphMgr         = viewMgr->getLineGraphMgr();
    connect(graphMgr, &LineGraphManager::jobSelected, this, &MainWindow::onJobSelected);

    // view = graphMgr->getView();
    view = new LineGraphWidget(this);

    // Welcome label
    welcomeLabel = new QLabel(this);
    welcomeLabel->setTextFormat(Qt::RichText);
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setFont(QFont("Arial", 15));
    welcomeLabel->setObjectName("WelcomeLabel");

    // JobPathEditor dock
    jobEditor          = new JobPathEditor(this);
    viewMgr->jobEditor = jobEditor;
    jobDock            = new QDockWidget(tr("Job Editor"), this);
    jobDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    jobDock->setWidget(jobEditor);
    jobDock->installEventFilter(this); // NOTE: see MainWindow::eventFilter() below

    addDockWidget(Qt::RightDockWidgetArea, jobDock);
    ui->menuView->addAction(jobDock->toggleViewAction());
    connect(jobDock->toggleViewAction(), &QAction::triggered, jobEditor, &JobPathEditor::show);

#ifdef ENABLE_BACKGROUND_MANAGER
    // Background Errors dock
    BackgroundResultPanel *resPanel = new BackgroundResultPanel(this);
    resPanelDock                    = new QDockWidget(tr("Errors"), this);
    resPanelDock->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    resPanelDock->setWidget(resPanel);
    resPanelDock->installEventFilter(this); // NOTE: see eventFilter() below

    addDockWidget(Qt::BottomDockWidgetArea, resPanelDock);
    ui->menuView->addAction(resPanelDock->toggleViewAction());
    ui->mainToolBar->addAction(resPanelDock->toggleViewAction());

    // Add checkers FIXME: move to session?
    JobCrossingChecker *jobCrossingChecker = new JobCrossingChecker(Session->m_Db, this);
    Session->getBackgroundManager()->addChecker(jobCrossingChecker);

    RsCheckerManager *rsChecker = new RsCheckerManager(Session->m_Db, this);
    Session->getBackgroundManager()->addChecker(rsChecker);
#endif // ENABLE_BACKGROUND_MANAGER

    // Allow JobPathEditor to use all vertical space when RsErrorWidget dock is at bottom
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);

    // Search Box
    SearchResultModel *searchModel = new SearchResultModel(Session->m_Db, this);
    searchEdit                     = new CustomCompletionLineEdit(searchModel, this);
    searchEdit->setMinimumWidth(300);
    searchEdit->setMinimumHeight(25);
    searchEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    searchEdit->setPlaceholderText(tr("Find"));
    searchEdit->setClearButtonEnabled(true);
    connect(searchEdit, &CustomCompletionLineEdit::completionDone, this,
            &MainWindow::onJobSearchItemSelected);
    connect(searchModel, &SearchResultModel::resultsReady, this,
            &MainWindow::onJobSearchResultsReady);

    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->mainToolBar->addWidget(spacer);
    ui->mainToolBar->addWidget(searchEdit);

    setup_actions();
    setCentralWidgetMode(CentralWidgetMode::StartPageMode);

    QMenu *recentFilesMenu = new QMenu(this);
    for (int i = 0; i < MaxRecentFiles; i++)
    {
        recentFileActs[i] = new QAction(this);
        recentFileActs[i]->setVisible(false);
        connect(recentFileActs[i], &QAction::triggered, this, &MainWindow::onOpenRecent);

        recentFilesMenu->addAction(recentFileActs[i]);
    }

    updateRecentFileActions();

    ui->actionOpen_Recent->setMenu(recentFilesMenu);

    // Listen to changes to display welcomeLabel or view
    connect(Session, &MeetingSession::segmentAdded, this, &MainWindow::checkLineNumber);
    connect(Session, &MeetingSession::segmentRemoved, this, &MainWindow::checkLineNumber);
    connect(Session, &MeetingSession::lineAdded, this, &MainWindow::checkLineNumber);
    connect(Session, &MeetingSession::lineRemoved, this, &MainWindow::checkLineNumber);
}

MainWindow::~MainWindow()
{
    Session->getViewManager()->m_mainWidget = nullptr;
    stopCloseTimer();
    delete ui;
}

void MainWindow::setup_actions()
{
    databaseActionGroup = new QActionGroup(this);

    databaseActionGroup->addAction(ui->actionAddJob);
    databaseActionGroup->addAction(ui->actionRemoveJob);

    databaseActionGroup->addAction(ui->actionStations);
    databaseActionGroup->addAction(ui->actionRollingstockManager);
    databaseActionGroup->addAction(ui->actionJob_Shifts);
    databaseActionGroup->addAction(ui->action_JobsMgr);
    databaseActionGroup->addAction(ui->actionRS_Session_Viewer);
    databaseActionGroup->addAction(ui->actionMeeting_Information);

    databaseActionGroup->addAction(ui->actionQuery);

    databaseActionGroup->addAction(ui->actionClose);
    databaseActionGroup->addAction(ui->actionPrint);

    databaseActionGroup->addAction(ui->actionSave);
    databaseActionGroup->addAction(ui->actionSaveCopy_As);

    databaseActionGroup->addAction(ui->actionExport_PDF);
    databaseActionGroup->addAction(ui->actionExport_Svg);

    databaseActionGroup->addAction(ui->actionPrev_Job_Segment);
    databaseActionGroup->addAction(ui->actionNext_Job_Segment);

    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onOpen);
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::onNew);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::onCloseSession);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSave);
    connect(ui->actionSaveCopy_As, &QAction::triggered, this, &MainWindow::onSaveCopyAs);

    connect(ui->actionPrint, &QAction::triggered, this, &MainWindow::onPrint);
    connect(ui->actionExport_PDF, &QAction::triggered, this, &MainWindow::onPrintPDF);
    connect(ui->actionExport_Svg, &QAction::triggered, this, &MainWindow::onExportSvg);
    connect(ui->actionProperties, &QAction::triggered, this, &MainWindow::onProperties);

    connect(ui->actionStations, &QAction::triggered, this, &MainWindow::onStationManager);
    connect(ui->actionRollingstockManager, &QAction::triggered, this,
            &MainWindow::onRollingStockManager);
    connect(ui->actionJob_Shifts, &QAction::triggered, this, &MainWindow::onShiftManager);
    connect(ui->action_JobsMgr, &QAction::triggered, this, &MainWindow::onJobsManager);
    connect(ui->actionRS_Session_Viewer, &QAction::triggered, this, &MainWindow::onSessionRSViewer);
    connect(ui->actionMeeting_Information, &QAction::triggered, this,
            &MainWindow::onMeetingInformation);

    connect(ui->actionAddJob, &QAction::triggered, this, &MainWindow::onAddJob);
    connect(ui->actionRemoveJob, &QAction::triggered, this, &MainWindow::onRemoveJob);

    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::about);
    connect(ui->actionAbout_Qt, &QAction::triggered, qApp, &QApplication::aboutQt);

#ifdef ENABLE_USER_QUERY
    connect(ui->actionQuery, &QAction::triggered, this, &MainWindow::onExecQuery);
#else
    ui->actionQuery->setVisible(false);
    ui->actionQuery->setEnabled(false);
#endif

    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onOpenSettings);

    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

    ui->actionNext_Job_Segment->setToolTip(
      tr("Hold shift and click to go to <b>last</b> job stop."));
    ui->actionPrev_Job_Segment->setToolTip(
      tr("Hold shift and click to go to <b>first</b> job stop."));
    connect(ui->actionNext_Job_Segment, &QAction::triggered, this,
            []()
            {
                bool shiftPressed =
                  QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
                Session->getViewManager()->requestJobShowPrevNextSegment(false, shiftPressed);
            });
    connect(ui->actionPrev_Job_Segment, &QAction::triggered, this,
            []()
            {
                bool shiftPressed =
                  QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
                Session->getViewManager()->requestJobShowPrevNextSegment(true, shiftPressed);
            });
}

void MainWindow::about()
{
    OwningQPointer<QMessageBox> msgBox = new QMessageBox(this);
    msgBox->setIcon(QMessageBox::Information);
    msgBox->setWindowTitle(tr("About %1").arg(qApp->applicationDisplayName()));

    const QString translatedText =
      tr("<h3>%1</h3>"
         "<p>This program makes it easier to deal with timetables and trains.</p>"
         "<p>Version: <b>%2</b></p>"
         "<p>Built: %3</p>"
         "<p>Website: <a href='%4'>%4</a></p>")
        .arg(qApp->applicationDisplayName(), qApp->applicationVersion(),
             QDate::fromString(AppBuildDate, QLatin1String("MMM dd yyyy")).toString("dd/MM/yyyy"),
             AppProjectWebSite);

    msgBox->setTextFormat(Qt::RichText);
    msgBox->setText(translatedText);
    msgBox->setStandardButtons(QMessageBox::Ok);
    msgBox->exec();
}

void MainWindow::onOpen()
{
    DEBUG_ENTRY;

#ifdef SEARCHBOX_MODE_ASYNC
    emit Session->getBackgroundManager()->abortTrivialTasks();
#endif

#ifdef ENABLE_BACKGROUND_MANAGER
    if (Session->getBackgroundManager()->isRunning())
    {
        int ret = QMessageBox::warning(
          this, tr("Backgroung Task"),
          tr("Background task for checking rollingstock errors is still running.\n"
             "Do you want to cancel it?"),
          QMessageBox::Yes, QMessageBox::No, QMessageBox::Yes);
        if (ret == QMessageBox::Yes)
            Session->getBackgroundManager()->abortAllTasks();
        else
            return;
    }
#endif

    OwningQPointer<QFileDialog> dlg = new QFileDialog(this, tr("Open Session"));
    dlg->setFileMode(QFileDialog::ExistingFile);
    dlg->setAcceptMode(QFileDialog::AcceptOpen);
    dlg->setDirectory(RecentDirStore::getDir(directory_key::session, RecentDirStore::Documents));

    QStringList filters;
    filters << FileFormats::tr(FileFormats::tttFormat);
    filters << FileFormats::tr(FileFormats::sqliteFormat);
    filters << FileFormats::tr(FileFormats::allFiles);
    dlg->setNameFilters(filters);

    if (dlg->exec() != QDialog::Accepted || !dlg)
        return;

    QString fileName = dlg->selectedUrls().value(0).toLocalFile();

    if (fileName.isEmpty())
        return;

    RecentDirStore::setPath(directory_key::session, fileName);

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    if (!QThreadPool::globalInstance()->waitForDone(2000))
    {
        QMessageBox::warning(this, tr("Background Tasks"),
                             tr("Some background tasks are still running.\n"
                                "The file was not opened. Try again."));
        QApplication::restoreOverrideCursor();
        return;
    }

    QApplication::restoreOverrideCursor();

    loadFile(fileName);
}

void MainWindow::loadFile(const QString &fileName)
{
    DEBUG_ENTRY;
    if (fileName.isEmpty())
        return;

    qDebug() << "Loading:" << fileName;

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    DB_Error err = Session->openDB(fileName, false);

    QApplication::restoreOverrideCursor();

    if (err == DB_Error::FormatTooOld)
    {
        int but = QMessageBox::warning(
          this, tr("Version is old"),
          tr("This file was created by an older version of %1.\n"
             "Opening it without conversion might not work and even crash the application.\n"
             "Do you want to open it anyway?")
            .arg(qApp->applicationDisplayName()),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (but == QMessageBox::Yes)
            err = Session->openDB(fileName, true);
    }
    else if (err == DB_Error::FormatTooNew)
    {
        if (err == DB_Error::FormatTooOld)
        {
            int but = QMessageBox::warning(this, tr("Version is too new"),
                                           tr("This file was created by a newer version of %1.\n"
                                              "You should update the application first. Opening "
                                              "this file might not work or even crash.\n"
                                              "Do you want to open it anyway?")
                                             .arg(qApp->applicationDisplayName()),
                                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (but == QMessageBox::Yes)
                err = Session->openDB(fileName, true);
        }
    }

    if (err == DB_Error::DbBusyWhenClosing)
        showCloseWarning();

    if (err != DB_Error::NoError)
        return;

    setCurrentFile(fileName);

    // Fake we are coming from Start Page
    // Otherwise we cannot show the first line
    m_mode = CentralWidgetMode::StartPageMode;
    checkLineNumber();

    if (!Session->checkImportRSTablesEmpty())
    {
        // Probably the application crashed before finishing RS importation
        // Give user choice to resume it or discard

        OwningQPointer<QMessageBox> msgBox =
          new QMessageBox(QMessageBox::Warning, tr("RS Import"),
                          tr("There is some rollingstock import data left in this file. "
                             "Probably the application has crashed!<br>"
                             "Before deleting it would you like to resume importation?<br>"
                             "<i>(Sorry for the crash, would you like to contact me and share "
                             "information about it?)</i>"),
                          QMessageBox::NoButton, this);
        auto resumeBut = msgBox->addButton(tr("Resume importation"), QMessageBox::YesRole);
        msgBox->addButton(tr("Just delete it"), QMessageBox::NoRole);
        msgBox->setDefaultButton(resumeBut);
        msgBox->setTextFormat(Qt::RichText);

        msgBox->exec();
        if (!msgBox)
            return;

        if (msgBox->clickedButton() == resumeBut)
        {
            Session->getViewManager()->resumeRSImportation();
        }
        else
        {
            Session->clearImportRSTables();
        }
    }
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    DEBUG_ENTRY;

    if (fileName.isEmpty())
    {
        setWindowFilePath(QString()); // Reset title bar
        return;
    }

    // Qt automatically takes care of showing stripped filename in window title
    setWindowFilePath(fileName);

    QStringList files = AppSettings.getRecentFiles();
    files.removeAll(fileName);
    files.prepend(fileName);
    while (files.size() > MaxRecentFiles)
        files.removeLast();

    AppSettings.setRecentFiles(files);

    updateRecentFileActions();
}

QString MainWindow::strippedName(const QString &fullFileName, bool *ok)
{
    QFileInfo fi(fullFileName);
    if (ok)
        *ok = fi.exists();
    return fi.fileName();
}

void MainWindow::updateRecentFileActions()
{
    DEBUG_ENTRY;
    QStringList files  = AppSettings.getRecentFiles();

    int numRecentFiles = qMin(files.size(), int(MaxRecentFiles));

    for (int i = 0; i < numRecentFiles; i++)
    {
        bool ok      = true;
        QString name = strippedName(files[i], &ok);
        if (name.isEmpty() || !ok)
        {
            files.removeAt(i);
            i--;
            numRecentFiles = qMin(files.size(), int(MaxRecentFiles));
        }
        else
        {
            QString text = tr("&%1 %2").arg(i + 1).arg(name);
            recentFileActs[i]->setText(text);
            recentFileActs[i]->setData(files[i]);
            recentFileActs[i]->setToolTip(files[i]);
            recentFileActs[i]->setVisible(true);
        }
    }
    for (int j = numRecentFiles; j < MaxRecentFiles; ++j)
        recentFileActs[j]->setVisible(false);

    AppSettings.setRecentFiles(files);
}

void MainWindow::onOpenRecent()
{
    DEBUG_ENTRY;
    QAction *act = qobject_cast<QAction *>(sender());
    if (!act)
        return;

    loadFile(act->data().toString());
}

void MainWindow::onNew()
{
    DEBUG_ENTRY;

#ifdef SEARCHBOX_MODE_ASYNC
    emit Session->getBackgroundManager()->abortTrivialTasks();
#endif

#ifdef ENABLE_BACKGROUND_MANAGER
    if (Session->getBackgroundManager()->isRunning())
    {
        int ret = QMessageBox::warning(
          this, tr("Backgroung Task"),
          tr("Background task for checking rollingstock errors is still running.\n"
             "Do you want to cancel it?"),
          QMessageBox::Yes, QMessageBox::No, QMessageBox::Yes);
        if (ret == QMessageBox::Yes)
            Session->getBackgroundManager()->abortAllTasks();
        else
            return;
    }
#endif // ENABLE_BACKGROUND_MANAGER

    OwningQPointer<QFileDialog> dlg = new QFileDialog(this, tr("Create new Session"));
    dlg->setFileMode(QFileDialog::AnyFile);
    dlg->setAcceptMode(QFileDialog::AcceptSave);
    dlg->setDirectory(RecentDirStore::getDir(directory_key::session, RecentDirStore::Documents));

    QStringList filters;
    filters << FileFormats::tr(FileFormats::tttFormat);
    filters << FileFormats::tr(FileFormats::sqliteFormat);
    filters << FileFormats::tr(FileFormats::allFiles);
    dlg->setNameFilters(filters);

    if (dlg->exec() != QDialog::Accepted || !dlg)
        return;

    QString fileName = dlg->selectedUrls().value(0).toLocalFile();

    if (fileName.isEmpty())
        return;

    RecentDirStore::setPath(directory_key::session, fileName);

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    if (!QThreadPool::globalInstance()->waitForDone(2000))
    {
        QMessageBox::warning(this, tr("Background Tasks"),
                             tr("Some background tasks are still running.\n"
                                "The new file was not created. Try again."));
        QApplication::restoreOverrideCursor();
        return;
    }

    QFile f(fileName);
    if (f.exists())
        f.remove();

    DB_Error err = Session->createNewDB(fileName);

    QApplication::restoreOverrideCursor();

    if (err == DB_Error::DbBusyWhenClosing)
        showCloseWarning();

    if (err != DB_Error::NoError)
        return;

    setCurrentFile(fileName);
    checkLineNumber();
}

void MainWindow::onSave()
{
    if (!Session->getViewManager()->closeEditors())
        return;

    Session->releaseAllSavepoints();
}

void MainWindow::onSaveCopyAs()
{
    DEBUG_ENTRY;

    if (!Session->getViewManager()->closeEditors())
        return;

    OwningQPointer<QFileDialog> dlg = new QFileDialog(this, tr("Save Session Copy"));
    dlg->setFileMode(QFileDialog::AnyFile);
    dlg->setAcceptMode(QFileDialog::AcceptSave);
    dlg->setDirectory(RecentDirStore::getDir(directory_key::session, RecentDirStore::Documents));

    QStringList filters;
    filters << FileFormats::tr(FileFormats::tttFormat);
    filters << FileFormats::tr(FileFormats::sqliteFormat);
    filters << FileFormats::tr(FileFormats::allFiles);
    dlg->setNameFilters(filters);

    if (dlg->exec() != QDialog::Accepted || !dlg)
        return;

    QString fileName = dlg->selectedUrls().value(0).toLocalFile();

    if (fileName.isEmpty())
        return;

    RecentDirStore::setPath(directory_key::session, fileName);

    QFile f(fileName);
    if (f.exists())
        f.remove();

    database backupDB(fileName.toUtf8(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);

    int rc = Session->m_Db.backup(backupDB,
                                  [](int pageCount, int remaining, int res)
                                  {
                                      Q_UNUSED(res)
                                      qDebug() << pageCount << "/" << remaining;
                                  });

    if (rc != SQLITE_OK && rc != SQLITE_DONE)
    {
        QString errMsg = Session->m_Db.error_msg();
        qDebug() << Session->m_Db.error_code() << errMsg;
        QMessageBox::warning(this, tr("Error saving copy"), errMsg);
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (closeSession())
        e->accept();
    else
        e->ignore();
}

void MainWindow::showCloseWarning()
{
    QMessageBox::warning(this, tr("Error while Closing"),
                         tr("There was an error while closing the database.\n"
                            "Make sure there aren't any background tasks running and try again."));
}

void MainWindow::stopCloseTimer()
{
    if (closeTimerId)
    {
        killTimer(closeTimerId);
        closeTimerId = 0;
    }
}

void MainWindow::setCentralWidgetMode(MainWindow::CentralWidgetMode mode)
{
    switch (mode)
    {
    case CentralWidgetMode::StartPageMode:
    {
        jobDock->hide();

#ifdef ENABLE_BACKGROUND_MANAGER
        resPanelDock->hide();
#endif // ENABLE_BACKGROUND_MANAGER

        welcomeLabel->setText(tr("<p>Open a file: <b>File</b> > <b>Open</b></p>"
                                 "<p>Create new project: <b>File</b> > <b>New</b></p>"));
        statusBar()->showMessage(tr("Open file or create a new one"));

        break;
    }
    case CentralWidgetMode::NoLinesWarningMode:
    {
        jobDock->show();

#ifdef ENABLE_BACKGROUND_MANAGER
        resPanelDock->hide();
#endif // ENABLE_BACKGROUND_MANAGER

        welcomeLabel->setText(
          tr("<p><b>There are no lines in this session</b></p>"
             "<p>"
             "<table align=\"center\">"
             "<tr>"
             "<td>Start by creating the railway layout for this session:</td>"
             "</tr>"
             "<tr>"
             "<td>"
             "<table>"
             "<tr>"
             "<td>1.</td>"
             "<td>Create stations (<b>Edit</b> > <b>Stations</b>)</td>"
             "</tr>"
             "<tr>"
             "<td>2.</td>"
             "<td>Create railway lines (<b>Edit</b> > <b>Stations</b> > <b>Lines Tab</b>)</td>"
             "</tr>"
             "<tr>"
             "<td>3.</td>"
             "<td>Add stations to railway lines</td>"
             "</tr>"
             "<tr>"
             "<td></td>"
             "<td>(<b>Edit</b> > <b>Stations</b> > <b>Lines Tab</b> > <b>Edit Line</b>)</td>"
             "</tr>"
             "</table>"
             "</td>"
             "</tr>"
             "</table>"
             "</p>"));
        break;
    }
    case CentralWidgetMode::ViewSessionMode:
    {
        jobDock->show();

#ifdef ENABLE_BACKGROUND_MANAGER
        resPanelDock->show();
#endif // ENABLE_BACKGROUND_MANAGER

        welcomeLabel->setText(QString());
        break;
    }
    }

    enableDBActions(mode != CentralWidgetMode::StartPageMode);

    if (mode == CentralWidgetMode::ViewSessionMode)
    {
        if (centralWidget() != view)
        {
            takeCentralWidget(); // Remove ownership from welcomeLabel
            setCentralWidget(view);
            view->show();
            welcomeLabel->hide();
        }

        // Enable Job Creation
        ui->actionAddJob->setEnabled(true);
        ui->actionAddJob->setToolTip(tr("Add train job"));

        // Update actions based on Job selection
        JobStopEntry selectedJob =
          Session->getViewManager()->getLineGraphMgr()->getCurrentSelectedJob();
        onJobSelected(selectedJob.jobId);
    }
    else
    {
        if (centralWidget() != welcomeLabel)
        {
            takeCentralWidget(); // Remove ownership from LineGraphWidget
            setCentralWidget(welcomeLabel);
            view->hide();
            welcomeLabel->show();
        }

        // If there aren't lines prevent from creating jobs
        ui->actionAddJob->setEnabled(false);
        ui->actionAddJob->setToolTip(
          tr("You must create at least one railway segment before adding job to this session"));
        ui->actionRemoveJob->setEnabled(false);
    }

    m_mode = mode;
}

void MainWindow::onCloseSession()
{
    closeSession();
}

void MainWindow::onProperties()
{
    OwningQPointer<PropertiesDialog> dlg = new PropertiesDialog(this);
    dlg->exec();
}

void MainWindow::onMeetingInformation()
{
    OwningQPointer<MeetingInformationDialog> dlg = new MeetingInformationDialog(this);
    int ret                                      = dlg->exec();
    if (dlg && ret == QDialog::Accepted)
        dlg->saveData();
}

bool MainWindow::closeSession()
{
    DB_Error err = Session->closeDB();

    if (err == DB_Error::DbBusyWhenClosing)
    {
        if (closeTimerId)
        {
            // We already tried again

            stopCloseTimer();

            showCloseWarning();
            return false;
        }

        // Start a timer to try again
        closeTimerId = startTimer(1500);
        return false;
    }

    stopCloseTimer();

    if (err != DB_Error::NoError && err != DB_Error::DbNotOpen)
        return false;

    setCentralWidgetMode(CentralWidgetMode::StartPageMode);

    // Reset filePath to refresh title
    setCurrentFile(QString());

    return true;
}

void MainWindow::enableDBActions(bool enable)
{
    databaseActionGroup->setEnabled(enable);
    searchEdit->setEnabled(enable);
    if (!enable)
        jobEditor->setEnabled(false);

#ifdef ENABLE_BACKGROUND_MANAGER
    resPanelDock->widget()->setEnabled(enable);
#endif
}

void MainWindow::onStationManager()
{
    Session->getViewManager()->showStationsManager();
}

void MainWindow::onRollingStockManager()
{
    Session->getViewManager()->showRSManager();
}

void MainWindow::onShiftManager()
{
    Session->getViewManager()->showShiftManager();
}

void MainWindow::onJobsManager()
{
    Session->getViewManager()->showJobsManager();
}

void MainWindow::onAddJob()
{
    Session->getViewManager()->requestJobCreation();
}

void MainWindow::onRemoveJob()
{
    DEBUG_ENTRY;
    Session->getViewManager()->removeSelectedJob();
}

void MainWindow::onPrint()
{
    OwningQPointer<PrintWizard> wizard = new PrintWizard(Session->m_Db, this);
    wizard->setOutputType(Print::OutputType::Native);
    wizard->exec();
}

void MainWindow::onPrintPDF()
{
    OwningQPointer<PrintWizard> wizard = new PrintWizard(Session->m_Db, this);
    wizard->setOutputType(Print::OutputType::Pdf);
    wizard->exec();
}

void MainWindow::onExportSvg()
{
    OwningQPointer<PrintWizard> wizard = new PrintWizard(Session->m_Db, this);
    wizard->setOutputType(Print::OutputType::Svg);
    wizard->exec();
}

#ifdef ENABLE_USER_QUERY
void MainWindow::onExecQuery()
{
    DEBUG_ENTRY;
    SQLConsole *console = new SQLConsole(this);
    console->setAttribute(Qt::WA_DeleteOnClose);
    console->show();
}
#endif

void MainWindow::onOpenSettings()
{
    DEBUG_ENTRY;
    OwningQPointer<SettingsDialog> dlg = new SettingsDialog(this);
    dlg->loadSettings();
    dlg->exec();
}

void MainWindow::checkLineNumber()
{
    RailwaySegmentHelper helper(Session->m_Db);

    bool isLine      = false;
    db_id graphObjId = 0;

    if (!helper.findFirstLineOrSegment(graphObjId, isLine))
        graphObjId = 0;
    if (graphObjId && m_mode != CentralWidgetMode::ViewSessionMode)
    {
        // First line was added or newly opened file -> Session has at least one line
        setCentralWidgetMode(CentralWidgetMode::ViewSessionMode);

        // Load first line or segment
        view->tryLoadGraph(graphObjId,
                           isLine ? LineGraphType::RailwayLine : LineGraphType::RailwaySegment);
    }
    else if (graphObjId == 0 && m_mode != CentralWidgetMode::NoLinesWarningMode)
    {
        // Last line removed -> Session has no line
        setCentralWidgetMode(CentralWidgetMode::NoLinesWarningMode);
    }
}

void MainWindow::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == closeTimerId)
    {
        closeSession();
        return;
    }

    QMainWindow::timerEvent(e);
}

void MainWindow::onJobSelected(db_id jobId)
{
    const bool selected = jobId != 0;
    ui->actionPrev_Job_Segment->setEnabled(selected);
    ui->actionNext_Job_Segment->setEnabled(selected);
    ui->actionRemoveJob->setEnabled(selected);

    QString removeJobTooltip;
    if (selected)
        removeJobTooltip = tr("Remove selected Job");
    else
        removeJobTooltip = tr("First select a Job by double click on graph or type in search box");
    ui->actionRemoveJob->setToolTip(removeJobTooltip);
}

// QT-BUG 69922: If user closes a floating dock widget, when shown again it cannot dock anymore
// HACK: intercept dock close event and manually re-dock and hide so next time is shown it's docked
// NOTE: calling directly 'QDockWidget::setFloating(false)' from inside 'eventFinter()' causes CRASH
//       so queue it. Cannot use 'QMetaObject::invokeMethod()' because it's not a slot.
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == jobDock && event->type() == QEvent::Close)
    {
        if (jobDock->isFloating())
        {
            QTimer::singleShot(0, jobDock, [this]() { jobDock->setFloating(false); });
        }
    }
#ifdef ENABLE_BACKGROUND_MANAGER
    else if (watched == resPanelDock && event->type() == QEvent::Close)
    {
        if (resPanelDock->isFloating())
        {
            QTimer::singleShot(0, resPanelDock, [this]() { resPanelDock->setFloating(false); });
        }
    }
#endif // ENABLE_BACKGROUND_MANAGER

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onSessionRSViewer()
{
    Session->getViewManager()->showSessionStartEndRSViewer();
}

void MainWindow::onJobSearchItemSelected()
{
    db_id jobId = 0;
    QString tmp;
    if (!searchEdit->getData(jobId, tmp))
        return;

    searchEdit->clear(); // Clear text
    Session->getViewManager()->requestJobSelection(jobId, true, true);
}

void MainWindow::onJobSearchResultsReady()
{
    searchEdit->resizeColumnToContents();
    searchEdit->selectFirstIndexOrNone(true);
}
