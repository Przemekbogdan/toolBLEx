/*!
 * This file is part of toolBLEx.
 * Copyright (c) 2022 Emeric Grange - All Rights Reserved
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
 * \date      2022
 * \author    Emeric Grange <emeric.grange@gmail.com>
 */

#include "device_toolblex.h"
#include "BleServiceInfo.h"
#include "BleCharacteristicInfo.h"

#include "DeviceManager.h"

#include <cstdlib>
#include <cmath>

#include <QBluetoothUuid>
#include <QBluetoothAddress>
#include <QBluetoothServiceInfo>
#include <QLowEnergyService>

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <QSqlQuery>
#include <QSqlError>

#include <QDateTime>
#include <QTimer>
#include <QDebug>

/* ************************************************************************** */

DeviceToolBLEx::DeviceToolBLEx(const QString &deviceAddr, const QString &deviceName,
                               QObject *parent): Device(deviceAddr, deviceName, parent)
{
    // Creation from database cache

    m_isCached = true;
    m_hasCache = true;

    getSqlDeviceInfos();
}

DeviceToolBLEx::DeviceToolBLEx(const QBluetoothDeviceInfo &d, QObject *parent):
    Device(d, parent)
{
    // Creation from BLE scanning

    addAdvertisementEntry(d.rssi(), !d.manufacturerIds().empty(), !d.serviceIds().empty());

    m_isCached = (d.isCached() || d.rssi() == 0);
    m_firstSeen = QDateTime::currentDateTime();
    m_bluetoothCoreConfiguration = d.coreConfigurations();
}

DeviceToolBLEx::~DeviceToolBLEx()
{
    // will update 'last seen'
    updateCache();

    qDeleteAll(m_services);
    m_services.clear();

    qDeleteAll(m_advertisementEntries);
    m_advertisementEntries.clear();

    qDeleteAll(m_svd);
    m_svd.clear();
    qDeleteAll(m_svd_uuid);
    m_svd_uuid.clear();

    qDeleteAll(m_mfd);
    m_mfd.clear();
    qDeleteAll(m_mfd_uuid);
    m_mfd_uuid.clear();
}

bool DeviceToolBLEx::getSqlDeviceInfos()
{
    //qDebug() << "Device::getSqlDeviceInfos(" << m_deviceAddress << ")";
    bool status = false;

    if (m_dbInternal || m_dbExternal)
    {
        QSqlQuery getInfos;
        getInfos.prepare("SELECT deviceAddrMAC, deviceModel, deviceModelID, deviceManufacturer," \
                           "deviceFirmware, deviceBattery," \
                           "deviceCoreConfig, deviceClass," \
                           "starred, comment, color," \
                           "firstSeen, lastSeen " \
                         "FROM devices WHERE deviceAddr = :deviceAddr");
        getInfos.bindValue(":deviceAddr", getAddress());
        if (getInfos.exec())
        {
            while (getInfos.next())
            {
                m_deviceAddressMAC = getInfos.value(0).toString();
                m_deviceModel = getInfos.value(1).toString();
                m_deviceModelID = getInfos.value(2).toString();
                m_deviceManufacturer = getInfos.value(3).toString();

                m_deviceFirmware = getInfos.value(4).toString();
                m_deviceBattery = getInfos.value(5).toInt();

                m_bluetoothCoreConfiguration = getInfos.value(6).toInt();
                m_isBLE = (m_bluetoothCoreConfiguration == 1 || m_bluetoothCoreConfiguration == 3);
                m_isClassic = (m_bluetoothCoreConfiguration == 2 || m_bluetoothCoreConfiguration == 3);

                QString deviceClass = getInfos.value(7).toString();
                QStringList dc = deviceClass.split('-');
                if (dc.size() == 3)
                {
                    Device::setDeviceClass(dc.at(0).toInt(), dc.at(1).toInt(), dc.at(2).toInt());
                }

                m_userStarred = getInfos.value(8).toInt();
                m_userComment = getInfos.value(9).toString();
                m_userColor = getInfos.value(10).toString();

                m_firstSeen = getInfos.value(11).toDateTime();
                m_lastSeen = getInfos.value(12).toDateTime();

                QString settings = getInfos.value(11).toString();
                QJsonDocument doc = QJsonDocument::fromJson(settings.toUtf8());
                if (!doc.isNull() && doc.isObject())
                {
                    m_additionalSettings = doc.object();
                }

                status = true;
            }
        }
        else
        {
            qWarning() << "> getInfos.exec() ERROR"
                       << getInfos.lastError().type() << ":" << getInfos.lastError().text();
        }
    }

    return status;
}

