#ifndef LINEGRAPHWIDGET_H
#define LINEGRAPHWIDGET_H

#include <QWidget>

#include "utils/types.h"

#include "graph/linegraphtypes.h"

class LineGraphScene;
class LineGraphView;
class LineGraphToolbar;

/*!
 * \brief An all-in-one widget as a railway line view
 *
 * This widget encapsulate a toolbar to select which
 * contents user wants to see (stations, segments or railway lines)
 * and a view which renders the chosen contents
 *
 * \sa LineGraphToolbar
 * \sa LineGraphView
 */
class LineGraphWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LineGraphWidget(QWidget *parent = nullptr);

    inline LineGraphScene *getScene() const { return m_scene; }
    inline LineGraphView *getView() const { return view; }
    inline LineGraphToolbar *getToolbar() const { return toolBar; }

    bool tryLoadGraph(db_id graphObjId, LineGraphType type);

private:
    LineGraphScene *m_scene;

    LineGraphView *view;
    LineGraphToolbar *toolBar;
};

#endif // LINEGRAPHWIDGET_H
