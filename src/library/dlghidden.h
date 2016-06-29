#ifndef DLGHIDDEN_H
#define DLGHIDDEN_H

#include "library/ui_dlghidden.h"
#include "preferences/usersettings.h"
#include "library/library.h"
#include "library/libraryview.h"
#include "library/trackcollection.h"
#include "controllers/keyboard/keyboardeventfilter.h"

class WTrackTableView;
class HiddenTableModel;
class QItemSelection;

class DlgHidden : public QWidget, public Ui::DlgHidden {
    Q_OBJECT
  public:
    DlgHidden(QWidget* parent);
    virtual ~DlgHidden();

    void setSelectedIndexes(const QModelIndexList& selectedIndexes);
    void setTableModel(HiddenTableModel* pTableModel);

  public slots:
    void onShow();

  signals:
    void selectAll();  
    void unhide();
    void purge();
    void trackSelected(TrackPointer pTrack);

  private:
    void activateButtons(bool enable);
    HiddenTableModel* m_pHiddenTableModel;
};

#endif //DLGHIDDEN_H