/* ************************************************************************** */
/* ************************************************************************** */

void DeviceToolBLEx::setDeviceClass(const int major, const int minor, const int service)
{
    if (m_major != major || m_minor != minor || m_service != service)
    {
        Device::setDeviceClass(major, minor, service);
        updateCache();
    }
}

void DeviceToolBLEx::setCoreConfiguration(const int bleconf)
{
    if (bleconf > 0)
    {
        if (bleconf == 1 && !m_isBLE)
        {
            m_isBLE = true;
            Q_EMIT boolChanged();

            Device::setCoreConfiguration(bleconf);
            updateCache();
        }
        else if (bleconf == 2 && !m_isClassic)
        {
            m_isClassic = true;
            Q_EMIT boolChanged();

            Device::setCoreConfiguration(bleconf);
            updateCache();
        }
        else
        {
            Device::setCoreConfiguration(bleconf);
        }
    }
}

/* ************************************************************************** */

void DeviceToolBLEx::setBeacon(bool v)
{
    if (m_isBeacon != v)
    {
        m_isBeacon = v;
        Q_EMIT boolChanged();
    }
}

void DeviceToolBLEx::setBlacklisted(bool v)
{
    if (m_isBlacklisted != v)
    {
        m_isBlacklisted = v;
        Q_EMIT boolChanged();

        static_cast<DeviceManager *>(parent())->invalidate();
    }
}

void DeviceToolBLEx::setCached(bool v)
{
    if (m_isCached != v)
    {
        m_isCached = v;
        Q_EMIT boolChanged();

        static_cast<DeviceManager *>(parent())->invalidate();
    }
}

void DeviceToolBLEx::setCache(bool v)
{
    if (m_hasCache != v)
    {
        m_hasCache = v;
        Q_EMIT cacheChanged();

        static_cast<DeviceManager *>(parent())->invalidate();
    }
}

void DeviceToolBLEx::setPairingStatus(QBluetoothLocalDevice::Pairing p)
{
    if (m_pairingStatus != p)
    {
        m_pairingStatus = p;
        Q_EMIT pairingChanged();
    }
}

void DeviceToolBLEx::setDeviceColor(const QString &color)
{
    m_color = color;
}

void DeviceToolBLEx::setUserColor(const QString &color)
{
    if (m_userColor != color)
    {
        m_userColor = color;
        Q_EMIT colorChanged();

        updateCache();
    }
}

void DeviceToolBLEx::setUserComment(const QString &comment)
{
    if (m_userComment != comment)
    {
        m_userComment = comment;
        Q_EMIT commentChanged();

        updateCache();
    }
}

void DeviceToolBLEx::setUserStar(bool star)
{
    if (m_userStarred != star)
    {
        m_userStarred = star;
        Q_EMIT starChanged();

        updateCache();
    }
}

void DeviceToolBLEx::setLastSeen(const QDateTime &dt)
{
    m_lastSeen = dt;
    Q_EMIT seenChanged();
}

bool DeviceToolBLEx::isLastSeenToday()
{
    return (m_lastSeen.secsTo(QDateTime(QDate::currentDate(), QTime(0, 0, 0))) <= 0);
}

/* ************************************************************************** */

