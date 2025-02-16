#include "extensionwrapper.hh"
#include <QMetaProperty>
#include "logger.hh"
#include "configreference.hh"


/* ******************************************************************************************** *
 * Implementation of ExtensionProxy
 * ******************************************************************************************** */
ExtensionProxy::ExtensionProxy(QObject *parent)
  : QIdentityProxyModel(parent)
{
  // pass...
}

void
ExtensionProxy::setSourceModel(QAbstractItemModel *sourceModel) {
  QIdentityProxyModel::setSourceModel(sourceModel);

  _indexP2S.clear(); _indexS2P.clear();
  if (PropertyWrapper *model = qobject_cast<PropertyWrapper *>(sourceModel)) {
    // Create root element maps
    for (int s=0,p=0; s<model->rowCount(QModelIndex()); s++) {
      if (model->isExtension(model->index(s,0, QModelIndex()))) {
        _indexP2S[p] = s;
        _indexS2P[s] = p;
        p++;
      }
    }
  }
}

int
ExtensionProxy::rowCount(const QModelIndex &parent) const {
  if (nullptr == sourceModel())
    return 0;
  // Non root elements
  if (parent.isValid())
    return sourceModel()->rowCount(mapToSource(parent));

  // root element
  return _indexP2S.count();
}

int
ExtensionProxy::columnCount(const QModelIndex &parent) const {
  if (nullptr == sourceModel())
    return 0;
  return sourceModel()->columnCount(mapToSource(parent));
}

QModelIndex
ExtensionProxy::index(int row, int column, const QModelIndex &parent) const {
  if (parent.isValid())
    return mapFromSource(sourceModel()->index(row, column, mapToSource(parent)));
  return createIndex(row, column, qobject_cast<PropertyWrapper *>(sourceModel())->root());
}

QModelIndex
ExtensionProxy::parent(const QModelIndex &child) const {
  if (! child.isValid())
    return QModelIndex();

  ConfigItem *root = qobject_cast<PropertyWrapper*>(sourceModel())->root();
  ConfigItem *parent = reinterpret_cast<ConfigItem*>(child.internalPointer());
  if ((root == parent) || (nullptr == parent))
    return QModelIndex();

  ConfigItem *gp = qobject_cast<ConfigItem*>(parent->parent());
  if (nullptr == gp)
    return QModelIndex();

  // Search for index of parent in grand-parent:
  const QMetaObject *meta = gp->metaObject();
  for (int p=QObject::staticMetaObject.propertyCount(); p<meta->propertyCount(); p++) {
    QMetaProperty prop = meta->property(p);
    if (! prop.isValid())
      continue;
    if (prop.read(gp).value<ConfigItem *>() == parent) {
      if (root == gp)
        return createIndex(_indexS2P[p-QObject::staticMetaObject.propertyCount()], 0,
            reinterpret_cast<quintptr>(gp));
      else
        return createIndex(p-QObject::staticMetaObject.propertyCount(), 0,
            reinterpret_cast<quintptr>(gp));
    }
  }

  return QModelIndex();
}

QModelIndex
ExtensionProxy::mapToSource(const QModelIndex &proxyIndex) const {
  // root maps to root
  if (! proxyIndex.isValid())
    return QModelIndex();
  // If no source -> blub.
  if (nullptr == sourceModel())
    return QModelIndex();

  // If index is not an immediate child of root -> map 1:1
  if (proxyIndex.parent().isValid()) {
    return sourceModel()->index(
          proxyIndex.row(), proxyIndex.column(), mapToSource(proxyIndex.parent()));
  }

  // If index is an imediate child of root -> map only extensions
  if (_indexP2S.contains(proxyIndex.row()))
    return sourceModel()->index(
          _indexP2S[proxyIndex.row()], proxyIndex.column(), QModelIndex());

  return QModelIndex();
}

QModelIndex
ExtensionProxy::mapFromSource(const QModelIndex &sourceIndex) const {
  // root maps to root
  if (! sourceIndex.isValid())
    return QModelIndex();
  // If no source -> blub.
  if (nullptr == sourceModel())
    return QModelIndex();

  // If index is not an immediate child of root -> map 1:1
  if (sourceModel()->parent(sourceIndex).isValid()) {
    return createIndex(sourceIndex.row(), sourceIndex.column(), sourceIndex.internalPointer());
  }

  if (_indexS2P.contains(sourceIndex.row()))
    return createIndex(_indexS2P[sourceIndex.row()], sourceIndex.column(), sourceIndex.internalPointer());

  return QModelIndex();
}


/* ******************************************************************************************** *
 * Implementation of PropertyWrapper
 * ******************************************************************************************** */
PropertyWrapper::PropertyWrapper(ConfigItem *obj, QObject *parent)
  : QAbstractItemModel(parent), _object(obj)
{
  if (_object) {
    connect(_object, SIGNAL(beginClear()), this, SLOT(onItemClearing()));
    connect(_object, SIGNAL(endClear()), this, SLOT(onItemCleared()));
  }
}

