/*
    SPDX-FileCopyrightText: 2018 Ralf Habacker ralf.habacker @freenet.de
    SPDX-FileCopyrightText: 2019 Thomas Baumgart tbaumgart @kde.org

    This file is part of libalkimia.

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifdef ENABLE_FINANCEQUOTE
#include "alkfinancequoteprocess.h"
#endif
#include "alkonlinequotesprofile.h"
#include "alkonlinequotesprofilemanager.h"

#include "alkonlinequotesource.h"

#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QString>
#include <QtDebug>
#include <QFileInfo>

#include <KConfig>
#include <KConfigGroup>

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
#include <KNSCore/Engine>
#include <KSharedConfig>
#include <QStandardPaths>
namespace KNS = KNSCore;
#else
    #include <KGlobal>
    #include <KStandardDirs>
    #include <knewstuff3/downloadmanager.h>
    namespace KNS = KNS3;
#endif
#include <QRegExp>

class AlkOnlineQuotesProfile::Private : public QObject
{
    Q_OBJECT
public:
    AlkOnlineQuotesProfile *m_p;
    QString m_name;
    QString m_GHNSFile;
    QString m_GHNSFilePath;
    QString m_kconfigFile;
    AlkOnlineQuotesProfileManager *m_profileManager;
#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
    KNS::DownloadManager *m_manager = 0;
    KConfig* m_config;
#else
    KNSCore::Engine *m_engine = 0;
    KSharedConfigPtr m_config;
#endif
    Type m_type;
    static QString m_financeQuoteScriptPath;
    static QStringList m_financeQuoteSources;

    bool setupFinanceQuoteScriptPath()
    {
        if (m_financeQuoteScriptPath.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
            m_financeQuoteScriptPath =
                QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("alkimia%1/misc/financequote.pl").arg(TARGET_SUFFIX));
#else
            m_financeQuoteScriptPath = KGlobal::dirs()->findResource("appdata",
                                                                     QString("misc/financequote.pl"));
#endif
        }
        return !m_financeQuoteScriptPath.isEmpty();
    }

    explicit Private(AlkOnlineQuotesProfile *p)
        : m_p(p)
        , m_profileManager(0)
        , m_config(0)
        , m_type(Type::Undefined)
    {
#ifdef ENABLE_FINANCEQUOTE
        setupFinanceQuoteScriptPath();
#endif
    }

    ~Private()
    {
#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
        delete m_manager;
        delete m_config;
#endif
    }

    void checkUpdates()
    {
#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
        m_manager = new KNS::DownloadManager(m_p->hotNewStuffConfigFile(), this);
        // to know when checking for updates is done
        connect(m_manager, SIGNAL(searchResult(KNS3::Entry::List)), this,
                SLOT(slotUpdatesFound(KNS3::Entry::List)));
        // to know about finished installations
        connect(m_manager, SIGNAL(entryStatusChanged(KNS3::Entry)), this,
                SLOT(entryStatusChanged(KNS3::Entry)));
        // start checking for updates
        m_manager->checkForUpdates();
#else
        m_engine = new KNSCore::Engine(this);
        if (m_engine->init(m_p->hotNewStuffConfigFile())) {
            connect(m_engine, &KNSCore::Engine::signalUpdateableEntriesLoaded, this, [this](const KNSCore::EntryInternal::List &updates) {
                for (const KNSCore::EntryInternal &entry : updates) {
                    qDebug() << "update available in profile" << m_p->name() << "for" << entry.name() << entry.version() << entry.uniqueId() << entry.category() << entry.providerId();
                    Q_EMIT m_p->updateAvailable(m_p->name(), entry.name());
                }
            });
            connect(m_engine, &KNSCore::Engine::signalProvidersLoaded, this, [this]() {
                m_engine->checkForUpdates();
            });
        }
#endif
    }

public Q_SLOTS:
    void slotUpdatesFound(const KNS3::Entry::List &updates)
    {
        for (const KNS3::Entry &entry : updates) {
            qDebug() << "update available in profile" << m_p->name() << "for" << entry.name() << entry.version() << entry.id() << entry.category() << entry.providerId();
            Q_EMIT m_p->updateAvailable(m_p->name(), entry.name());
        }
    }

    // to know about finished installations
    void entryStatusChanged(const KNS3::Entry &entry)
    {
        qDebug() << __FUNCTION__ << entry.name() << entry.status() << entry.summary();
    }

    const QStringList quoteSourcesNative()
    {
        //KSharedConfigPtr kconfig = KGlobal::config();
        auto kconfig = KSharedConfig::openConfig(m_kconfigFile, KConfig::SimpleConfig);
        QStringList groups = kconfig->groupList();

        QStringList::Iterator it;
        QRegExp onlineQuoteSource(QString("^Online-Quote-Source-(.*)$"));

        // get rid of all 'non online quote source' entries
        for (it = groups.begin(); it != groups.end(); it = groups.erase(it)) {
            if (onlineQuoteSource.indexIn(*it) >= 0) {
                // Insert the name part
                it = groups.insert(it, onlineQuoteSource.cap(1));
                ++it;
            }
        }

        // Set up each of the default sources.  These are done piecemeal so that
        // when we add a new source, it's automatically picked up. And any changes
        // are also picked up.
        QMap<QString, AlkOnlineQuoteSource> defaults = defaultQuoteSources();
        QMap<QString, AlkOnlineQuoteSource>::iterator it_source = defaults.begin();
        while (it_source != defaults.end()) {
            if (!groups.contains((*it_source).name())) {
                groups += (*it_source).name();
                (*it_source).write();
                kconfig->sync();
            }
            ++it_source;
        }

        return groups;
    }

#ifdef ENABLE_FINANCEQUOTE
    const QStringList quoteSourcesFinanceQuote()
    {
        if (m_financeQuoteSources.empty()) { // run the process one time only
            // since this is a static function it can be called without constructing an object
            // so we need to make sure that m_financeQuoteScriptPath is properly initialized
            if (setupFinanceQuoteScriptPath()) {
                AlkFinanceQuoteProcess testList;
                testList.testLaunch(m_financeQuoteScriptPath);
                while (!testList.isFinished()) {
                    qApp->processEvents();
                }

                if (testList.exitCode() == 0) {
                    AlkFinanceQuoteProcess getList;
                    getList.launch(m_financeQuoteScriptPath);
                    while (!getList.isFinished()) {
                        qApp->processEvents();
                    }
                    m_financeQuoteSources = getList.getSourceList();
                }
            }
        }
        return m_financeQuoteSources;
    }
#endif

    const QStringList quoteSourcesSkrooge()
    {
        return quoteSourcesGHNS();
    }

    const QStringList quoteSourcesGHNS()
    {
        QStringList sources;

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
        QStringList resources;
        const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, m_GHNSFilePath, QStandardPaths::LocateDirectory);
        Q_FOREACH (const QString& dir, dirs) {
            const QStringList fileNames = QDir(dir).entryList(QStringList() << QStringLiteral("*.txt"));
            Q_FOREACH (const QString& file, fileNames) {
                resources.append(dir + '/' + file);
            }
        }
#else
        const QString filename = QString("%1/*.txt").arg(m_GHNSFilePath);
        const QStringList resources = KStandardDirs().findAllResources("data", filename);
#endif
        for (const QString &file : resources) {
            QFileInfo f(file);
            QString file2 = f.completeBaseName();
            AlkOnlineQuoteSource source(file2, m_p);
            if (source.isEmpty()) {
                qDebug() << "skipping" << file;
                continue;
            }
            if (!sources.contains(file2)) {
                qDebug() << "adding quote source" << file;
                sources.push_back(file2);
            }
        }

        return sources;
    }

    const AlkOnlineQuotesProfile::Map defaultQuoteSources()
    {
        QMap<QString, AlkOnlineQuoteSource> result;

        // Use fx-rate.net as the standard currency exchange rate source until
        // we have the capability to use more than one source. Use a neutral
        // name for the source.

        switch (m_p->type()) {
        case AlkOnlineQuotesProfile::Type::None:
        case AlkOnlineQuotesProfile::Type::Alkimia4:
        case AlkOnlineQuotesProfile::Type::Alkimia5: {
            AlkOnlineQuoteSource source(AlkOnlineQuoteSource::defaultCurrencyQuoteSource("Alkimia Currency"));
            source.setProfile(m_p);
            result[source.name()] = source;
#if defined(BUILD_WITH_WEBKIT) || defined(BUILD_WITH_WEBENGINE)
            AlkOnlineQuoteSource source2(AlkOnlineQuoteSource::defaultCurrencyQuoteSource("Alkimia Currency.webkit"));
            source2.setProfile(m_p);
            result[source2.name()] = source2;
#endif
            break;
        }
        default:
            break;
        }
        return result;
    }

    /**
     * @brief return data root path
     * @return path
     */
    QString dataRootPath()
    {
        return QLibraryInfo::location(QLibraryInfo::PrefixPath) + "/share";
    }

    /**
     * @brief return home root path
     * @return path
     */
    QString homeRootPath()
    {
        if (m_type == Type::KMyMoney5 || m_type == Type::Alkimia5 || m_type == Type::Skrooge5)
            return QDir::homePath();
        else if (m_type == Type::KMyMoney4 || m_type == Type::Alkimia4 || m_type == Type::Skrooge4) {
#ifdef Q_OS_WIN
            return qgetenv("APPDATA");
#else
            return QDir::homePath();
#endif
        } else {
            return QString();
        }
    }

    QString configPath()
    {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
        if (m_type == Type::KMyMoney5)
            return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        else if(m_type == Type::Alkimia5 || m_type == Type::Skrooge5)
            return QString("%1/.config").arg(homeRootPath());
        else
#endif
        if (m_type == Type::KMyMoney4 || m_type == Type::Alkimia4 || m_type == Type::Skrooge4)
            return QString("%1/.kde4/share/config").arg(homeRootPath());
        return
            QString();
    }

    QString dataReadPath()
    {
        if (m_type == Type::KMyMoney5 || m_type == Type::Alkimia5 || m_type == Type::Skrooge5)
            return dataRootPath();
        else if (m_type == Type::KMyMoney4 || m_type == Type::Alkimia4 || m_type == Type::Skrooge4)
            return QString("%1/kde4/apps").arg(dataRootPath());
        return
            QString();
    }

    QString dataWritePath()
    {
        if (m_type == Type::KMyMoney5 || m_type == Type::Alkimia5 || m_type == Type::Skrooge5)
            return QString("%1/.local/share").arg(homeRootPath());
        else if (m_type == Type::KMyMoney4 || m_type == Type::Alkimia4 || m_type == Type::Skrooge4)
            return QString("%1/.kde4/share/apps").arg(homeRootPath());
        return
            QString();
    }
};

