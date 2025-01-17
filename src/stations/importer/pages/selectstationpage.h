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

#ifndef SELECTSTATIONPAGE_H
#define SELECTSTATIONPAGE_H

#include <QWizardPage>

#include "utils/types.h"

class StationImportWizard;

class QToolBar;
class QAction;
class QTableView;
class ImportStationModel;
class ModelPageSwitcher;

class SelectStationPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit SelectStationPage(StationImportWizard *w);

    void setupModel(ImportStationModel *m);
    void finalizeModel();

private slots:
    void openStationDlg();
    void openStationSVGPlan();
    void importSelectedStation();

private:
    StationImportWizard *mWizard;

    QToolBar *toolBar;
    QAction *actOpenStDlg;
    QAction *actOpenSVGPlan;
    QAction *actImportSt;

    QTableView *view;
    ModelPageSwitcher *pageSwitcher;

    ImportStationModel *stationsModel;
};

#endif // SELECTSTATIONPAGE_H
