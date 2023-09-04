/*
    SPDX-FileCopyrightText: 2004 Thomas Baumgart tbaumgart @kde.org
    SPDX-FileCopyrightText: 2018 Ralf Habacker ralf.habacker @freenet.de

    This file is part of libalkimia.

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ALKONLINEQUOTESWIDGET_H
#define ALKONLINEQUOTESWIDGET_H

#include <alkimia/alkonlinequote.h>

#include <QWidget>

class QListWidgetItem;

class ALK_EXPORT AlkOnlineQuotesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AlkOnlineQuotesWidget(bool showProfiles = false, bool showUpload = false, QWidget *parent = 0);
    virtual ~AlkOnlineQuotesWidget();

    void writeConfig();
    void readConfig();
    void resetConfig();

    QWidget *profilesWidget();
    QWidget *profileDetailsWidget();
    QWidget *onlineQuotesWidget();
    QWidget *quoteDetailsWidget();
    QWidget *debugWidget();

    QString acceptLanguage() const;
    void setAcceptLanguage(const QString &text);

private:
    class Private;
    Private *const d;
};

#endif
