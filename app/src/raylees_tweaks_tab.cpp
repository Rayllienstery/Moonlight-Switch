//
//  raylees_tweaks_tab.cpp
//  Moonlight
//

#include "raylees_tweaks_tab.hpp"
#include "Settings.hpp"
#include "link_test_view.hpp"

#include <algorithm>

using namespace brls;
using namespace brls::literals;

RayleesTweaksTab::RayleesTweaksTab() {
    this->inflateFromXMLRes("xml/tabs/raylees_tweaks.xml");

    const auto hosts = Settings::instance().hosts();
    linkTest->setText("Link test");
    if (hosts.empty()) {
        linkHost->setVisibility(Visibility::GONE);
        linkTest->setDetailText("Add a host first");
        return;
    }

    std::vector<std::string> names;
    names.reserve(hosts.size());
    for (const Host& host : hosts)
        names.push_back(host.hostname.empty() ? host.preferred_address() : host.hostname);
    linkHost->setText("Host");
    linkHost->setData(names);
    linkHost->setSelection(0);
    linkHost->getEvent()->subscribe([this](int selected) { hostIndex = selected; });
    linkTest->setDetailText("Live graphs");
    linkTest->registerClickAction([this](View*) {
        const auto currentHosts = Settings::instance().hosts();
        if (currentHosts.empty())
            return true;
        const int index = std::clamp(hostIndex, 0, static_cast<int>(currentHosts.size()) - 1);
        auto* view = new LinkTestView(currentHosts[index]);
        view->getAppletFrameItem()->title = "Link test";
        auto* frame = new AppletFrame(view);
        Application::pushActivity(new Activity(frame));
        return true;
    });
}

RayleesTweaksTab::~RayleesTweaksTab() { Settings::instance().save(); }

brls::View* RayleesTweaksTab::create() { return new RayleesTweaksTab(); }
