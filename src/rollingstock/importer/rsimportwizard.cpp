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

#include "rsimportwizard.h"
#include "rsimportstrings.h"

#include "model/rsimportedmodelsmodel.h"
#include "model/rsimportedownersmodel.h"
#include "model/rsimportedrollingstockmodel.h"

#include "app/session.h"

#include "pages/optionspage.h"
#include "utils/wizard/choosefilepage.h"
#include "pages/loadingpage.h"
#include "pages/itemselectionpage.h"

#include "utils/delegates/kmspinbox/spinboxeditorfactory.h"
#include <QStyledItemDelegate>

#include "backends/rsbackendsmodel.h"
#include "backends/ioptionswidget.h"
#include "backends/loadtaskutils.h"
#include "backends/loadprogressevent.h"
#include "backends/importtask.h"
#include <QThreadPool>

// Backends
#include "backends/ods/rsimportodsbackend.h"
#include "backends/sqlite/rsimportsqlitebackend.h"

#include <QMessageBox>
#include <QPushButton>

#include "utils/owningqpointer.h"

RSImportWizard::RSImportWizard(bool resume, QWidget *parent) :
    QWizard(parent),
    loadTask(nullptr),
    importTask(nullptr),
    isStoppingTask(false),
    defaultSpeed(120),
    defaultRsType(RsType::FreightWagon),
    importMode(RSImportMode::ImportRSPieces),
    backendIdx(0)
{
    // Load backends
    backends = new RSImportBackendsModel(this);
    backends->addBackend(new RSImportODSBackend);
    backends->addBackend(new RSImportSQLiteBackend);

    modelsModel  = new RSImportedModelsModel(Session->m_Db, this);
    ownersModel  = new RSImportedOwnersModel(Session->m_Db, this);
    listModel    = new RSImportedRollingstockModel(Session->m_Db, this);

    loadFilePage = new LoadingPage(this);
    loadFilePage->setCommitPage(true);
    loadFilePage->setTitle(RsImportStrings::tr("File loading"));
    loadFilePage->setSubTitle(RsImportStrings::tr("Parsing file data..."));

    // HACK: I don't like the 'Commit' button. This hack makes it similar to 'Next' button
    loadFilePage->setButtonText(QWizard::CommitButton, buttonText(QWizard::NextButton));

    importPage = new LoadingPage(this);
    importPage->setTitle(RsImportStrings::tr("Importing"));
    importPage->setSubTitle(RsImportStrings::tr("Importing data..."));

    spinFactory = new SpinBoxEditorFactory;
    spinFactory->setRange(-1, 99999);
    spinFactory->setSpecialValueText(RsImportStrings::tr("Original"));
    spinFactory->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    ChooseFilePage *chooseFilePage = new ChooseFilePage;
    connect(chooseFilePage, &ChooseFilePage::fileChosen, this, &RSImportWizard::onFileChosen);

    setPage(OptionsPageIdx, new OptionsPage);
    setPage(ChooseFileIdx, chooseFilePage);
    setPage(LoadFileIdx, loadFilePage);
    setPage(SelectOwnersIdx,
            new ItemSelectionPage(this, ownersModel, nullptr, ownersModel,
                                  RSImportedOwnersModel::MatchExisting, ModelModes::Owners));
    setPage(SelectModelsIdx,
            new ItemSelectionPage(this, modelsModel, nullptr, modelsModel,
                                  RSImportedModelsModel::MatchExisting, ModelModes::Models));
    setPage(SelectRsIdx, new ItemSelectionPage(this, listModel, spinFactory, nullptr,
                                               RSImportedRollingstockModel::NewNumber,
                                               ModelModes::Rollingstock));
    setPage(ImportRsIdx, importPage);

    if (resume)
    {
        setStartId(SelectOwnersIdx);
        setWindowTitle(tr("Continue Rollingstock Importation"));
    }
    else
    {
        setWindowTitle(tr("Import Rollingstock"));
    }

    resize(700, 500);
}

RSImportWizard::~RSImportWizard()
{
    abortLoadTask();
    abortImportTask();
    delete spinFactory;
}