void DeviceToolBLEx::updateCache()
{
    if (m_dbInternal || m_dbExternal)
    {
        QString deviceClass;
        if (m_major && m_minor)
        {
            deviceClass = QString::number(m_major) + "-" +
                          QString::number(m_minor) + "-" +
                          QString::number(m_service);
        }

        QSqlQuery updateCache;
        updateCache.prepare("UPDATE devices SET "
                             "deviceCoreConfig = :deviceCoreConfig, "
                             "deviceClass = :deviceClass, "
                             "starred = :starred, "
                             "comment = :comment, "
                             "color = :color, "
                             "lastSeen = :lastSeen "
                            "WHERE deviceAddr = :deviceAddr");
        updateCache.bindValue(":deviceCoreConfig", m_bluetoothCoreConfiguration);
        updateCache.bindValue(":deviceClass", deviceClass);
        updateCache.bindValue(":starred", m_userStarred);
        updateCache.bindValue(":comment", m_userComment);
        updateCache.bindValue(":color", m_userColor);
        updateCache.bindValue(":lastSeen", m_lastSeen);
        updateCache.bindValue(":deviceAddr", getAddress());

        if (!updateCache.exec())
        {
            qWarning() << "> updateCache.exec() ERROR"
                       << updateCache.lastError().type() << ":" << updateCache.lastError().text();
        }
    }
}

/* ************************************************************************** */

void DeviceToolBLEx::blacklist(bool b)
{
    if (m_isBlacklisted != b)
    {
        if (b) static_cast<DeviceManager *>(parent())->blacklistDevice(m_deviceAddress);
        else static_cast<DeviceManager *>(parent())->whitelistDevice(m_deviceAddress);

        setBlacklisted(b);
    }
}

void DeviceToolBLEx::cache(bool c)
{
    if (m_isCached != c)
    {
        if (c) static_cast<DeviceManager *>(parent())->cacheDevice(m_deviceAddress);
        else static_cast<DeviceManager *>(parent())->uncacheDevice(m_deviceAddress);

        setCache(c);
        if (m_rssi >= 0) setCached(c);
    }
}

/* ************************************************************************** */
/* ************************************************************************** */

void DeviceToolBLEx::actionScanWithValues()
{
    qDebug() << "DeviceToolBLEx::actionScanWithValues()" << getAddress() << getName();

    if (!isBusy())
    {
        m_ble_action = DeviceUtils::ACTION_SCAN_WITH_VALUES;
        actionStarted();
        deviceConnect();
    }
}

void DeviceToolBLEx::actionScanWithoutValues()
{
    qDebug() << "DeviceToolBLEx::actionScanWithoutValues()" << getAddress() << getName();

    if (!isBusy())
    {
        m_ble_action = DeviceUtils::ACTION_SCAN_WITHOUT_VALUES;
        actionStarted();
        deviceConnect();
    }
}

/* ************************************************************************** */

void DeviceToolBLEx::askForNotify(const QString &uuid)
{
    // Iterate through services, until we find the characteristic we want to read
    for (auto s: m_services)
    {
        ServiceInfo *srv = qobject_cast<ServiceInfo *>(s);
        if (srv)
        {
            for (auto c: srv->getCharacteristicsInfos())
            {
                CharacteristicInfo *cst = qobject_cast<CharacteristicInfo *>(c);
                if (cst && cst->getUuidFull() == uuid)
                {
                    srv->askForNotify(uuid);
                    return;
                }
            }
        }
    }
}

void DeviceToolBLEx::askForRead(const QString &uuid)
{
    // Iterate through services, until we find the characteristic we want to read
    for (auto s: m_services)
    {
        ServiceInfo *srv = qobject_cast<ServiceInfo *>(s);
        if (srv)
        {
            for (auto c: srv->getCharacteristicsInfos())
            {
                CharacteristicInfo *cst = qobject_cast<CharacteristicInfo *>(c);
                if (cst && cst->getUuidFull() == uuid)
                {
                    srv->askForRead(uuid);
                    return;
                }
            }
        }
    }
}

void DeviceToolBLEx::askForWrite(const QString &uuid, const QString &value, const QString &type)
{
    // Iterate through services, until we find the characteristic we want to write
    for (auto s: m_services)
    {
        ServiceInfo *srv = qobject_cast<ServiceInfo *>(s);
        if (srv)
        {
            for (auto c: srv->getCharacteristicsInfos())
            {
                CharacteristicInfo *cst = qobject_cast<CharacteristicInfo *>(c);
                if (cst && cst->getUuidFull() == uuid)
                {
                    srv->askForWrite(uuid, value, type);
                    return;
                }
            }
        }
    }
}