// define static members
QString AlkOnlineQuotesProfile::Private::m_financeQuoteScriptPath;
QStringList AlkOnlineQuotesProfile::Private::m_financeQuoteSources;


AlkOnlineQuotesProfile::AlkOnlineQuotesProfile(const QString &name, Type type,
                                               const QString &ghnsConfigFile)
    : d(new Private(this))
{
    d->m_name = name;
    d->m_GHNSFile = ghnsConfigFile;
    d->m_type = type;
    if (type == Type::KMyMoney5)
        d->m_kconfigFile = QString("%1/kmymoney/kmymoneyrc").arg(d->configPath());
    else if (type == Type::KMyMoney4)
        d->m_kconfigFile = QString("%1/kmymoneyrc").arg(d->configPath());
    else if (type == Type::Alkimia5 || type == Type::Alkimia4)
        d->m_kconfigFile = QString("%1/alkimiarc").arg(d->configPath());
    else
        d->m_kconfigFile.clear();

    if (!d->m_kconfigFile.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
        d->m_config = KSharedConfig::openConfig(d->m_kconfigFile, KConfig::SimpleConfig);
        qDebug() << (void*)d->m_config.constData();
#else
        d->m_config = new KConfig(d->m_kconfigFile);
#endif
    }

    if (!d->m_GHNSFile.isEmpty()) {
        KConfig ghnsFile(hotNewStuffConfigFile());
        KConfigGroup group = ghnsFile.group("KNewStuff3");
        d->m_GHNSFilePath = group.readEntry("TargetDir");
        d->checkUpdates();
    }
}