void RSImportWizard::done(int result)
{
    if (result == QDialog::Rejected || result == RejectWithoutAsking)
    {
        if (!isStoppingTask)
        {
            if (result == QDialog::Rejected) // RejectWithoutAsking skips this
            {
                OwningQPointer<QMessageBox> msgBox = new QMessageBox(this);
                msgBox->setIcon(QMessageBox::Question);
                msgBox->setWindowTitle(RsImportStrings::tr("Abort import?"));
                msgBox->setText(
                  RsImportStrings::tr("Do you want to import process? No data will be imported"));
                QPushButton *abortBut = msgBox->addButton(QMessageBox::Abort);
                QPushButton *noBut    = msgBox->addButton(QMessageBox::No);
                msgBox->setDefaultButton(noBut);
                msgBox->setEscapeButton(
                  noBut); // Do not Abort if dialog is closed by Esc or X window button
                msgBox->exec();
                bool abortClicked = msgBox && msgBox->clickedButton() == abortBut;
                if (!abortClicked)
                    return;
            }

            if (loadTask)
            {
                loadTask->stop();
                isStoppingTask = true;
                loadFilePage->setSubTitle(RsImportStrings::tr("Aborting..."));
            }

            if (importTask)
            {
                importTask->stop();
                isStoppingTask = true;
                importPage->setSubTitle(RsImportStrings::tr("Aborting..."));
            }
        }
        else
        {
            if (loadTask || importTask)
                return; // Already sent 'stop', just wait
        }

        // Reset to standard value because QWizard doesn't know about RejectWithoutAsking
        result = QDialog::Rejected;
    }

    // Clear tables after import process completed or was aborted
    Session->clearImportRSTables();

    QWizard::done(result);
}

bool RSImportWizard::validateCurrentPage()
{
    if (QWizard::validateCurrentPage())
    {
        if (nextId() == ImportRsIdx)
        {
            startImportTask();
        }
        return true;
    }
    return false;
}

int RSImportWizard::nextId() const
{
    int id = QWizard::nextId();
    switch (currentId())
    {
    case LoadFileIdx:
    {
        if ((importMode & RSImportMode::ImportRSOwners) == 0)
        {
            // Skip owners page
            id = SelectModelsIdx;
        }
        break;
    }
    case SelectOwnersIdx:
    {
        if ((importMode & RSImportMode::ImportRSModels) == 0)
        {
            // Skip models and rollingstock pages
            id = ImportRsIdx;
        }
        break;
    }
    case SelectModelsIdx:
    {
        if ((importMode & RSImportMode::ImportRSPieces) == 0)
        {
            // Skip rollingstock page
            id = ImportRsIdx;
        }
        break;
    }
    }
    return id;
}

bool RSImportWizard::event(QEvent *e)
{
    if (e->type() == LoadProgressEvent::_Type)
    {
        LoadProgressEvent *ev = static_cast<LoadProgressEvent *>(e);
        ev->setAccepted(true);

        if (ev->task == loadTask)
        {
            QString errText;
            if (ev->max == LoadProgressEvent::ProgressMaxFinished)
            {
                if (ev->progress == LoadProgressEvent::ProgressError)
                {
                    errText = loadTask->getErrorText();
                }

                loadFilePage->setSubTitle(tr("Completed."));

                // Delete task before handling event because otherwise it is detected as still
                // running
                delete loadTask;
                loadTask = nullptr;
                loadFilePage->setProgressCompleted(true);
            }

            loadFilePage->handleProgress(ev->progress, ev->max);

            if (ev->progress == LoadProgressEvent::ProgressError)
            {
                QMessageBox::warning(this, RsImportStrings::tr("Loading Error"), errText);
                reject();
            }
            else if (ev->progress == LoadProgressEvent::ProgressAbortedByUser)
            {
                reject(); // Reject the second time
            }
        }
        else if (ev->task == importTask)
        {
            if (ev->max == LoadProgressEvent::ProgressMaxFinished)
            {
                // Delete task before handling event because otherwise it is detected as still
                // running
                delete importTask;
                importTask = nullptr;
                importPage->setProgressCompleted(true);
            }

            importPage->handleProgress(ev->progress, ev->max);

            if (ev->progress == LoadProgressEvent::ProgressError)
            {
                // QMessageBox::warning(this, RsImportStrings::tr("Loading Error"), errText); TODO
                reject();
            }
            else if (ev->progress == LoadProgressEvent::ProgressAbortedByUser)
            {
                reject(); // Reject the second time
            }
        }

        return true;
    }
    else if (e->type() == QEvent::Type(CustomEvents::RsImportGoBackPrevPage))
    {
        e->setAccepted(true);
        back();
    }
    return QWizard::event(e);
}