QByteArray DeviceToolBLEx::askForData_qba(const QString &value, const QString &type)
{
    //qDebug() << "DeviceToolBLEx::askForData_qba(" << value << " / " << type << ")";

    QByteArray data;

    if (type.startsWith("uint"))
    {
        //qDebug() << "uINTEGER > " << value.toULongLong();
        if (type.startsWith("uint16")) {
            uint16_t u = value.toUShort();
            data.append(reinterpret_cast<const char*>(&u), 2);
        }
        else if (type.startsWith("uint32")) {
            uint32_t u = value.toUInt();
            data.append(reinterpret_cast<const char*>(&u), 4);
        }
        else if (type.startsWith("uint64")) {
            uint64_t u = value.toULongLong();
            data.append(reinterpret_cast<const char*>(&u), 8);
        }
    }
    else if (type.startsWith("int"))
    {
        //qDebug() << "sINTEGER > " << value.toInt();
        if (type.startsWith("int16")) {
            int16_t i = value.toShort();
            data.append(reinterpret_cast<const char*>(&i), 2);
        }
        else if (type.startsWith("int32")) {
            int32_t i = value.toInt();
            data.append(reinterpret_cast<const char*>(&i), 4);
        }
        else if (type.startsWith("int64")) {
            int64_t i = value.toLongLong();
            data.append(reinterpret_cast<const char*>(&i), 8);
        }
    }

    else if (type == "float32")
    {
        //qDebug() << "FLOAT > " << value.toFloat();
        float f = value.toFloat();
        data.append(reinterpret_cast<const char*>(&f), 4);
    }
    else if (type == "float64") {
        //qDebug() << "DOUBLE > " << value.toDouble();
        double d = value.toDouble();
        data.append(reinterpret_cast<const char*>(&d), 8);
    }

    else if (type == "data") {
        //qDebug() << "data > " << value.toLatin1();
        for (int i = 0; i < value.size(); i++)
        {
            data.append(value.at(i).toLatin1());
        }
    }
    else if (type == "ascii") {
        //qDebug() << "ascii > " << value.toLatin1().toHex();
        data = value.toLatin1().toHex();
    }

    return data;
}

QStringList DeviceToolBLEx::askForData_list(const QString &value, const QString &type)
{

    // Convert data to QByteArray
    QByteArray a = askForData_qba(value, type);

    // Make it compatible with the data widget for display
    QStringList out;
    if (type == "data")
    {
        for (int i = 0; i < a.size();)
        {
            QByteArray duo; duo += a.at(i++);
            if (i < a.size()) duo += a.at(i++);
            else duo += '0';
            out += duo;
        }
    }
    else if (type == "ascii")
    {
        for (int i = 0; i < a.size();)
        {
            QByteArray duo;
            duo += a.at(i++);
            duo += a.at(i++);
            out += duo;
        }
    }
    else
    {
        for (int i = 0; i < a.size();)
        {
            QByteArray duo;
            duo += a.at(i++);
            out += duo.toHex();
        }
    }

    //qDebug() << "DeviceToolBLEx::askForData_list(" << value << " / " << type << ")  >> " << out;
    return out;
}

/* ************************************************************************** */

void DeviceToolBLEx::deviceConnected()
{
    qDebug() << "DeviceToolBLEx::deviceConnected(" << m_deviceAddress << ")";

    if (m_ble_action == DeviceUtils::ACTION_SCAN ||
        m_ble_action == DeviceUtils::ACTION_SCAN_WITH_VALUES ||
        m_ble_action == DeviceUtils::ACTION_SCAN_WITHOUT_VALUES)
    {
        qDeleteAll(m_services);
        m_services.clear();
    }

    Device::deviceConnected();
}

/* ************************************************************************** */
/* ************************************************************************** */