AlkOnlineQuotesProfile::~AlkOnlineQuotesProfile()
{
    delete d;
}

QString AlkOnlineQuotesProfile::name() const
{
    return d->m_name;
}

QString AlkOnlineQuotesProfile::hotNewStuffConfigFile() const
{
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    QString configFile = QStandardPaths::locate(QStandardPaths::ConfigLocation, d->m_GHNSFile);
    if (configFile.isEmpty()) {
        configFile = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "knsrcfiles/" + d->m_GHNSFile);
    }
#else
    QString configFile = KStandardDirs::locate("config", d->m_GHNSFile);
    if (configFile.isEmpty()) {
         configFile = KStandardDirs::locate("data", "knsrcfiles/" + d->m_GHNSFile);
    }
#endif
    if (configFile.isEmpty()) {
        configFile = QString("%1/%2").arg(KNSRC_DIR, d->m_GHNSFile);
    }

    return configFile;
}

QString AlkOnlineQuotesProfile::hotNewStuffReadFilePath(const QString &fileName) const
{
    for (const QString &path : hotNewStuffReadPath()) {
        QFileInfo f(path + fileName);
        if (f.exists())
            return f.absoluteFilePath();
    }
    return QString();
}

QString AlkOnlineQuotesProfile::hotNewStuffWriteFilePath(const QString &fileName) const
{
    return QString("%1%2").arg(hotNewStuffWriteDir(), fileName);
}

