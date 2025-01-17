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

#ifndef PRINTWORKER_H
#define PRINTWORKER_H

#include "utils/thread/iquittabletask.h"
#include "utils/worker_event_types.h"

#include "printdefs.h"

#include "utils/types.h"

#include "printing/helper/model/printhelper.h"

class QPrinter;
class QPainter;

class IGraphSceneCollection;

namespace sqlite3pp {
class database;
}

class PrintProgressEvent : public QEvent
{
public:
    enum
    {
        ProgressError         = -1,
        ProgressAbortedByUser = -2,
        ProgressMaxFinished   = -3
    };

    static constexpr Type _Type = Type(CustomEvents::PrintProgress);

    PrintProgressEvent(QRunnable *self, int pr, const QString &descrOrErr);

public:
    QRunnable *task;
    int progress;
    QString descriptionOrError;
};

class PrintWorker : public IQuittableTask
{
public:
    PrintWorker(sqlite3pp::database &db, QObject *receiver);
    ~PrintWorker();

    void setPrinter(QPrinter *printer);

    inline Print::PrintBasicOptions getPrintOpt() const
    {
        return printOpt;
    };
    void setPrintOpt(const Print::PrintBasicOptions &newPrintOpt);

    void setCollection(IGraphSceneCollection *newCollection);
    int getMaxProgress() const;

    void setScenePageLay(const Print::PageLayoutOpt &pageLay);

    // IQuittableTask
    void run() override;

private:
    bool printSvg();
    bool printPdf();
    bool printPaged();

private:
    typedef std::function<bool(QPainter *painter, const QString &title, const QRectF &sourceRect,
                               const QString &type, int progressiveNum)>
      BeginPaintFunc;

    bool printInternal(BeginPaintFunc func, bool endPaintingEveryPage);
    bool printInternalPaged(BeginPaintFunc func, bool endPaintingEveryPage);

public:
    // For each scene, count 10 steps
    static constexpr int ProgressStepsForScene = 10;

    bool sendProgressOrAbort(int progress, const QString &msg);

private:
    QPrinter *m_printer;
    Print::PrintBasicOptions printOpt;
    Print::PageLayoutOpt scenePageLay;

    IGraphSceneCollection *m_collection;
};

#endif // PRINTWORKER_H
