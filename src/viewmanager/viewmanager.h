#ifndef VIEWMANAGER_H
#define VIEWMANAGER_H

#include <QObject>

#include <QHash>

#include "utils/types.h"

class RollingStockManager;
class RSJobViewer;
class SessionStartEndRSViewer;

class StationsManager;
class StationJobView;
class StationFreeRSViewer;
class StationSVGPlanDlg;

class ShiftManager;
class ShiftViewer;
class ShiftGraphEditor;

class JobsManager;
class JobPathEditor;
class GraphManager; //FIXME: remove

class LineGraphManager;


class ViewManager : public QObject
{
    Q_OBJECT

    friend class MainWindow;
public:
    explicit ViewManager(QObject *parent = nullptr);

    void prepareQueries();
    void finalizeQueries();

    bool closeEditors();
    void clearAllLineGraphs();

    GraphManager *getGraphMgr() const;

    inline LineGraphManager *getLineGraphMgr() const { return lineGraphManager; }

    bool requestJobSelection(db_id jobId, bool select, bool ensureVisible) const;
    bool requestJobEditor(db_id jobId, db_id stopId = 0);
    bool requestJobCreation();
    bool requestClearJob(bool evenIfEditing = false);
    bool removeSelectedJob();

    bool requestJobShowPrevNextSegment(bool prev, bool select = true, bool ensureVisible = true);

    void updateRSPlans(QSet<db_id> set);

    void requestRSInfo(db_id rsId);
    void requestStJobViewer(db_id stId);
    void requestStSVGPlan(db_id stId);
    void requestStFreeRSViewer(db_id stId);
    void requestShiftViewer(db_id id);
    void requestShiftGraphEditor();

    void showRSManager();
    void showStationsManager();
    void showShiftManager();
    void showJobsManager();

    void showSessionStartEndRSViewer();

    void resumeRSImportation();

private slots:

    void onRSRemoved(db_id rsId);
    void onRSPlanChanged(db_id rsId);
    void onRSInfoChanged(db_id rsId);

    void onStRemoved(db_id stId);
    void onStNameChanged(db_id stId);
    void onStPlanChanged(db_id stId);

    void onShiftRemoved(db_id shiftId);
    void onShiftEdited(db_id shiftId);
    void onShiftJobsChanged(db_id shiftId);

private:
    RSJobViewer* createRSViewer(db_id rsId);
    StationJobView *createStJobViewer(db_id stId);
    StationSVGPlanDlg *createStPlanDlg(db_id stId, QString &stNameOut);
    StationFreeRSViewer *createStFreeRSViewer(db_id stId);
    ShiftViewer *createShiftViewer(db_id id);

private:
    GraphManager *mGraphMgr;
    LineGraphManager *lineGraphManager;

    QWidget *m_mainWidget;

    RollingStockManager *rsManager;
    QHash<db_id, RSJobViewer *> rsHash;
    SessionStartEndRSViewer *sessionRSViewer;

    StationsManager *stManager;
    QHash<db_id, StationJobView *> stHash;
    QHash<db_id, StationFreeRSViewer *> stRSHash;
    QHash<db_id, StationSVGPlanDlg *> stPlanHash;

    ShiftManager *shiftManager;
    QHash<db_id, ShiftViewer *> shiftHash;
    ShiftGraphEditor *shiftGraphEditor;

    JobsManager *jobsManager;
    JobPathEditor *jobEditor;
};

#endif // VIEWMANAGER_H