void DeviceToolBLEx::addLowEnergyService(const QBluetoothUuid &uuid)
{
    qDebug() << "DeviceToolbox::addLowEnergyService(" << uuid.toString() << ")";

    QLowEnergyService *service = m_bleController->createServiceObject(uuid);
    if (!service)
    {
        qWarning() << "Cannot create service for UUID" << uuid;
        return;
    }

    QLowEnergyService::DiscoveryMode scanmode = QLowEnergyService::FullDiscovery;
    if (m_ble_action == DeviceUtils::ACTION_SCAN_WITHOUT_VALUES)
    {
        m_services_scanmode = 2; // incomplete scan
        scanmode = QLowEnergyService::SkipValueDiscovery;
    }
    else if (m_ble_action == DeviceUtils::ACTION_SCAN_WITH_VALUES)
    {
        m_services_scanmode = 3; // incomplete scan (with values)
        scanmode = QLowEnergyService::FullDiscovery;
    }

    auto serv = new ServiceInfo(service, scanmode, this);
    m_services.append(serv);

    Q_EMIT servicesChanged();
}

/* ************************************************************************** */

void DeviceToolBLEx::serviceScanDone()
{
    qDebug() << "DeviceToolbox::serviceScanDone(" << m_deviceAddress << ")";

    if (m_services_scanmode == 2) // "incomplete scan"
    {
        m_services_scanmode = 4; // now "scanned"
    }
    else if (m_services_scanmode == 3) // "incomplete scan (with values)"
    {
        m_services_scanmode = 5; // now "scanned (with values)"
    }
    Q_EMIT servicesChanged();

    // Stay connected
    m_ble_status = DeviceUtils::DEVICE_CONNECTED;
    Q_EMIT statusUpdated();
}

/* ************************************************************************** */
/* ************************************************************************** */

bool DeviceToolBLEx::parseAdvertisementToolBLEx(const uint16_t mode,
                                                const uint16_t id,
                                                const QBluetoothUuid &uuid,
                                                const QByteArray &data)
{
    bool hasNewData = false;

    // mode:
    // DeviceUtils::BLE_ADV_MANUFACTURERDATA
    // DeviceUtils::BLE_ADV_SERVICEDATA

    if (mode == DeviceUtils::BLE_ADV_MANUFACTURERDATA)
    {
        if (!m_mfd.isEmpty())
        {
            hasNewData = m_mfd.first()->compare(data);
            if (!hasNewData) return false;
        }

        AdvertisementData *a = new AdvertisementData(mode, id, data, this);
        m_advertisementData.push_front(a); // always add it to the unfiltered list

        bool uuidFound = false;
        for (auto uuu: m_mfd_uuid)
        {
            if (uuu->getUuid() == id)
            {
                uuidFound = true;

                if (uuu->getSelected())
                {
                    m_advertisementData_filtered.push_front(a);
                }
            }
        }
        if (!uuidFound)
        {
            AdvertisementUUID *uu = new AdvertisementUUID(id, true);
            m_mfd_uuid.push_back(uu);
            m_advertisementData_filtered.push_front(a);
        }

        m_mfd.push_front(a);
        if (m_mfd.length() >= s_max_entries_packets)
        {
            AdvertisementData *d = m_mfd.back();
            m_mfd.pop_back();
            m_advertisementData.removeOne(d);
            m_advertisementData_filtered.removeOne(d);
            delete d;
        }

        Q_EMIT advertisementChanged();

        mfdFilterUpdate();
    }
    else if (mode == DeviceUtils::BLE_ADV_SERVICEDATA)
    {
        if (!m_svd.isEmpty())
        {
            hasNewData = m_svd.first()->compare(data);
            if (!hasNewData) return false;
        }

        AdvertisementData *a = new AdvertisementData(mode, id, data, this);
        m_advertisementData.push_front(a); // always add it to the unfiltered list

        bool uuidFound = false;
        for (auto uuu: m_svd_uuid)
        {
            if (uuu->getUuid() == id)
            {
                uuidFound = true;

                if (uuu->getSelected())
                {
                    m_advertisementData_filtered.push_front(a);
                }
            }
        }
        if (!uuidFound)
        {
            AdvertisementUUID *uu = new AdvertisementUUID(id, true);
            m_svd_uuid.push_back(uu);
            m_advertisementData_filtered.push_front(a);
        }

        m_svd.push_front(a);
        if (m_svd.length() >= s_max_entries_packets)
        {
            AdvertisementData *d = m_svd.back();
            m_svd.pop_back();
            m_advertisementData.removeOne(d);
            m_advertisementData_filtered.removeOne(d);
            delete d;
        }

        Q_EMIT advertisementChanged();

        svdFilterUpdate();
    }

    if (m_mfd.length() > 0 || m_svd.length())
    {
        if (!m_hasAdvertisement)
        {
            m_hasAdvertisement = true;
            Q_EMIT advertisementChanged();
        }
    }

    return hasNewData;
}