QStringList AlkOnlineQuotesProfile::hotNewStuffReadPath() const
{
    return QStringList()
        << QString("%1/%2/").arg(d->dataReadPath(), d->m_GHNSFilePath)
        << hotNewStuffWriteDir();
}

QString AlkOnlineQuotesProfile::hotNewStuffWriteDir() const
{
    return QString("%1/%2/").arg(d->dataWritePath(), d->m_GHNSFilePath);
}

QString AlkOnlineQuotesProfile::hotNewStuffRelPath() const
{
    return d->m_GHNSFilePath;
}

QString AlkOnlineQuotesProfile::kConfigFile() const
{
    return d->m_kconfigFile;
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
KSharedConfigPtr AlkOnlineQuotesProfile::kConfig() const
#else
KConfig *AlkOnlineQuotesProfile::kConfig() const
#endif
{
    return d->m_config;
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
void AlkOnlineQuotesProfile::setKConfig(KSharedConfigPtr kconfig)
{
    d->m_config = kconfig;
}
#endif

AlkOnlineQuotesProfile::Type AlkOnlineQuotesProfile::type()
{
    return d->m_type;
}

bool AlkOnlineQuotesProfile::typeIsSupported() const
{
#ifndef ENABLE_FINANCEQUOTE
    if (d->m_type == Type::Script) {
        return false;
    }
#endif
    return true;
}

bool AlkOnlineQuotesProfile::typeIsOperational() const
{
    if (d->m_type == Type::Script) {
#ifdef ENABLE_FINANCEQUOTE
        if (d->quoteSourcesFinanceQuote().isEmpty()) {
            return false;
        }
#else
        return false;
#endif
    }
    return true;
}

bool AlkOnlineQuotesProfile::hasGHNSSupport()
{
    return !d->m_GHNSFile.isEmpty();
}

const AlkOnlineQuotesProfile::Map AlkOnlineQuotesProfile::defaultQuoteSources()
{
    return d->defaultQuoteSources();
}

const QStringList AlkOnlineQuotesProfile::quoteSources()
{
    QStringList result;
    switch(d->m_type) {
    case AlkOnlineQuotesProfile::Type::Alkimia4:
    case AlkOnlineQuotesProfile::Type::Alkimia5:
    case AlkOnlineQuotesProfile::Type::KMyMoney4:
    case AlkOnlineQuotesProfile::Type::KMyMoney5:
        result << d->quoteSourcesNative();
        break;
#ifdef ENABLE_FINANCEQUOTE
    case AlkOnlineQuotesProfile::Type::Script:
        result << d->quoteSourcesFinanceQuote();
        break;
#endif
    case AlkOnlineQuotesProfile::Type::None:
        result << d->defaultQuoteSources().keys();
        break;
    default:
        break;
    }
    if (hasGHNSSupport())
        result << d->quoteSourcesGHNS();
    return result;
}

void AlkOnlineQuotesProfile::setManager(AlkOnlineQuotesProfileManager *manager)
{
    d->m_profileManager = manager;
}

AlkOnlineQuotesProfileManager *AlkOnlineQuotesProfile::manager()
{
    return d->m_profileManager;
}

QString AlkOnlineQuotesProfile::scriptPath()
{
    return d->m_financeQuoteScriptPath;
}

#include "alkonlinequotesprofile.moc"
