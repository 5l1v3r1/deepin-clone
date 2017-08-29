#ifndef UTILITYLISTITEM_H
#define UTILITYLISTITEM_H

#include <QWidget>
#include <QIcon>

QT_BEGIN_NAMESPACE
class QLabel;
class QHBoxLayout;
QT_END_NAMESPACE

class RightContentLabel;
class UtilityListItem : public QWidget
{
    Q_OBJECT
public:
    explicit UtilityListItem(QWidget *parent);

    void setTitle(const QString &title);
    void setMessage(const QString &message);

    QString title() const;

    void setIcon(const QIcon &icon, const QSize &size = QSize());
    QIcon icon() const;

    void addWidget(QWidget *w, int stretch = 0, Qt::Alignment alignment = Qt::Alignment());

private:
    QIcon m_iconFile;
    QLabel *m_icon;
    RightContentLabel *m_label;
    QHBoxLayout *m_layout;
};

#endif // UTILITYLISTITEM_H
