#ifndef PROPERTYDELEGATE_HH
#define PROPERTYDELEGATE_HH

#include <QItemDelegate>

class PropertyWrapper;

class PropertyDelegate : public QItemDelegate
{
  Q_OBJECT

public:
  explicit PropertyDelegate(QObject *parent=nullptr);

  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
  void setEditorData(QWidget *editor, const QModelIndex &index) const;
  void setModelData(QWidget *editor, QAbstractItemModel *abstractmodel, const QModelIndex &index) const;

protected:
  static const PropertyWrapper *getModel(const QAbstractItemModel *model);
};

#endif // PROPERTYDELEGATE_HH
