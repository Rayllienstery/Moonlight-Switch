//
//  host_tab.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#include "host_tab.hpp"
#include "GameStreamClient.hpp"
#include "app_list_view.hpp"
#include "helper.hpp"
#include "main_tabs_view.hpp"

using namespace brls::literals;

namespace {
std::string host_subtitle(const Host& host) {
    std::string subtitle = host.address;
    if (!host.remoteAddress.empty() && host.remoteAddress != host.address) {
        subtitle = subtitle.empty() ? host.remoteAddress
                                    : subtitle + " | " + host.remoteAddress;
    }
    return subtitle;
}

// The sidebar builds the host tab from a snapshot taken at launch. A MAC
// learned later is written to settings, but that snapshot stays empty, so
// Wake refuses before a packet is sent.
void apply_saved_mac(Host& host) {
    if (!normalized_mac_address(host.mac).empty())
        return;

    for (const Host& saved : Settings::instance().hosts()) {
        if (!hosts_match(saved, host))
            continue;
        if (const std::string mac = normalized_mac_address(saved.mac); !mac.empty())
            host.mac = mac;
        return;
    }
}
}

HostTab::HostTab(const Host& host) : host(host) {
    apply_saved_mac(this->host);

    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/host.xml");

    remove->setText("common/remove"_i18n);
    remove->title->setTextColor(RGB(229, 57, 53));

    reloadHost();

    registerAction("host/rename"_i18n, ControllerButton::BUTTON_START,
                   [this](View* view) {
                       std::string title = this->host.hostname;
                       Application::getPlatform()->getImeManager()->openForText(
                               [this](const std::string& text) {
                                   this->host.hostname = text;
                                   Settings::instance().add_host(this->host);
                                   MainTabs::getInstanse()->refillTabs();
                               },
                               "host/rename_title"_i18n, "", 60, title, 0);

                       return true;
                   });

    connect->registerClickAction([this](View* view) {
        switch (state) {
        case AVAILABLE:
            this->present(new AppListView(this->host));
            break;
        case UNAVAILABLE:
            apply_saved_mac(this->host);
            if (GameStreamClient::can_wake_up_host(this->host)) {
                const auto wakeRequestId = ++this->wakeRequestGeneration;
                this->canceledWakeRequestGeneration = 0;

                Dialog* loader =
                    createLoadingDialog("host/wake_up_message"_i18n,
                                        [this, wakeRequestId] {
                                            if (this->wakeRequestGeneration ==
                                                wakeRequestId) {
                                                this->canceledWakeRequestGeneration =
                                                    wakeRequestId;
                                            }
                                        });
                loader->open();

                ASYNC_RETAIN
                GameStreamClient::wake_up_host(
                    this->host,
                    [ASYNC_TOKEN, loader, wakeRequestId](
                        const GSResult<bool>& result) {
                        ASYNC_RELEASE

                        if (wakeRequestId != this->wakeRequestGeneration) {
                            return;
                        }

                        if (this->canceledWakeRequestGeneration ==
                            wakeRequestId) {
                            return;
                        }

                        loader->close([this, result, wakeRequestId] {
                            if (wakeRequestId != this->wakeRequestGeneration) {
                                return;
                            }

                            if (result.isSuccess()) {
                                reloadHost();
                            } else {
                                showError("host/wake_up_error"_i18n);
                            }
                        });
                    });
            } else {
                showError("host/wake_up_no_mac"_i18n);
            }
            break;
        case FETCHING:
            break;
        }
        return true;
    });

    remove->registerClickAction([host](View* view) {
        auto* dialog = new Dialog("host/remove_message"_i18n);
        dialog->addButton("common/cancel"_i18n, [] {});
        dialog->addButton("common/remove"_i18n, [host] {
            Settings::instance().remove_host(host);
            MainTabs::getInstanse()->refillTabs();
        });
        dialog->open();

        return true;
    });
}

void HostTab::reloadHost() {
    state = FETCHING;
    header->setTitle("host/status"_i18n + ": " + "host/fetching"_i18n);
    header->setSubtitle(host_subtitle(host));
    connect->setText("host/wait"_i18n);

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN](const GSResult<SERVER_DATA>& result) {
            ASYNC_RELEASE

            if (result.isSuccess()) {
                if (const std::string mac = normalized_mac_address(result.value().mac);
                    !mac.empty())
                    this->host.mac = mac;
                const auto connectedAddress =
                    GameStreamClient::instance().active_address(this->host);
                header->setTitle("host/status"_i18n + ": " + "host/ready"_i18n);
                header->setSubtitle(connectedAddress.empty()
                                        ? host_subtitle(this->host)
                                        : connectedAddress);
                connect->setText("host/connect"_i18n);
                state = AVAILABLE;
            } else {
                header->setTitle("host/status"_i18n + ": " +
                                 "host/unable"_i18n);
                connect->setText("host/wake_up"_i18n);
                state = UNAVAILABLE;
            }
        });
}