ConfigItem *
PropertyWrapper::root() const {
  return _object;
}

ConfigItem *
PropertyWrapper::item(const QModelIndex &item) const {
  if (! item.isValid())
    return nullptr;

  ConfigItem *pobj = _object;
  if (nullptr != item.internalPointer())
    pobj = reinterpret_cast<ConfigItem *>(item.internalPointer());
  const QMetaObject *meta = pobj->metaObject();

  int pcount = meta->propertyCount() - QObject::staticMetaObject.propertyCount();
  if (item.row() < pcount) {
    QMetaProperty prop = meta->property(QObject::staticMetaObject.propertyCount() + item.row());
    return prop.read(pobj).value<ConfigItem *>();
  }
  return nullptr;
}

ConfigItem *
PropertyWrapper::parentObject(const QModelIndex &index) const {
  ConfigItem *pobj = _object;
  if (index.isValid())
     pobj = reinterpret_cast<ConfigItem *>(index.internalId());
  return pobj;
}

QMetaProperty
PropertyWrapper::propertyAt(const QModelIndex &index) const {
  ConfigItem *pobj = parentObject(index);
  const QMetaObject *meta = pobj->metaObject();

  int propCount = meta->propertyCount() - QObject::staticMetaObject.propertyCount();
  if (index.row() < propCount)
    return meta->property(index.row()+QObject::staticMetaObject.propertyCount());
  return QMetaProperty();
}

bool
PropertyWrapper::isExtension(const QModelIndex &index) const {
  if (! index.isValid())
    return false;
  QMetaProperty prop = propertyAt(index);
  return propIsInstance<ConfigExtension>(prop);
}

bool
PropertyWrapper::createInstanceAt(const QModelIndex &item) {
  ConfigItem *obj = parentObject(item);
  QMetaProperty prop = propertyAt(item);
  if ((nullptr == obj) || (! prop.isValid()))
    return false;
  // Check type of property
  if (! propIsInstance<ConfigItem>(prop))
    return false;
  // If property is already set -> abort
  if (prop.read(obj).value<ConfigItem*>())
    return false;
  // Get TypeObject of property
  if (QMetaType::UnknownType == prop.userType())
    return false;
  QMetaType type(prop.userType());
  if (! (QMetaType::PointerToQObject & type.flags()))
    return false;
  const QMetaObject *propType = type.metaObject();
  // Instantiate extension
  ConfigItem *ext = qobject_cast<ConfigItem *>(
        propType->newInstance(QGenericArgument(nullptr, obj)));
  if (nullptr == ext)
    return false;
  // store item
  beginInsertRows(item, 0, ext->metaObject()->propertyCount());
  prop.write(obj, QVariant::fromValue(ext));
  endInsertRows();
  emit dataChanged(index(item.row(), 0, item.parent()),
                   index(item.row(), 2, item.parent()));
  emit dataChanged(index(0, 0, item),
                   index(ext->metaObject()->propertyCount(),2, item));
  return true;
}

bool
PropertyWrapper::deleteInstanceAt(const QModelIndex &item) {
  ConfigItem *obj = parentObject(item);
  QMetaProperty prop = propertyAt(item);
  if ((nullptr == obj) || (! prop.isValid()))
    return false;
  // Check type of property
  if (! propIsInstance<ConfigItem>(prop))
    return false;
  // If property is set -> delete
  if (ConfigItem *ext = prop.read(obj).value<ConfigItem*>()) {
    if (! prop.isWritable())
      return false;
    beginRemoveRows(item, 0, rowCount(item));
    prop.write(obj, QVariant::fromValue<ConfigItem*>(nullptr));
    endRemoveRows();
    ext->deleteLater();
    return true;
  }
  return false;
}

QModelIndex
PropertyWrapper::index(int row, int column, const QModelIndex &parent) const {
  ConfigItem *pobj = _object;
  if (parent.isValid())
    pobj = item(parent);
  const QMetaObject *meta = pobj->metaObject();

  int pcount = meta->propertyCount() - QObject::staticMetaObject.propertyCount();
  if (row < pcount)
    return createIndex(row, column, pobj);

  return QModelIndex();
}

QModelIndex
PropertyWrapper::parent(const QModelIndex &child) const {
  if (! child.isValid())
    return QModelIndex();

  ConfigItem *pobj = reinterpret_cast<ConfigItem*>(child.internalPointer());
  if (_object == pobj)
    return QModelIndex();

  ConfigItem *gp = qobject_cast<ConfigItem*>(pobj->parent());
  if (nullptr == gp)
    return QModelIndex();

  // Search for parent in grand-parent's properties
  const QMetaObject *meta = gp->metaObject();
  for (int p=QObject::staticMetaObject.propertyCount(); p<meta->propertyCount(); p++) {
    QMetaProperty prop = meta->property(p);
    if (! prop.isValid())
      continue;
    if (prop.read(gp).value<ConfigItem *>() == pobj) {
      return createIndex(p-QObject::staticMetaObject.propertyCount(), 0, reinterpret_cast<quintptr>(gp));
    }
  }

  return QModelIndex();
}