void RSImportWizard::onFileChosen(const QString &filename)
{
    startLoadTask(filename);
}

bool RSImportWizard::startLoadTask(const QString &fileName)
{
    abortLoadTask();

    // Clear tables before starting new import process
    Session->clearImportRSTables();

    loadTask = createLoadTask(optionsMap, fileName);

    if (!loadTask)
    {
        QMessageBox::warning(this, RsImportStrings::tr("Error"),
                             RsImportStrings::tr("Invalid option selected. Please try again."));
        return false;
    }

    loadFilePage->setProgressCompleted(false);
    QThreadPool::globalInstance()->start(loadTask);
    return true;
}

void RSImportWizard::abortLoadTask()
{
    if (loadTask)
    {
        loadTask->stop();
        loadTask->cleanup();
        loadTask = nullptr;
    }
}

void RSImportWizard::startImportTask()
{
    abortImportTask();

    importTask = new ImportTask(Session->m_Db, this);
    importPage->setProgressCompleted(false);
    QThreadPool::globalInstance()->start(importTask);
}

void RSImportWizard::abortImportTask()
{
    if (importTask)
    {
        importTask->stop();
        importTask->cleanup();
        importTask = nullptr;
    }
}

void RSImportWizard::goToPrevPageQueued()
{
    qApp->postEvent(this, new QEvent(QEvent::Type(CustomEvents::RsImportGoBackPrevPage)));
}

void RSImportWizard::setDefaultTypeAndSpeed(RsType t, int speed)
{
    defaultRsType = t;
    defaultSpeed  = speed;
}

void RSImportWizard::setImportMode(int m)
{
    if (m == 0)
        m = RSImportMode::ImportRSPieces;
    if (m & RSImportMode::ImportRSPieces)
        m |= RSImportMode::ImportRSOwners | RSImportMode::ImportRSModels;
    importMode = m;
}

QAbstractItemModel *RSImportWizard::getBackendsModel() const
{
    return backends;
}

IOptionsWidget *RSImportWizard::createOptionsWidget(int idx, QWidget *parent)
{
    RSImportBackend *back = backends->getBackend(idx);
    if (!back)
        return nullptr;

    IOptionsWidget *w = back->createOptionsWidget();
    if (!w)
        return nullptr;

    w->setParent(parent);
    w->loadSettings(optionsMap);
    return w;
}

void RSImportWizard::setSource(int idx, IOptionsWidget *options)
{
    backendIdx = idx;
    optionsMap.clear();
    options->saveSettings(optionsMap);

    // Update ChooseFilePage
    ChooseFilePage *chooseFilePage = static_cast<ChooseFilePage *>(page(ChooseFileIdx));
    QString dlgTitle;
    QStringList fileFormats;

    options->getFileDialogOptions(dlgTitle, fileFormats);
    chooseFilePage->setFileDlgOptions(dlgTitle, fileFormats);
}

ILoadRSTask *RSImportWizard::createLoadTask(const QMap<QString, QVariant> &arguments,
                                            const QString &fileName)
{
    RSImportBackend *back = backends->getBackend(backendIdx);
    if (!back)
        return nullptr;

    ILoadRSTask *task = back->createLoadTask(arguments, Session->m_Db, importMode, defaultSpeed,
                                             defaultRsType, fileName, this);
    return task;
}