/* ************************************************************************** */

void DeviceToolBLEx::addAdvertisementEntry(const int rssi, const bool hasMFD, const bool hasSVD)
{
    int maxentries = s_min_entries_advertisement;
    if (m_advertisementInterval > 0 && m_advertisementInterval < 1000) maxentries = s_max_entries_advertisement;

    m_advertisementEntries.push_back(new AdvertisementEntry(rssi, hasMFD, hasSVD, this));
    if (m_advertisementEntries.length() > maxentries)
    {
        delete m_advertisementEntries.at(0);
        m_advertisementEntries.pop_front();
    }

    if (m_advertisementEntries.length() > 1)
    {
        int intvm = 0;
        for (int i=1; i < m_advertisementEntries.length(); i++)
        {
            //qDebug() << i << m_rssiHistory.at(i)->getTimestamp();
            intvm += m_advertisementEntries.at(i-1)->getTimestamp().msecsTo(m_advertisementEntries.at(i)->getTimestamp());
        }
        m_advertisementInterval = (intvm / (m_advertisementEntries.length()-1.0));
    }

    Q_EMIT rssiUpdated();
}

void DeviceToolBLEx::cleanAdvertisementEntries()
{
    qDeleteAll(m_advertisementEntries);
    m_advertisementEntries.clear();
}

/* ************************************************************************** */

void DeviceToolBLEx::setAdvertisedServices(const QList <QBluetoothUuid> &services)
{
    if (services.size() != m_advertised_services.size())
    {
        m_advertised_services.clear();
        for (auto u: services)
        {
            m_advertised_services.push_back(u.toString(QUuid::WithBraces).toUpper());
        }
        Q_EMIT servicesAdvertisedChanged();
    }
}

/* ************************************************************************** */

void DeviceToolBLEx::mfdFilterUpdate()
{
    QList <uint16_t> accepted_uuids;
    for (auto u: m_mfd_uuid)
    {
        if (u->getSelected())
        {
            accepted_uuids.push_back(u->getUuid());
        }
    }

    for (auto adv: m_advertisementData_filtered)
    {
        if (!accepted_uuids.contains(adv->getUUID_int()))
        {
            m_advertisementData_filtered.removeOne(adv);
        }
    }

    Q_EMIT advertisementFilteredChanged();
}

void DeviceToolBLEx::svdFilterUpdate()
{
    QList <uint16_t> accepted_uuids;
    for (auto u: m_svd_uuid)
    {
        if (u->getSelected())
        {
            accepted_uuids.push_back(u->getUuid());
        }
    }

    for (auto adv: m_advertisementData_filtered)
    {
        if (!accepted_uuids.contains(adv->getUUID_int()))
        {
            m_advertisementData_filtered.removeOne(adv);
        }
    }

    Q_EMIT advertisementFilteredChanged();
}

bool comparefunc(AdvertisementData *c1, AdvertisementData *c2)
{
    return c1->getTimestamp() > c2->getTimestamp();
}

