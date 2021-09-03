/*
 * Copyright (C) 2011 ~ 2021 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *             kirigaya <kirigaya@mkacg.com>
 *             Hualet <mr.asianwang@gmail.com>
 *
 * Maintainer: sbw <sbw@sbw.so>
 *             kirigaya <kirigaya@mkacg.com>
 *             Hualet <mr.asianwang@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "networkmodulewidget.h"
#include "pppoepage.h"
#include "window/utils.h"
#include "window/gsettingwatcher.h"
#include "widgets/nextpagewidget.h"
#include "widgets/settingsgroup.h"
#include "widgets/switchwidget.h"
#include "widgets/multiselectlistview.h"

#include <DStyleOption>

#include <QDebug>
#include <QPointer>
#include <QVBoxLayout>
#include <QPushButton>
#include <QProcess>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QGSettings>

#include <interface/frameproxyinterface.h>

#include <wireddevice.h>
#include <wirelessdevice.h>
#include <networkdevicebase.h>
#include <networkcontroller.h>
#include <proxycontroller.h>
#include <networkconst.h>
#include <hotspotcontroller.h>

#include <widgets/contentwidget.h>

using namespace dcc::widgets;
using namespace DCC_NAMESPACE;
using namespace dde::network;

static const int SectionRole = Dtk::UserRole + 1;
static const int DeviceRole = Dtk::UserRole + 2;
static const int SearchPath = Dtk::UserRole + 3;

NetworkModuleWidget::NetworkModuleWidget(QWidget *parent)
    : QWidget(parent)
    , m_lvnmpages(new dcc::widgets::MultiSelectListView(this))
    , m_modelpages(new QStandardItemModel(this))
    , m_nmConnectionEditorProcess(nullptr)
    , m_settings(new QGSettings("com.deepin.dde.control-center", QByteArray(), this))
{
    setObjectName("Network");
    m_lvnmpages->setAccessibleName("List_networkmenulist");
    m_lvnmpages->setFrameShape(QFrame::NoFrame);
    m_lvnmpages->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_lvnmpages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lvnmpages->setModel(m_modelpages);
    m_lvnmpages->setViewportMargins(ScrollAreaMargins);
    m_lvnmpages->setIconSize(ListViweIconSize);

    m_centralLayout = new QVBoxLayout();
    setMinimumWidth(250);
    m_centralLayout->setMargin(0);
    setLayout(m_centralLayout);

    DStandardItem *pppIt = new DStandardItem(tr("DSL"));
    pppIt->setData(QVariant::fromValue(PageType::DSLPage), SectionRole);
    pppIt->setIcon(QIcon::fromTheme("dcc_dsl"));
    m_modelpages->appendRow(pppIt);
    GSettingWatcher::instance()->bind("networkDsl", m_lvnmpages, pppIt);

    //~ contents_path /VPN
    DStandardItem *vpnit = new DStandardItem(tr("VPN"));
    vpnit->setData(QVariant::fromValue(PageType::VPNPage), SectionRole);
    vpnit->setIcon(QIcon::fromTheme("dcc_vpn"));
    m_modelpages->appendRow(vpnit);
    GSettingWatcher::instance()->bind("networkVpn", m_lvnmpages, vpnit);

    //~ contents_path /network/System Proxy
    DStandardItem *prxyit = new DStandardItem(tr("System Proxy"));
    prxyit->setData(QVariant::fromValue(PageType::SysProxyPage), SectionRole);
    prxyit->setIcon(QIcon::fromTheme("dcc_system_agent"));
    m_modelpages->appendRow(prxyit);
    GSettingWatcher::instance()->bind("systemProxy", m_lvnmpages, prxyit);

    //~ contents_path /network/Application Proxy
    DStandardItem *aprxit = new DStandardItem(tr("Application Proxy"));
    aprxit->setData(QVariant::fromValue(PageType::AppProxyPage), SectionRole);
    aprxit->setIcon(QIcon::fromTheme("dcc_app_proxy"));
    m_modelpages->appendRow(aprxit);
    GSettingWatcher::instance()->bind("applicationProxy", m_lvnmpages, aprxit);

    //~ contents_path /network/Network Details
    DStandardItem *infoit = new DStandardItem(tr("Network Details"));
    infoit->setData(QVariant::fromValue(PageType::NetworkInfoPage), SectionRole);
    infoit->setIcon(QIcon::fromTheme("dcc_network"));
    m_modelpages->appendRow(infoit);
    GSettingWatcher::instance()->bind("networkDetails", m_lvnmpages, infoit);

    m_centralLayout->addWidget(m_lvnmpages);
    if (IsServerSystem)
        handleNMEditor();

    connect(m_lvnmpages, &DListView::activated, this, &NetworkModuleWidget::onClickCurrentListIndex);
    connect(m_lvnmpages, &DListView::clicked, m_lvnmpages, &DListView::activated);

    connect(GSettingWatcher::instance(), &GSettingWatcher::requestUpdateSecondMenu, this, &NetworkModuleWidget::updateSecondMenu);

    NetworkController *pNetworkController = NetworkController::instance();
    connect(pNetworkController, &NetworkController::deviceRemoved, this, &NetworkModuleWidget::onDeviceChanged);
    connect(pNetworkController, &NetworkController::deviceAdded, this, &NetworkModuleWidget::onDeviceChanged);

    ProxyController *proxyController = pNetworkController->proxyController();
    connect(proxyController, &ProxyController::proxyMethodChanged, this, &NetworkModuleWidget::onProxyMethodChanged);

    onDeviceChanged();

    connect(m_settings, &QGSettings::changed, this, [ = ](const QString & key) {
        if (key == "networkWired" || key == "networkWireless") {
            for (int i = 0; i < m_modelpages->rowCount(); i++) {
                if (m_modelpages->index(i, 0).data(SectionRole).value<PageType>() == PageType::WiredPage) {
                    bool status = m_settings->get("networkWired").toBool();
                    m_lvnmpages->setRowHidden(i, !status);
                    if (!status)
                        updateSecondMenu(i);
                } else if (m_modelpages->index(i, 0).data(SectionRole).value<PageType>() == PageType::WirelessPage) {
                    bool status = m_settings->get("networkWireless").toBool();
                    m_lvnmpages->setRowHidden(i, !status);
                    if (!status)
                        updateSecondMenu(i);
                }
            }
        }
    });
}

NetworkModuleWidget::~NetworkModuleWidget()
{
    if (m_nmConnectionEditorProcess) {
        m_nmConnectionEditorProcess->close();
        m_nmConnectionEditorProcess->deleteLater();
        m_nmConnectionEditorProcess = nullptr;
    }
}

void NetworkModuleWidget::onClickCurrentListIndex(const QModelIndex &idx)
{
    const QString searchPath = idx.data(SearchPath).toString();
    m_modelpages->itemFromIndex(idx)->setData("", SearchPath);
    if (m_lastIndex == idx && searchPath.isEmpty())
        return;

    PageType type = idx.data(SectionRole).value<PageType>();
    m_lastIndex = idx;
    m_lvnmpages->setCurrentIndex(idx);
    switch (type) {
    case PageType::DSLPage:
        Q_EMIT requestShowPppPage(searchPath);
        break;
    case PageType::VPNPage:
        Q_EMIT requestShowVpnPage(searchPath);
        break;
    case PageType::SysProxyPage:
        Q_EMIT requestShowProxyPage();
        break;
    case PageType::AppProxyPage:
        Q_EMIT requestShowChainsPage();
        break;
    case PageType::HotspotPage:
        Q_EMIT requestHotspotPage();
        break;
    case PageType::NetworkInfoPage:
        Q_EMIT requestShowInfomation();
        break;
    case PageType::WiredPage:
    case PageType::WirelessPage:
        Q_EMIT requestShowDeviceDetail(idx.data(DeviceRole).value<NetworkDeviceBase *>(), searchPath);
        break;
    default:
        break;
    }

    m_lvnmpages->resetStatus(idx);
}

void NetworkModuleWidget::onProxyMethodChanged(const ProxyMethod &method)
{
    if (method == ProxyMethod::Init) return;
    QPointer<DViewItemAction> action(new DViewItemAction(Qt::AlignmentFlag::AlignRight | Qt::AlignmentFlag::AlignVCenter));
    if (action.isNull()) return;
    //遍历获取系统代理项,设置状态
    for (int i = 0; i < m_modelpages->rowCount(); i++) {
        DStandardItem *item = dynamic_cast<DStandardItem *>(m_modelpages->item(i));
        if (!item)
            continue;

        if (item->data(SectionRole).value<PageType>() == PageType::SysProxyPage) {
            item->setActionList(Qt::Edge::RightEdge, {action});
            if (method == ProxyMethod::None)
                action->setText(tr("Disabled"));
            else if (method == ProxyMethod::Manual)
                action->setText(tr("Manual"));
            else if (method == ProxyMethod::Auto)
                action->setText(tr("Auto"));
            else
                action->setText(tr("Disabled"));

            break;
        }
    }
}

bool NetworkModuleWidget::handleNMEditor()
{
    QProcess *process = new QProcess(this);
    QPushButton *nmConnEditBtn = new QPushButton(tr("Configure by Network Manager"));
    m_centralLayout->addWidget(nmConnEditBtn);
    nmConnEditBtn->hide();
    process->start("which nm-connection-editor");

    connect(process, static_cast<void (QProcess::*)(int)>(&QProcess::finished), this, [ = ] {
        QString networkManageOutput = process->readAll();
        if (!networkManageOutput.isEmpty()) {
            nmConnEditBtn->show();
            connect(nmConnEditBtn, &QPushButton::clicked, this, [ = ] {
                if (!m_nmConnectionEditorProcess) {
                    m_nmConnectionEditorProcess = new QProcess(this);
                }
                m_nmConnectionEditorProcess->start("nm-connection-editor");
            });
        }
        process->deleteLater();
    });

    return true;
}

void NetworkModuleWidget::updateSecondMenu(int row)
{
    bool isAllHidden = true;
    for (int i = 0; i < m_modelpages->rowCount(); i++) {
        if (!m_lvnmpages->isRowHidden(i)) {
            isAllHidden = false;
            break;
        }
    }

    if (m_lvnmpages->selectionModel()->selectedRows().size() > 0) {
        int index = m_lvnmpages->selectionModel()->selectedRows()[0].row();
        Q_EMIT requestUpdateSecondMenu(index == row);
    } else {
        Q_EMIT requestUpdateSecondMenu(false);
    }

    if (isAllHidden) {
        m_lastIndex = QModelIndex();
        m_lvnmpages->clearSelection();
    }
}

void NetworkModuleWidget::initSetting(const int settingIndex, const QString &searchPath)
{
    if (!searchPath.isEmpty()) {
        m_modelpages->itemFromIndex(m_modelpages->index(settingIndex, 0))->setData(searchPath, SearchPath);
    }
    m_lvnmpages->setCurrentIndex(m_modelpages->index(settingIndex, 0));
    m_lvnmpages->clicked(m_modelpages->index(settingIndex, 0));
}

void NetworkModuleWidget::showDefaultWidget()
{
    for (int i = 0; i < m_modelpages->rowCount(); i++) {
        if (!m_lvnmpages->isRowHidden(i)) {
            m_lvnmpages->activated(m_modelpages->index(i, 0));
            break;
        }
    }
}

void NetworkModuleWidget::setCurrentIndex(const int settingIndex)
{
    // 设置网络列表当前索引
    m_lvnmpages->setCurrentIndex(m_modelpages->index(settingIndex, 0));
}

void NetworkModuleWidget::setIndexFromPath(const QString &path)
{
    for (int i = 0; i < m_modelpages->rowCount(); ++i) {
        QString indexPath = m_modelpages->item(i)->data(DeviceRole).value<NetworkDeviceBase *>()->path();
        if (indexPath == path) {
            m_lvnmpages->setCurrentIndex(m_modelpages->index(i, 0));
            return;
        }
    }
}

int NetworkModuleWidget::gotoSetting(const QString &path)
{
    PageType type = PageType::NonePage;
    if (path == QStringLiteral("Network Details")) {
        type = PageType::NetworkInfoPage;
    } else if (path == QStringLiteral("Application Proxy")) {
        type = PageType::AppProxyPage;
    } else if (path == QStringLiteral("System Proxy")) {
        type = PageType::SysProxyPage;
    } else if (path == QStringLiteral("VPN")) {
        type = PageType::VPNPage;
    } else if (path == QStringLiteral("DSL")) {
        type = PageType::DSLPage;
    } else if (path.contains("Wireless Network")) {
        type = PageType::WirelessPage;
    } else if (path.contains("Wired Network")) {
        type = PageType::WiredPage;
    } else if (path == QStringLiteral("Personal Hotspot")) {
        type = PageType::HotspotPage;
    }
    int index = -1;
    for (int i = 0; i < m_modelpages->rowCount(); ++i) {
        if (m_modelpages->item(i)->data(SectionRole).value<PageType>() == type) {
            index = i;
            break;
        }
    }

    return index;
}

void NetworkModuleWidget::onDeviceChanged()
{
    QList<NetworkDeviceBase *> devices = NetworkController::instance()->devices();
    QModelIndex currentIndex = m_lvnmpages->currentIndex();
    NetworkDeviceBase *currentDevice = currentIndex.data(DeviceRole).value<NetworkDeviceBase *>();
    bool moveFirst = !devices.contains(currentDevice);

    for (int i = m_modelpages->rowCount() - 1; i >= 0; i--) {
        QStandardItem *item = m_modelpages->item(i);
        PageType itemPageType = item->data(SectionRole).value<PageType>();
        if (itemPageType != PageType::WiredPage && itemPageType != PageType::WirelessPage
                && itemPageType != PageType::HotspotPage)
            continue;

        m_modelpages->removeRow(i);
    }

    bool supportHotspot = false;
    for (NetworkDeviceBase *device : devices) {
        if (device->supportHotspot()) {
            supportHotspot = true;
            break;
        }
    }
    if (!currentDevice) {
        // 是否选中的当前设备
        PageType pageType = currentIndex.data(SectionRole).value<PageType>();
        if (pageType == PageType::HotspotPage && !supportHotspot)
            moveFirst = true;
    }

    for (int i = 0; i < devices.size(); i++) {
        NetworkDeviceBase *device = devices[i];
        DStandardItem *deviceItem = new DStandardItem(device->deviceName());
        deviceItem->setData(QVariant::fromValue(device->deviceType() == DeviceType::Wireless ? PageType::WirelessPage : PageType::WiredPage), SectionRole);
        deviceItem->setIcon(QIcon::fromTheme(device->deviceType() == DeviceType::Wireless ? "dcc_wifi" : "dcc_ethernet"));
        deviceItem->setData(QVariant::fromValue(device), DeviceRole);

        QPointer<DViewItemAction> dummyStatus(new DViewItemAction(Qt::AlignmentFlag::AlignRight | Qt::AlignmentFlag::AlignVCenter));
        deviceItem->setActionList(Qt::Edge::RightEdge, { dummyStatus });

        if (!dummyStatus.isNull()) {
            if (device->isEnabled())
                dummyStatus->setText(device->property("statusName").toString());
            else
                dummyStatus->setText(tr("Disabled"));

            m_lvnmpages->update();
        }

        connect(device, &NetworkDeviceBase::enableChanged, this, [this, device, dummyStatus](const bool enabled) {
            if (!dummyStatus.isNull()) {
                QString txt = enabled ? device->property("statusName").toString() : tr("Disabled");
                dummyStatus->setText(txt);
            }
            this->m_lvnmpages->update();
        });

        connect(device, &NetworkDeviceBase::deviceStatusChanged, this, [ this, device, dummyStatus ]() {
            if (!dummyStatus.isNull()) {
                if (device->isEnabled()) {
                    dummyStatus->setText(device->property("statusName").toString());
                } else {
                    dummyStatus->setText(tr("Disabled"));
                }
            }
            this->m_lvnmpages->update();
        });

        if (device->deviceType() == DeviceType::Wireless) {
            WirelessDevice *wirelssDev = qobject_cast<WirelessDevice *>(device);
            if (wirelssDev) {
                HotspotController *hotspotController = NetworkController::instance()->hotspotController();
                if (wirelssDev->supportHotspot() && hotspotController->supportHotspot()) {  //热点相关还未实现
                    if (!dummyStatus.isNull()) {
                        dummyStatus->setText(tr("Disconnected")); //热点未实现，暂时屏蔽
                        m_lvnmpages->update();
                    }
                }
                connect(wirelssDev, &WirelessDevice::hotspotEnableChanged, this, [this, dummyStatus](const bool enabled) {
                    if (enabled && !dummyStatus.isNull()) {
                        dummyStatus->setText(tr("Disconnected"));
                        m_lvnmpages->update();
                    }
                });
            }
        }
        m_modelpages->insertRow(i, deviceItem);
    }

    if (supportHotspot) {
        DStandardItem *hotspotit = new DStandardItem(tr("Personal Hotspot"));
        hotspotit->setData(QVariant::fromValue(PageType::HotspotPage), SectionRole);
        hotspotit->setIcon(QIcon::fromTheme("dcc_hotspot"));
        m_modelpages->insertRow(m_modelpages->rowCount() - 1, hotspotit);
        GSettingWatcher::instance()->bind("personalHotspot", m_lvnmpages, hotspotit);
    }
    if (moveFirst)
        setCurrentIndex(0);
}