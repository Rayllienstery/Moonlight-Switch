//
//  raylees_tweaks_tab.cpp
//  Moonlight
//

#include "raylees_tweaks_tab.hpp"
#include "Settings.hpp"

using namespace brls::literals;

RayleesTweaksTab::RayleesTweaksTab() {
    this->inflateFromXMLRes("xml/tabs/raylees_tweaks.xml");
}

RayleesTweaksTab::~RayleesTweaksTab() { Settings::instance().save(); }

brls::View* RayleesTweaksTab::create() { return new RayleesTweaksTab(); }