void DeviceToolBLEx::advertisementFilterUpdate()
{
    QList <uint16_t> accepted_uuids;
    for (auto u: m_svd_uuid)
    {
        if (u->getSelected())
        {
            accepted_uuids.push_back(u->getUuid());
        }
    }
    for (auto u: m_mfd_uuid)
    {
        if (u->getSelected())
        {
            accepted_uuids.push_back(u->getUuid());
        }
    }

    m_advertisementData_filtered.clear();
    for (auto adv: m_svd)
    {
        if (accepted_uuids.contains(adv->getUUID_int()))
        {
            m_advertisementData_filtered.push_front(adv);
        }
    }
    for (auto adv: m_mfd)
    {
        if (accepted_uuids.contains(adv->getUUID_int()))
        {
            m_advertisementData_filtered.push_front(adv);
        }
    }

    std::sort(m_advertisementData_filtered.begin(), m_advertisementData_filtered.end(), comparefunc);

    Q_EMIT advertisementFilteredChanged();
}

/* ************************************************************************** */

bool DeviceToolBLEx::hasServiceCache() const
{
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    cachePath += "/devices/" + m_deviceAddress + ".cache";

    return QFile::exists(cachePath);
}

bool DeviceToolBLEx::saveServiceCache()
{
    bool status = false;

    // Services
    QJsonArray servicesArray;
    for (auto s: m_services)
    {
        ServiceInfo *srv = qobject_cast<ServiceInfo *>(s);
        if (srv)
        {
            // Characteristics
            QJsonArray characteristicsArray;
            for (auto c: srv->getCharacteristicsInfos())
            {
                CharacteristicInfo *cst = qobject_cast<CharacteristicInfo *>(c);
                if (cst)
                {
                    QJsonObject characteristicObject;
                    characteristicObject.insert("name", QJsonValue::fromVariant(cst->getName()));
                    characteristicObject.insert("uuid", QJsonValue::fromVariant(cst->getUuidFull()));
                    characteristicObject.insert("properties", QJsonValue::fromVariant(cst->getPropertyList()));

                    characteristicsArray.append(characteristicObject);
                }
            }

            QJsonObject serviceObject;
            serviceObject.insert("name", QJsonValue::fromVariant(srv->getName()));
            serviceObject.insert("uuid", QJsonValue::fromVariant(srv->getUuidFull()));
            serviceObject.insert("type", QJsonValue::fromVariant(srv->getTypeList()));
            serviceObject.insert("characteristics", characteristicsArray);

            servicesArray.append(serviceObject);
        }
    }

    QJsonObject root;
    root.insert("address", QJsonValue::fromVariant(getAddress()));
    root.insert("services", servicesArray);

    // Get cache directory path
    QString cacheDirectoryPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/devices";
    QDir cacheDirectory(cacheDirectoryPath);
    if (!cacheDirectory.exists())
    {
        cacheDirectory.mkpath(cacheDirectoryPath);
    }

    qDebug() << "CACHE DIRECTORY" << cacheDirectory;

    if (cacheDirectory.exists())
    {
        // Finish preping cache path
        QString cacheFilePath = "/" + m_deviceAddress + ".cache";
        cacheFilePath = cacheDirectoryPath + cacheFilePath;

        // Open file and save content
        QFile efile(cacheFilePath);
        if (efile.open(QFile::WriteOnly | QIODevice::Text))
        {
            QJsonDocument cacheJsonDoc(root);
            efile.write(cacheJsonDoc.toJson());
            efile.close();

            status = true;
        }
    }

    return status;
}

void DeviceToolBLEx::restoreServiceCache()
{
    QString cacheDirectoryPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    cacheDirectoryPath += "/devices/" + m_deviceAddress + ".cache";

    QFile file(cacheDirectoryPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QJsonDocument cacheJsonDoc = QJsonDocument().fromJson(file.readAll());
        QJsonObject root = cacheJsonDoc.object();
        file.close();

        qDeleteAll(m_services);
        m_services.clear();
        m_services_scanmode = 1; // cache

        QJsonArray servicesArray = root["services"].toArray();
        for (const auto &srv: servicesArray)
        {
            QJsonObject obj = srv.toObject();
            qDebug() << "+ SERVICE >" << obj["name"].toString() << obj["uuid"].toString();

            auto serv = new ServiceInfo(obj, this);
            m_services.append(serv);
        }

        Q_EMIT servicesChanged();
    }
}

