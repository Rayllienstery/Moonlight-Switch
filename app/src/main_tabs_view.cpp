//
//  main_tabs_view.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#include "main_tabs_view.hpp"
#include "Settings.hpp"

#include <string>
#include "about_tab.hpp"
#include "add_host_tab.hpp"
#include "host_tab.hpp"
#include "raylees_tweaks_tab.hpp"
#include "settings_tab.hpp"

MainTabs::MainTabs() {
    favoriteTab = new FavoriteTab();
    favoriteTab->ptrLock();

    MainTabs::instanse = this;
    refillTabs();
    lastHasAnyFavorites = Settings::instance().has_any_favorite();
}

void MainTabs::willAppear(bool resetState) {
    Box::willAppear(resetState);
    getAppletFrameItem()->title = std::string("Moonlight ") + APP_VERSION;
    updateAppletFrameItem();
    updateFavoritesIfNeeded();
    favoriteTab->refreshIfNeeded();
}

void MainTabs::updateFavoritesIfNeeded() {
    if (lastHasAnyFavorites != Settings::instance().has_any_favorite()) {
        refillTabs(false);
    }
}

void MainTabs::refillTabs(bool keepFocus) {
    int newFocus = 0;

    if (keepFocus) {
        for (int i = 0; i < this->sidebar->getItemsSize(); i++) {
            auto item = dynamic_cast<SidebarItem*>(this->sidebar->getItem(i));
            if (item && item->isActive())
                newFocus = i;
        }
    }

    clearTabs();

    bool hasAnyFavorite = Settings::instance().has_any_favorite();
    if (hasAnyFavorite) {
        addTab("tabs/favorites"_i18n, [this] { return this->favoriteTab; });
        addSeparator();
    }
    lastHasAnyFavorites = hasAnyFavorite;

    auto hosts = Settings::instance().hosts();
    for (const Host& host : hosts) {
        const std::string address = host.preferred_address();
        addTab(host.hostname, [address] {
            Host current;
            current.address = address;
            for (const Host& saved : Settings::instance().hosts()) {
                if (saved.preferred_address() == address || saved.has_address(address)) {
                    current = saved;
                    break;
                }
            }
            return new HostTab(current);
        });
    }
    if (!hosts.empty())
        addSeparator();

    addTab("tabs/add_host"_i18n, AddHostTab::create);
    addTab("tabs/settings"_i18n, SettingsTab::create);
    addTab("tabs/raylees_tweaks"_i18n, RayleesTweaksTab::create);
    addSeparator();
    addTab("tabs/about"_i18n, AboutTab::create);
    sidebar->setContentOffsetY(-40, false);
    focusTab(newFocus);
}

View* MainTabs::create() { return new MainTabs(); }