int
PropertyWrapper::rowCount(const QModelIndex &parent) const {
  ConfigItem *pobj = _object;
  if (parent.isValid())
    pobj = item(parent);
  if (nullptr == pobj)
    return 0;
  const QMetaObject *meta = pobj->metaObject();
  int c = (meta->propertyCount() - QObject::staticMetaObject.propertyCount());
  return c;
}

int
PropertyWrapper::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return 3;
}

Qt::ItemFlags
PropertyWrapper::flags(const QModelIndex &index) const {
  // check if property is a config object or atomic (or reference)
  QMetaProperty prop = propertyAt(index);
  if (propIsInstance<ConfigItem>(prop)) {
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
  }

  if (prop.isWritable() && (1 == index.column()))
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemNeverHasChildren;
  return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
}

bool
PropertyWrapper::hasChildren(const QModelIndex &parent) const {
  if (! parent.isValid())
    return true;
  ConfigItem* pobj = reinterpret_cast<ConfigItem *>(parent.internalId());


  QMetaProperty prop = propertyAt(parent);
  if (! propIsInstance<ConfigItem>(prop))
    return false;
  return nullptr != prop.read(pobj).value<ConfigItem*>();
}

QVariant
PropertyWrapper::headerData(int section, Qt::Orientation orientation, int role) const {
  // Only handle horizontal display data
  if ((Qt::Horizontal != orientation) || (Qt::DisplayRole != role))
    return QAbstractItemModel::headerData(section, orientation, role);
  // Dispatch by section index
  switch (section) {
  case 0: return tr("Property");
  case 1: return tr("Value");
  case 2: return tr("Description");
  default: break;
  }
  // default
  return QAbstractItemModel::headerData(section, orientation, role);
}

QVariant
PropertyWrapper::data(const QModelIndex &index, int role) const {
  if (! index.isValid())
    return QVariant();

  ConfigItem *pobj = parentObject(index);
  QMetaProperty prop = propertyAt(index);

  if (0 == index.column()) {
    if (Qt::DisplayRole == role)
      return prop.name();
  } else if ((2 == index.column()) && (Qt::DisplayRole == role)) {
    if (propIsInstance<ConfigItem>(prop)) {
      ConfigItem *item = prop.read(pobj).value<ConfigItem*>();
      if (item && item->hasDescription())
        return item->description();
    }
    if (pobj->hasDescription(prop))
      return pobj->description(prop);
    return QVariant();
  } else if (Qt::ToolTipRole == role) {
    if (propIsInstance<ConfigItem>(prop)) {
      ConfigItem *item = prop.read(pobj).value<ConfigItem*>();
      if (item && item->hasLongDescription())
        return item->longDescription();
    }
    if (pobj->hasLongDescription(prop))
      return pobj->longDescription(prop);
    if (pobj->hasDescription(prop))
      return pobj->description(prop);
    return QVariant();
  }

  QVariant value = prop.read(pobj);
  if (prop.isEnumType() && ((Qt::DisplayRole == role) || (Qt::EditRole == role))) {
    QMetaEnum e = prop.enumerator();
    const char *key = e.valueToKey(value.toInt());
    if (nullptr == key) {
      logError() << "Cannot map value " << value.toUInt()
                 << " to enum " << e.name()
                 << ". Ignore attribute but this points to an incompatibility in some codeplug. "
                 << "Consider reporting it to https://github.com/hmatuschek/qdmr/issues.";
      return QVariant();
    }
    return QString(key);
  } else if ((QVariant::Bool == prop.type()) && (Qt::EditRole == role)) {
    return value;
  } else if ((QVariant::Bool == prop.type()) && (Qt::DisplayRole == role)) {
    if (value.toBool())
      return tr("true");
    return tr("false");
  } else if ( ((QVariant::Int == prop.type()) || (QVariant::UInt == prop.type()) ||
               (QVariant::Double == prop.type()) || (QVariant::String == prop.type()))
              && ((Qt::DisplayRole == role) || (Qt::EditRole==role)) ) {
    return value;
  } else if (value.value<ConfigObjectReference *>() && (Qt::DisplayRole == role)) {
    ConfigObjectReference *ref = value.value<ConfigObjectReference *>();
    ConfigObject *obj = ref->as<ConfigObject>();
    if (nullptr == obj)
      return tr("[None]");
    return QString("%1 (%2)").arg(obj->name()).arg(obj->metaObject()->className());
  } else if (value.value<ConfigObjectReference *>() && (Qt::EditRole == role)) {
    return value;
  } else if (propIsInstance<ConfigItem>(prop)) {
    ConfigItem *item = value.value<ConfigItem*>();
    if (Qt::DisplayRole == role) {
      if (nullptr == item)
        return tr("[None]");
      else
        return tr("Instance of %1").arg(item->metaObject()->className());
    }
  }

  return QVariant();
}

void
PropertyWrapper::onItemClearing() {
  beginResetModel();
}

void
PropertyWrapper::onItemCleared() {
  endResetModel();
}