/* ************************************************************************** */

QString DeviceToolBLEx::getExportPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/toolBLEx";
}

bool DeviceToolBLEx::exportDeviceInfo(bool withAdvertisements, bool withServices, bool withValues)
{
    bool status = false;

    QString txt;
    QString endl = QChar('\n');

    txt += "Device Name: " + m_deviceName + endl;
    if (hasAddressMAC()) txt += "Device MAC: " + m_deviceAddress + endl;
    if (m_deviceManufacturer.length() > 0) txt += "Device MAC manufacturer: " + m_deviceManufacturer + endl;
    txt += endl;

    // Advertisements
    if (withAdvertisements)
    {
        txt += "Advertising interval: " + QString::number(m_advertisementInterval) + "ms" + endl;

        if (m_advertisementData.size() == 0)
        {
            txt += "> No advertisement packets." + endl;
            txt += endl;
        }
        else
        {
            txt += "Advertisement packets:" + endl;

            for (auto adv: m_advertisementData)
            {
                if (adv->getMode() == DeviceUtils::BLE_ADV_MANUFACTURERDATA)
                {
                    txt += "> MFD > ";
                    txt += adv->getTimestamp().toString("hh:mm:ss.zzz") + " > ";
                    txt += "0x" + adv->getUUID_str() + " (" + adv->getUUID_vendor() + ")" + " > ";
                    txt += "(" + QString::number(adv->getDataSize()).rightJustified(3, ' ') + " bytes) ";
                    txt += "0x" + adv->getDataHex();
                }
                else if (adv->getMode() == DeviceUtils::BLE_ADV_SERVICEDATA)
                {
                    txt += "> SVD > ";
                    txt += adv->getTimestamp().toString("hh:mm:ss.zzz") + " > ";
                    txt += "0x" + adv->getUUID_str() + " > ";
                    txt += "(" + QString::number(adv->getDataSize()).rightJustified(3, ' ') + " bytes) ";
                    txt += "0x" + adv->getDataHex();
                }
                else
                {
                    txt += "> ??? > ";
                }

                txt += endl;
            }

            txt += endl;
        }
    }

    // Services
    if (withServices)
    {
        for (auto s: m_services)
        {
            ServiceInfo *srv = qobject_cast<ServiceInfo *>(s);
            if (srv)
            {
                txt += "Service Name: \"" + srv->getName() + "\"" + endl;
                txt += "Service UUID: " + srv->getUuidFull() + endl;

                for (auto c: srv->getCharacteristicsInfos())
                {
                    CharacteristicInfo *cst = qobject_cast<CharacteristicInfo *>(c);
                    if (cst)
                    {
                        txt += "Characteristic Name: " + cst->getName();
                        txt += " - UUID: " + cst->getUuidFull();
                        txt += " - Properties: " + cst->getProperty();
                        //exp += " - Handle: " + cst->getHandle();

                        if (withValues)
                        {
                            if (cst->getValue() == "<none>")
                                txt += " - Value: <none>";
                            else
                                txt += " - Value: 0x" + cst->getValueHex();
                        }

                        txt += endl;
                    }
                }

                txt += endl;
            }
        }
    }

    // DEBUG
    //qDebug() << "DeviceToolBLEx::exportDeviceInfo()" << txt;

    // Get home directory path
    QString exportDirectoryPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/toolBLEx";
    QDir exportDirectory(exportDirectoryPath);
    if (!exportDirectory.exists())
    {
        exportDirectory.mkpath(exportDirectoryPath);
    }

    if (exportDirectory.exists())
    {
        // Finish preping export path
        QString exportFilePath = "/" + m_deviceName + ".txt";
        exportFilePath.replace(" ", "_");
        exportFilePath = exportDirectoryPath + exportFilePath;

        // Open file and save content
        QFile efile(exportFilePath);
        if (efile.open(QFile::WriteOnly | QIODevice::Text))
        {
            QTextStream eout(&efile);
            eout.setEncoding(QStringConverter::Utf8);
            eout << txt;

            status = true;
            efile.close();
        }
    }

    return status;
}

/* ************************************************************************** */